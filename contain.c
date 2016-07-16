#define _GNU_SOURCE
#include <errno.h>
#include <err.h>
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
#include <sys/syscall.h>
#include <sys/types.h>
#include "contain.h"

void usage(char *progname) {
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
      err(1, "fork");
    case 0:
      raise(SIGSTOP);
      if (geteuid() != 0)
        denysetgroups(parent);
      writemap(parent, GID, gidmap);
      writemap(parent, UID, uidmap);

      if (outside) {
        if (setgid(getgid()) < 0 || setuid(getuid()) < 0)
          errx(1, "Failed to drop privileges");
        execlp(SHELL, SHELL, "-c", outside, NULL);
        err(1, "exec %s", outside);
      }

      exit(EXIT_SUCCESS);
  }

  if (setgid(getgid()) < 0 || setuid(getuid()) < 0)
    errx(1, "Failed to drop privileges");

  if (unshare(CLONE_NEWUSER) < 0)
    errx(1, "Failed to unshare user namespace");

#ifdef CLONE_NEWCGROUP
  if (unshare(CLONE_NEWCGROUP) < 0)
    errx(1, "Failed to unshare cgroup namespace");
#endif

  if (unshare(CLONE_NEWIPC) < 0)
    errx(1, "Failed to unshare IPC namespace");

  if (!hostnet && unshare(CLONE_NEWNET) < 0)
    errx(1, "Failed to unshare network namespace");

  if (unshare(CLONE_NEWNS) < 0)
    errx(1, "Failed to unshare mount namespace");

  if (unshare(CLONE_NEWUTS) < 0)
    errx(1, "Failed to unshare UTS namespace");

  waitforstop(child);
  kill(child, SIGCONT);
  waitforexit(child);

  setgid(0);
  setgroups(0, NULL);
  setuid(0);

  master = stdio ? -1 : getconsole();
  createroot(argv[optind], master, inside);

  unshare(CLONE_NEWPID);
  switch (child = fork()) {
    case -1:
      err(1, "fork");
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
      err(1, "exec");
  }

  return supervise(child, master);
}
