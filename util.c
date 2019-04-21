#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/sendfile.h>
#include <sys/types.h>
#include <sys/wait.h>
#include "contain.h"

char *progname;

char *append(char **destination, const char *format, ...) {
  char *extra, *result;
  va_list args;

  va_start(args, format);
  if (vasprintf(&extra, format, args) < 0)
    die(errno, "asprintf");
  va_end(args);

  if (*destination == NULL) {
    *destination = extra;
    return extra;
  }

  if (asprintf(&result, "%s%s", *destination, extra) < 0)
      die(errno, "asprintf");
  free(*destination);
  free(extra);
  *destination = result;
  return result;
}

void die(int errnum, char *format, ...) {
  va_list args;

  fprintf(stderr, "%s: ", progname);
  va_start(args, format);
  vfprintf(stderr, format, args);
  va_end(args);
  if (errnum != 0)
    fprintf(stderr, ": %s\n", strerror(errnum));
  else
    fputc('\n', stderr);
  exit(EXIT_FAILURE);
}

void seal(char **argv, char **envp) {
  const int seals = F_SEAL_SEAL | F_SEAL_SHRINK | F_SEAL_GROW | F_SEAL_WRITE;
  int dst, src;
  ssize_t length;

  if ((src = open("/proc/self/exe", O_RDONLY)) < 0)
    die(errno, "open /proc/self/exe");
  if (fcntl(src, F_GET_SEALS) == seals) {
    close(src);
    return;
  }

  dst = memfd_create("/proc/self/exe", MFD_CLOEXEC | MFD_ALLOW_SEALING);
  if (dst < 0)
    die(errno, "memfd_create");

  while (length = sendfile(dst, src, NULL, BUFSIZ), length != 0)
    if (length < 0 && errno != EAGAIN && errno != EINTR)
      die(errno, "sendfile");
  close(src);

  if (fcntl(dst, F_ADD_SEALS, seals) < 0)
    die(errno, "fcntl F_ADD_SEALS");
  fexecve(dst, argv, envp);
  die(errno, "fexecve");
}

char *string(const char *format, ...) {
  char *result;
  va_list args;

  va_start(args, format);
  if (vasprintf(&result, format, args) < 0)
    die(errno, "asprintf");
  va_end(args);
  return result;
}

char *tmpdir(void) {
  char *dir;

  if (!(dir = strdup("/tmp/XXXXXX")))
    die(errno, "strdup");
  else if (!mkdtemp(dir))
    die(0, "Failed to create temporary directory");
  return dir;
}

void waitforexit(pid_t child) {
  int status;

  if (waitpid(child, &status, 0) < 0)
    die(errno, "waitpid");
  else if (WEXITSTATUS(status) != EXIT_SUCCESS)
    exit(WEXITSTATUS(status));
}

void waitforstop(pid_t child) {
  int status;

  if (waitpid(child, &status, WUNTRACED) < 0)
    die(errno, "waitpid");
  if (!WIFSTOPPED(status))
    exit(WEXITSTATUS(status));
}
