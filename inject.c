#define _GNU_SOURCE
#include <dirent.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include "contain.h"

static int getparent(pid_t child) {
  char *end, *line = NULL, *path, *start;
  pid_t parent = -1;
  size_t size;
  FILE *file;

  path = string("/proc/%u/stat", child);
  file = fopen(path, "r");
  free(path);

  if (file && getline(&line, &size, file) >= 0)
    /* "PID (NAME) S PPID ...", so PPID begins 4 chars after the last ')' */
    if ((start = strrchr(line, ')')) && strlen(start) >= 4) {
      parent = strtol(start + 4, &end, 10);
      if (end == start || *end != ' ')
        parent = -1;
    }

  if (file)
    fclose(file);
  if (line)
    free(line);

  return parent;
}

static void join(pid_t pid, char *type) {
  char *path;
  int fd;

  path = string("/proc/%u/ns/%s", pid, type);

  if ((fd = open(path, O_RDONLY)) >= 0) {
    if (syscall(__NR_setns, fd, 0) < 0 && strcmp(type, "user") == 0)
      errx(EXIT_FAILURE, "Failed to join user namespace");
    close(fd);
  } else if (errno != ENOENT) {
    errx(EXIT_FAILURE, "PID %u does not belong to you", pid);
  } else if (strcmp(type, "user") == 0) {
    errx(EXIT_FAILURE, "PID %u not found or user namespace unavailable", pid);
  }

  free(path);
}

static void usage(const char *progname) {
  fprintf(stderr, "Usage: %s PID [CMD [ARG]...]\n", progname);
  exit(64);
}

int main(int argc, char **argv, char **envp) {
  char *end, *item = NULL, *path;
  pid_t child = -1, parent, pid;
  size_t size;
  struct dirent *entry;
  DIR *dir;
  FILE *file;

  seal(argv, envp);
  if (argc < 2)
    usage(argv[0]);

  parent = strtol(argv[1], &end, 10);
  if (end == argv[1] || *end)
    usage(argv[0]);

  if (geteuid() != getuid())
    errx(EXIT_FAILURE, "setuid installation is unsafe");
  else if (getegid() != getgid())
    errx(EXIT_FAILURE, "setgid installation is unsafe");

  join(parent, "user");
  setgid(0);
  setgroups(0, NULL);
  setuid(0);

  if (!(dir = opendir("/proc")))
    errx(EXIT_FAILURE, "Failed to list processes");
  while (child < 0 && (entry = readdir(dir))) {
    pid = strtol(entry->d_name, &end, 10);
    if (end == entry->d_name || *end)
      continue;
    if (getparent(pid) == parent) {
      path = string("/proc/%u/environ", pid);
      if ((file = fopen(path, "r"))) {
        while (getdelim(&item, &size, '\0', file) >= 0)
          if (strcmp(item, "container=contain") == 0)
            child = pid;
        fclose(file);
      }
      free(path);
    }
  }
  closedir(dir);
  if (item)
    free(item);

  if (child < 0)
    errx(EXIT_FAILURE, "PID %u is not a container supervisor", parent);

  join(child, "cgroup");
  join(child, "ipc");
  join(child, "net");
  join(child, "pid");
  join(child, "time");
  join(child, "uts");
  join(child, "mnt");

  if (chdir("/") < 0)
    errx(EXIT_FAILURE, "Failed to enter container root directory");

  switch (child = fork()) {
    case -1:
      err(EXIT_FAILURE, "fork");
    case 0:
      if (argv[2])
        execvp(argv[2], argv + 2);
      else if (getenv("SHELL"))
        execl(getenv("SHELL"), getenv("SHELL"), NULL);
      else
        execl(SHELL, SHELL, NULL);
      err(EXIT_FAILURE, "exec");
  }

  waitforexit(child);
  return EXIT_SUCCESS;
}
