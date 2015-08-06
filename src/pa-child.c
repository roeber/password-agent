#include <errno.h>
#include <signal.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/timerfd.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "pa.h"

int setup_child_inotify(const char* file);
int too_late(long not_after);
int setup_timer(long not_after);
int a_decent_buffer_size(int output_socket);

void in_child(struct job* job) {
  int select_nfds;
  fd_set read_fds;
  int notify_fd;
  int signal_fd;
  int timer_fd;
  int payload_pipe[2];
  int output_socket;

  int buffer_size;
  char* buffer;
  char* bp;
  int buffer_used;
  
  pid_t payload_pid;
  int we_have_waited = 0;

  if (-1 == kill(job->pid, 0)) {
    if (ESRCH == errno) return;
    else die_perror("child: kill(pid, 0)");
  }

  notify_fd = setup_child_inotify(job->ask_file);
  signal_fd = setup_signalfd(SIGCHLD, 0);

  if (SIG_ERR == signal(SIGCHLD, SIG_DFL)) die_perror("child: signal");

  if (job->not_after > 0) {
    if (too_late(job->not_after)) return;
    timer_fd = setup_timer(job->not_after);
  } else {
    timer_fd = 0;
  }

  if (-1 == pipe(payload_pipe)) die_perror("child: pipe");

  output_socket = socket(AF_UNIX, SOCK_DGRAM|SOCK_CLOEXEC, 0);
  if (-1 == output_socket) die_perror("child: socket()");

  buffer_size = a_decent_buffer_size(output_socket);
  buffer = malloc(buffer_size);
  if (NULL == buffer) {
    fprintf(stderr, "Child: can't allocate %d bytes.\n", buffer_size);
    exit(1);
  }

  payload_pid = fork();
  if (-1 == payload_pid) die_perror("child: fork");

  if (0 == payload_pid) {
    close(payload_pipe[0]);
    dup2(payload_pipe[1], 1);
    close(payload_pipe[1]);

    setenv("ACCEPT_CACHED", job->accept_cached ? "1" : "0", 1);
    setenv("ASK_FILE", job->ask_file, 1);

    execl(job->payload, job->payload, job->message, NULL);
    die_perror("payload: execl");
  }

  close(payload_pipe[1]);

  FD_ZERO(&read_fds);
  FD_SET(notify_fd, &read_fds);
  FD_SET(signal_fd, &read_fds);
  if (0 != timer_fd) FD_SET(timer_fd, &read_fds);
  FD_SET(payload_pipe[0], &read_fds);

  select_nfds = ((notify_fd > signal_fd) ? notify_fd : signal_fd) + 1;
  if (timer_fd >= select_nfds) select_nfds = timer_fd + 1;
  if (payload_pipe[0] >= select_nfds) select_nfds = payload_pipe[0] + 1;

  buffer[0] = '+';
  bp = buffer+1;
  buffer_used = 1;

  while ((buffer_size - buffer_used) > 0) {
    fd_set fds = read_fds;
    int rv = select(select_nfds, &fds, NULL, NULL, NULL);
    if (-1 == rv) die_perror("child: select");

    if (FD_ISSET(notify_fd, &fds)) {
      /*
       * The ask file has gone away.  Tell (kill) the payload and go home
       * We use a SIGHUP, because by default it'll terminate the payload, but
       * it can be caught if the payload wants to know that someone else
       * gave an answer.
       */
      if (-1 == kill(payload_pid, SIGHUP)) die_perror("child: kill(SIGHUP)");
      if (-1 == waitpid(payload_pid, NULL, 0)) die_perror("child: waitpid/1");
      exit(1);
    }

    if (FD_ISSET(signal_fd, &fds)) {
      /*
       * Child has died, or at least had a life-threatening experience.
       * If it's a clean death, keep sucking on the pipe.  But if it's a
       * messy one, quit.
       */
      int status = 0;
      if (-1 == waitpid(payload_pid, &status, WNOHANG)) die_perror("child: waitpid/2");
      if (WIFEXITED(status) && (0 == WEXITSTATUS(status))) {
        we_have_waited = 1;
      } else {
        fprintf(stderr, "Child: payload abended, status = %d\n", status);
        exit(1);
      }
    }

    if ((0 != timer_fd) && FD_ISSET(timer_fd, &fds)) {
      /*
       * We timed out.  Tell (kill) the payload and go home.
       */
      if (-1 == kill(payload_pid, SIGTERM)) die_perror("child: kill(SIGTERM)");
      if (-1 == waitpid(payload_pid, NULL, 0)) die_perror("child: waitpid/3");
      exit(1);
    }

    if (FD_ISSET(payload_pipe[0], &fds)) {
      int r = read(payload_pipe[0], bp, (buffer_size - buffer_used));
      if (-1 == r) die_perror("child: read");

      if (0 == r) break;  /* End of file */

      bp += r;
      buffer_used += r;
    }
  }

  if (0 == (buffer_size - buffer_used)) {
    fprintf(stderr, "Warning: payload generated at least %d bytes.\n", buffer_size);
  }

  {
    struct sockaddr_un sun;
    int rv;

    sun.sun_family = AF_UNIX;
    strncpy(sun.sun_path, job->socket_name, sizeof(sun.sun_path));

    rv = sendto(output_socket, buffer, buffer_used, MSG_NOSIGNAL, (struct sockaddr *)&sun,
                offsetof(struct sockaddr_un, sun_path) + strlen(job->socket_name));
    if (-1 == rv) die_perror("child: sendto");
  }

  if (0 == we_have_waited) {
    /* Payload has closed its stdout, but hasn't died yet.  Kill it! */
    if (-1 == kill(payload_pid, SIGINT)) die_perror("child: kill(SIGINT)");
    if (-1 == waitpid(payload_pid, NULL, 0)) die_perror("child: waitpid/4");
    we_have_waited = 1;
  }

  /* Just drop our resources on the floor and go. */
  exit(0);
}
  
int setup_child_inotify(const char* file) {
  int fd;
  int wd;

  fd = inotify_init1(IN_CLOEXEC);
  if (-1 == fd) die_perror("child: inotify_init1");

  wd = inotify_add_watch(fd, file, IN_DELETE_SELF);
  if (-1 == wd) die_perror("child: inotify_add_watch");

  return fd;
}

int too_late(long not_after) {
  struct timespec ts;
  int rv;

  memset(&ts, 0, sizeof(ts));
  rv = clock_gettime(CLOCK_MONOTONIC, &ts);
  if (-1 == rv) die_perror("child: clock_gettime");

  return (((ts.tv_sec * 1000000) + (ts.tv_nsec / 1000)) > not_after);
}

int setup_timer(long not_after) {
  int fd;
  struct itimerspec its;

  fd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC);
  if (-1 == fd) die_perror("child: timerfd_create");

  memset(&its, 0, sizeof(its));
  its.it_value.tv_sec = not_after / 1000000;
  its.it_value.tv_nsec = (not_after % 1000000) * 1000;
  if (-1 == timerfd_settime(fd, TFD_TIMER_ABSTIME, &its, NULL)) die_perror("child: timerfd_settime");

  return fd;
}

int a_decent_buffer_size(int output_socket) {
  int val = 0;
  socklen_t valsize = sizeof(val);
  int rv = getsockopt(output_socket, AF_UNIX, SO_SNDBUF, &val, &valsize);
  if (-1 == rv) return 16384;

  if (val > 65536) return 65536;
  if (val < 0) return 4096;

  return val;
}
