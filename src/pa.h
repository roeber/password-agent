#ifndef PA_H
#define PA_H

#include <sys/param.h>
#include <stdio.h>

struct job {
  const char* payload;
  char ask_file[PATH_MAX];
  char socket_name[PATH_MAX];
  char message[BUFSIZ];
  pid_t pid;
  int accept_cached;
  long not_after;
};

/* pa4-util.c */
void die_perror(const char* s);
void die_perror_args(const char* s, ...);
int setup_signalfd(int sig, ...);
void drain_notify(int fd);

/* pa4-child.c */
void in_child(struct job* job);

#endif /* PA_H */
