#define _GNU_SOURCE
#include <errno.h>
#include <err.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/signalfd.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include "contain.h"

static struct termios saved;

int getconsole(void) {
  int master, null;

  if ((null = open("/dev/null", O_RDWR)) < 0)
    errx(1, "Failed to open /dev/null");

  if (fcntl(STDIN_FILENO, F_GETFD) < 0)
    dup2(null, STDIN_FILENO);
  if (fcntl(STDOUT_FILENO, F_GETFD) < 0)
    dup2(null, STDOUT_FILENO);
  if (fcntl(STDERR_FILENO, F_GETFD) < 0)
    dup2(null, STDERR_FILENO);

  if (null != STDIN_FILENO)
    if (null != STDOUT_FILENO)
      if (null != STDERR_FILENO)
        close(null);

  if ((master = posix_openpt(O_RDWR | O_NOCTTY)) < 0)
    errx(1, "Failed to allocate a console pseudo-terminal");
  grantpt(master);
  unlockpt(master);
  return master;
}

static void rawmode(void) {
  struct termios termios;

  if (!isatty(STDIN_FILENO))
    return;
  if (tcgetattr(STDIN_FILENO, &termios) < 0)
    err(1, "tcgetattr");
  cfmakeraw(&termios);
  tcsetattr(STDIN_FILENO, TCSANOW, &termios);
}

static void restoremode(void) {
  if (isatty(STDIN_FILENO))
    tcsetattr(STDIN_FILENO, TCSANOW, &saved);
}

static void savemode(void) {
  if (isatty(STDIN_FILENO) && tcgetattr(STDIN_FILENO, &saved) < 0)
    err(1, "tcgetattr");
}

void setconsole(char *name) {
  int console;
  struct termios termios;

  setsid();

  if ((console = open(name, O_RDWR)) < 0)
    errx(1, "Failed to open console in container");
  ioctl(console, TIOCSCTTY, NULL);

  if (tcgetattr(console, &termios) < 0)
    err(1, "tcgetattr");
  termios.c_iflag |= IGNBRK | IUTF8;
  tcsetattr(console, TCSANOW, &termios);

  dup2(console, STDIN_FILENO);
  dup2(console, STDOUT_FILENO);
  dup2(console, STDERR_FILENO);
  if (console != STDIN_FILENO)
    if (console != STDOUT_FILENO)
      if (console != STDERR_FILENO)
        close(console);
}

int supervise(pid_t child, int console) {
  char buffer[PIPE_BUF];
  int signals, slave, status;
  sigset_t mask;
  ssize_t count, length, offset;
  struct pollfd fds[3];

  if (console < 0) {
    if (waitpid(child, &status, 0) < 0)
      err(1, "waitpid");
    return WIFEXITED(status) ? WEXITSTATUS(status) : EXIT_FAILURE;
  }

  sigemptyset(&mask);
  sigaddset(&mask, SIGCHLD);
  sigprocmask(SIG_BLOCK, &mask, NULL);
  if ((signals = signalfd(-1, &mask, 0)) < 0)
    err(1, "signalfd");

  if (waitpid(child, &status, WNOHANG) > 0)
    if (WIFEXITED(status) || WIFSIGNALED(status))
      raise(SIGCHLD);

  savemode();
  atexit(restoremode);
  rawmode();

  slave = open(ptsname(console), O_RDWR);

  fds[0].fd = console;
  fds[0].events = POLLIN;
  fds[1].fd = STDIN_FILENO;
  fds[1].events = POLLIN;
  fds[2].fd = signals;
  fds[2].events = POLLIN;

  while (1) {
    if (poll(fds, 3, -1) < 0)
        if (errno != EAGAIN && errno != EINTR)
          err(1, "poll");

    if (fds[0].revents & POLLIN) {
      if ((length = read(console, buffer, sizeof(buffer))) < 0)
        if (errno != EAGAIN && errno != EINTR)
          err(1, "read");
      for (offset = 0; length > 0; offset += count, length -= count)
        while ((count = write(STDOUT_FILENO, buffer + offset, length)) < 0)
          if (errno != EAGAIN && errno != EINTR)
            err(1, "write");
    }

    if (fds[1].revents & (POLLHUP | POLLIN)) {
      if ((length = read(STDIN_FILENO, buffer, sizeof(buffer))) == 0)
        fds[1].events = 0;
      else if (length < 0 && errno != EAGAIN && errno != EINTR)
        err(1, "read");
      for (offset = 0; length > 0; offset += count, length -= count)
        while ((count = write(console, buffer + offset, length)) < 0)
          if (errno != EAGAIN && errno != EINTR)
            err(1, "write");
    }

    if (fds[2].revents & POLLIN) {
      if (read(signals, buffer, sizeof(buffer)) < 0)
        if (errno != EAGAIN && errno != EINTR)
          err(1, "read");
      if (waitpid(child, &status, WNOHANG) > 0)
        if (WIFEXITED(status) || WIFSIGNALED(status))
          break;
    }
  }

  close(signals);
  close(slave);

  while ((length = read(console, buffer, sizeof(buffer)))) {
    if (length < 0 && errno != EAGAIN && errno != EINTR)
      break;
    for (offset = 0; length > 0; offset += count, length -= count)
      while ((count = write(STDOUT_FILENO, buffer + offset, length)) < 0)
        if (errno != EAGAIN && errno != EINTR)
          err(1, "write");
  }

  return WIFEXITED(status) ? WEXITSTATUS(status) : EXIT_FAILURE;
}
