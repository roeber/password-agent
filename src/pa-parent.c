#include <dirent.h>
#include <errno.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <sys/select.h>
#include <sys/signalfd.h>
#include <sys/types.h>
#include <unistd.h>

#include "pa.h"

#define WATCH_DIR "/run/systemd/ask-password"

int setup_parent_inotify(void);
void scan_dir(const char* payload);
void process_ask_file(const char* name, const char* payload);
void do_child(struct job* job);

int main(int argc, char* argv[]) {
  int notify_fd = setup_parent_inotify();
  int signal_fd = setup_signalfd(SIGINT, SIGTERM, 0);
  int select_nfds = ((notify_fd > signal_fd) ? notify_fd : signal_fd) + 1;
  const char* payload;

  if (SIG_ERR == signal(SIGCHLD, SIG_IGN)) die_perror("signal(SIGCHLD)");

  payload = argv[1];
  if (-1 == access(payload, X_OK)) die_perror_args("Can't execute payload %s", payload);

  scan_dir(payload);

  while (1) {
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(notify_fd, &readfds);
    FD_SET(signal_fd, &readfds);

    if (-1 == select(select_nfds, &readfds, NULL, NULL, NULL)) {
      if (EINTR == errno) continue;
      else die_perror("select");
    }

    if (FD_ISSET(notify_fd, &readfds)) {
      scan_dir(payload);
      drain_notify(notify_fd);
    }

    if (FD_ISSET(signal_fd, &readfds)) break;
  }

  return 0;
}

int setup_parent_inotify(void) {
  int notify_fd;
  int dir_wd;

  notify_fd = inotify_init1(IN_CLOEXEC);
  if (-1 == notify_fd) die_perror("inotify_init1");

  dir_wd = inotify_add_watch(notify_fd, WATCH_DIR, IN_CLOSE_WRITE|IN_MOVED_TO);
  if (-1 == dir_wd) die_perror("inotify_add_watch(" WATCH_DIR ")");

  return notify_fd;
}

void scan_dir(const char* payload) {
  DIR* dir;
  struct dirent* de;

  dir = opendir(WATCH_DIR);
  if (NULL == dir) die_perror("opendir(" WATCH_DIR ")");

  for (de = readdir(dir); NULL != de; de = readdir(dir)) {
    if (de->d_type == DT_REG && (0 == strncmp(de->d_name, "ask.", 4))) {
      process_ask_file(de->d_name, payload);
    }
  }

  if (-1 == closedir(dir)) die_perror("closedir");
  return;
}

void process_ask_file(const char* name, const char* payload) {
  char* p;
  FILE* fp;
  char line[BUFSIZ];
  int in_ask = 0;

  struct job job;
  memset(&job, 0, sizeof(job));
  job.payload = payload;
  job.pid = -1;
  job.accept_cached = 0;
  job.not_after = 0;

  p = stpcpy(job.ask_file, WATCH_DIR);
  *p++ = '/';
  (void)stpcpy(p, name);

  fp = fopen(job.ask_file, "r");
  if (NULL == fp) {
    if (ENOENT == errno) return; /* Maybe it already went away? */
    die_perror_args("fopen(%s)", name);
  }

  while (NULL != fgets(line, sizeof(line), fp)) {
    char key[BUFSIZ];
    char value[BUFSIZ];

    if (('#' == *line) || (';' == *line)) continue;
    if ('\0' == line[ strspn(line, " \f\n\r\t\v") ]) continue;

    if (1 == sscanf(line, "\[%[^]\001-\037]]", key)) {
      in_ask = ! strcmp(key, "Ask");
      continue;
    }

    if (!in_ask) continue;

    if (2 == sscanf(line, "%[A-Za-z0-9-] = %[^\n]", key, value)) {
      if (0 == strcmp(key, "PID"))          job.pid=atoi(value);
      if (0 == strcmp(key, "Socket"))       strncpy(job.socket_name, value, sizeof(job.socket_name));
      if (0 == strcmp(key, "AcceptCached")) job.accept_cached=atoi(value);
      if (0 == strcmp(key, "NotAfter"))     job.not_after=atol(value);
      if (0 == strcmp(key, "Message"))      strncpy(job.message, value, sizeof(job.message));
      continue;
    }

    /* At this point we have an unrecognized line; we'll just skip it. */
  }

  fclose(fp);

  if ((-1 != job.pid) && (0 != strlen(job.socket_name))) {
    do_child(&job);
  }
}

void do_child(struct job* job) {
  pid_t pid = fork();

  if (-1 == pid) die_perror("fork()");
  if (0 != pid) return;

  in_child(job);
  /* Guarantee that this function won't return. */
  exit(0);
}
