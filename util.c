#define _GNU_SOURCE
#include <errno.h>
#include <err.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include "contain.h"

char *append(char **destination, const char *format, ...) {
  char *extra, *result;
  va_list args;

  va_start(args, format);
  if (vasprintf(&extra, format, args) < 0)
    err(1, "asprintf");
  va_end(args);

  if (*destination == NULL) {
    *destination = extra;
    return extra;
  }

  if (asprintf(&result, "%s%s", *destination, extra) < 0)
      err(1, "asprintf");
  free(*destination);
  free(extra);
  *destination = result;
  return result;
}

char *string(const char *format, ...) {
  char *result;
  va_list args;

  va_start(args, format);
  if (vasprintf(&result, format, args) < 0)
    err(1, "asprintf");
  va_end(args);
  return result;
}

char *tmpdir(void) {
  char *dir;

  if (!(dir = strdup("/tmp/XXXXXX")))
    err(1, "strdup");
  else if (!mkdtemp(dir))
    errx(1, "Failed to create temporary directory");
  return dir;
}

void waitforexit(pid_t child) {
  int status;

  if (waitpid(child, &status, 0) < 0)
    err(1, "waitpid");
  else if (WEXITSTATUS(status) != EXIT_SUCCESS)
    exit(WEXITSTATUS(status));
}

void waitforstop(pid_t child) {
  int status;

  if (waitpid(child, &status, WUNTRACED) < 0)
    err(1, "waitpid");
  if (!WIFSTOPPED(status))
    exit(WEXITSTATUS(status));
}
