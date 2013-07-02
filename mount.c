#define _GNU_SOURCE
#include <errno.h>
#include <error.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include "contain.h"

static void bindnode(char *src, char *dst) {
  int fd;

  if ((fd = open(dst, O_WRONLY | O_CREAT, 0600)) >= 0)
    close(fd);
  if (mount(src, dst, NULL, MS_BIND, NULL) < 0)
    error(1, 0, "Failed to bind %s into new /dev filesystem", src);
}

void buildroot(char *src, char *dst, char *console, char *helper) {
  mode_t mask;
  pid_t child;

  if (mount(src, dst, NULL, MS_BIND | MS_REC, NULL) < 0)
    error(1, 0, "Failed to bind new root filesystem");
  else if (chdir(dst) < 0)
    error(1, 0, "Failed to enter new root filesystem");

  mask = umask(0);
  mkdir("dev" , 0755);
  if (mount("tmpfs", "dev", "tmpfs", 0, "mode=0755") < 0)
    error(1, 0, "Failed to mount /dev tmpfs in new root filesystem");

  mkdir("dev/pts", 0755);
  if (mount("devpts", "dev/pts", "devpts", 0, "newinstance,ptmxmode=666") < 0)
    error(1, 0, "Failed to mount /dev/pts in new root filesystem");

  mkdir("dev/tmp", 0755);
  mkdir("proc" , 0755);
  mkdir("sys" , 0755);
  umask(mask);

  if (console)
    bindnode(console, "dev/console");
  bindnode("/dev/full", "dev/full");
  bindnode("/dev/null", "dev/null");
  bindnode("/dev/random", "dev/random");
  bindnode("/dev/tty", "dev/tty");
  bindnode("/dev/urandom", "dev/urandom");
  bindnode("/dev/zero", "dev/zero");
  symlink("pts/ptmx", "dev/ptmx");

  if (helper)
    switch (child = fork()) {
      case -1:
        error(1, errno, "fork");
      case 0:
        putenv("container=contain-inside-helper");
        execlp(helper, helper, NULL);
        error(1, errno, "exec %s", helper);
      default:
        waitforexit(child);
    }

  if (syscall(__NR_pivot_root, ".", "dev/tmp") < 0)
    error(1, 0, "Failed to pivot into new root filesystem");

  if (chdir("/dev/tmp") >= 0) {
    while (*dst == '/')
      dst++;
    rmdir(dst);
  }

  if (chdir("/") < 0 || umount2("/dev/tmp", MNT_DETACH) < 0)
    error(1, 0, "Failed to detach old root filesystem");
  else
    rmdir("/dev/tmp");
}

void mountproc(void) {
  if (mount("proc", "proc", "proc", 0, NULL) < 0)
    error(1, 0, "Failed to mount /proc in new root filesystem");
}

void mountsys(void) {
  if (mount("sysfs", "sys", "sysfs", 0, NULL) < 0)
    error(1, 0, "Failed to mount /sys in new root filesystem");
}
