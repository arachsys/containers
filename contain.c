#define _GNU_SOURCE
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>
#include <linux/sched.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include "contain.h"

static void usage(const char *progname) {
  fprintf(stderr, "\
Usage: %s [OPTIONS] DIR [CMD [ARG]...]\n\
Options:\n\
  -c        disable console emulation in the container\n\
  -g MAP    set the container-to-host GID map\n\
  -i CMD    run a helper child inside the new namespaces\n\
  -n        share the host network unprivileged in the container\n\
  -o CMD    run a helper child outside the new namespaces\n\
  -u MAP    set the container-to-host UID map\n\
GID and UID maps are specified as START:LOWER:COUNT[,START:LOWER:COUNT]...\n\
", progname);
  exit(EX_USAGE);
}

int main(int argc, char **argv) {
  char *gidmap = NULL, *inside = NULL, *outside = NULL, *uidmap = NULL;
  int hostnet = 0, master, option, stdio = 0;
  pid_t child, parent;

  while ((option = getopt(argc, argv, "+:cg:i:no:u:")) > 0)
    switch (option) {
      case 'c':
        stdio++;
        break;
      case 'g':
        gidmap = optarg;
        break;
      case 'i':
        inside = optarg;
        break;
      case 'n':
        hostnet++;
        break;
      case 'o':
        outside = optarg;
        break;
      case 'u':
        uidmap = optarg;
        break;
      default:
        usage(argv[0]);
    }

  if (argc <= optind)
    usage(argv[0]);

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

      if (outside) {
        if (setgid(getgid()) < 0 || setuid(getuid()) < 0)
          errx(EXIT_FAILURE, "Failed to drop privileges");
        prctl(PR_SET_DUMPABLE, 1);
        execlp(SHELL, SHELL, "-c", outside, NULL);
        err(EXIT_FAILURE, "exec %s", outside);
      }

      exit(EXIT_SUCCESS);
  }

  if (setgid(getgid()) < 0 || setuid(getuid()) < 0)
    errx(EXIT_FAILURE, "Failed to drop privileges");
  prctl(PR_SET_DUMPABLE, 1);

  if (unshare(CLONE_NEWUSER) < 0)
    errx(EXIT_FAILURE, "Failed to unshare user namespace");

#ifdef CLONE_NEWCGROUP
  if (unshare(CLONE_NEWCGROUP) < 0)
    errx(EXIT_FAILURE, "Failed to unshare cgroup namespace");
#endif

  if (unshare(CLONE_NEWIPC) < 0)
    errx(EXIT_FAILURE, "Failed to unshare IPC namespace");

  if (!hostnet && unshare(CLONE_NEWNET) < 0)
    errx(EXIT_FAILURE, "Failed to unshare network namespace");

  if (unshare(CLONE_NEWNS) < 0)
    errx(EXIT_FAILURE, "Failed to unshare mount namespace");

#ifdef CLONE_NEWTIME
  if (unshare(CLONE_NEWTIME) < 0)
    errx(EXIT_FAILURE, "Failed to unshare time namespace");
#endif

  if (unshare(CLONE_NEWUTS) < 0)
    errx(EXIT_FAILURE, "Failed to unshare UTS namespace");

  waitforstop(child);
  kill(child, SIGCONT);
  waitforexit(child);

  setgid(0);
  setgroups(0, NULL);
  setuid(0);

  master = stdio ? -1 : getconsole();
  createroot(argv[optind], master, inside);

  if (unshare(CLONE_NEWPID) < 0)
    errx(EXIT_FAILURE, "Failed to unshare PID namespace");

  switch (child = fork()) {
    case -1:
      err(EXIT_FAILURE, "fork");
    case 0:
      mountproc();
      if (!hostnet)
        mountsys();
      enterroot();

      if (master >= 0) {
        close(master);
        setconsole("/dev/console");
      }

      clearenv();
      putenv("container=contain");

      if (argv[optind + 1])
        execv(argv[optind + 1], argv + optind + 1);
      else
        execl(SHELL, SHELL, NULL);
      err(EXIT_FAILURE, "exec");
  }

  return supervise(child, master);
}
