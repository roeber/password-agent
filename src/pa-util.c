#include <errno.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <sys/signalfd.h>
#include <unistd.h>

#include "pa.h"

void die_perror(const char* s) {
  perror(s);
  exit(1);
}

void die_perror_args(const char* s, ...) {
  char buffer[BUFSIZ];
  va_list ap;
  int save = errno;
  va_start(ap, s);
  if (vsnprintf(buffer, sizeof(buffer), s, ap) < 0) strncpy(buffer, s, sizeof(buffer));
  errno = save;
  die_perror(buffer);
}

int setup_signalfd(int sig, ...) {
  sigset_t signals;
  va_list ap;
  int signal_fd;

  if (-1 == sigemptyset(&signals)) die_perror("sigemptyset");
  va_start(ap, sig);

  while (0 != sig) {
    if (-1 == sigaddset(&signals, sig)) die_perror_args("sigaddset(%d)", sig);
    sig = va_arg(ap, int);
  }

  va_end(ap);

  if (-1 == sigprocmask(SIG_SETMASK, &signals, NULL)) die_perror("sigprocmask");

  signal_fd = signalfd(-1, &signals, SFD_NONBLOCK|SFD_CLOEXEC);
  if (-1 == signal_fd) die_perror("signalfd");

  return signal_fd;
}

void drain_notify(int fd) {
  char buffer[ sizeof(struct inotify_event) + NAME_MAX + 1 + sizeof(int) ];
  int len;

  len = read(fd, buffer, sizeof(buffer));
  if (-1 == len) die_perror("read(notify_fd)");

  return;
}
