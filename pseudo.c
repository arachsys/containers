#define _GNU_SOURCE
#include <err.h>
#include <errno.h>
#include <grp.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sysexits.h>
#include <unistd.h>
#include <sys/prctl.h>
#include "contain.h"

static void usage(const char *progname) {
  fprintf(stderr, "\
Usage: %s [OPTIONS] [CMD [ARG]...]\n\
Options:\n\
  -g MAP    set the user namespace GID map\n\
  -u MAP    set the user namespace UID map\n\
GID and UID maps are specified as START:LOWER:COUNT[,START:LOWER:COUNT]...\n\
", progname);
  exit(EX_USAGE);
}

int main(int argc, char **argv) {
  char *gidmap = NULL, *uidmap = NULL;
  int option;
  pid_t child, parent;

  while ((option = getopt(argc, argv, "+:g:u:")) > 0)
    switch (option) {
      case 'g':
        gidmap = optarg;
        break;
      case 'u':
        uidmap = optarg;
        break;
      default:
        usage(argv[0]);
    }

  parent = getpid();
  switch (child = fork()) {
    case -1:
      err(EXIT_FAILURE, "fork");
    case 0:
      raise(SIGSTOP);
      if (geteuid() != 0)
        denysetgroups(parent);
      writemap(parent, GID, gidmap);
      writemap(parent, UID, uidmap);
      exit(0);
  }

  if (setgid(getgid()) < 0 || setuid(getuid()) < 0)
    errx(EXIT_FAILURE, "Failed to drop privileges");
  prctl(PR_SET_DUMPABLE, 1);

  if (unshare(CLONE_NEWUSER) < 0)
    errx(EXIT_FAILURE, "Failed to unshare user namespace");

  waitforstop(child);
  kill(child, SIGCONT);
  waitforexit(child);

  setgid(0);
  setgroups(0, NULL);
  setuid(0);

  if (argv[optind])
    execvp(argv[optind], argv + optind);
  else if (getenv("SHELL"))
    execl(getenv("SHELL"), getenv("SHELL"), NULL);
  else
    execl(SHELL, SHELL, NULL);

  err(EXIT_FAILURE, "exec");
  return EXIT_FAILURE;
}
