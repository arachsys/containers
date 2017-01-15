/* list-containers - list information about active containers.

   See: https://github.com/arachsys/containers

   Copyright (C) Assaf Gordon (assafgordon@gmail.com)
   License: MIT
*/
#define _GNU_SOURCE
#include <dirent.h>
#include <sys/types.h>
#include <errno.h>
#include <error.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <stdbool.h>
#include <getopt.h>

#define CONTAINED_ENV_MARKER "container=contain"
#define PROCFS_MOUNTPOINT "/proc"

// If true, print only the PID of the containers
static bool bare_mode = false;

// if true, exit with error if no containers found
static bool fail_if_no_containers = false;

// defined in 'util.c' - no header yet
extern char *string(const char *format, ...);

/* returns TRUE if this process can access the information
   about process PID (based on read access to files in
   /proc/<pid>/environ.

   returns FALSE if access is defined.

   terminates on any other error except "permission denied". */
bool is_process_readable(const size_t pid)
{
  char *p = string("%s/%zu/environ", PROCFS_MOUNTPOINT, pid);
  errno = 0;

  int fd = open(p,O_RDONLY);
  if (fd == -1 && errno != EACCES)
    error(1, errno, "failed to open '%s'", p);
  if (fd!=-1)
    close(fd);
  free (p);

  return (fd != -1);
}

/* Return true if process PID is a 'contained' process,
   based on existance of environment variable 'container=contain'
   (which is automatically set by the 'contain' program).

   Returns FALSE if not contained.

   Terminates on any error. */
int is_process_contained(const size_t pid)
{
  char *line = NULL;
  size_t alloc = 0;
  ssize_t len;
  bool contained = false;
  const size_t explen = strlen(CONTAINED_ENV_MARKER);

  char *p = string("%s/%zu/environ", PROCFS_MOUNTPOINT, pid);
  FILE *f = fopen(p, "rb");
  if (f==NULL)
    error(1,errno,"open(%s) failed", p);

  errno = 0;
  while ( (len = getdelim(&line, &alloc, '\0', f)) != -1 ) {
    if ((size_t)len != (explen+1)) //+1 for the delimiter
      continue;

    if (strncmp(line,CONTAINED_ENV_MARKER,explen)==0) {
      contained = true;
      break;
    }
  }
  if (errno != 0)
    error(1,errno,"reading(%s) failed", p);

  if (fclose(f)!=0)
    error(1,errno,"close(%s) failed",p);

  free (p);
  free (line);

  return contained;
}

/* Returns a string containing the PATH of the mounted/chroot'd root
   for the contained process. The returned value should be free'd.
   Returns NULL if mounted directory could not be detected.

   NOTE:
   This relies on 'contain's implementation of using
   a bind-mount+pivot_root syscall.

   The file /proc/<pid>/mountinfo will contain the host's directory
   as mounted to '/' in one of the entries.

   Terminates on any error. */
char* get_container_root_mount_dir(const size_t pid)
{
  char *line = NULL;
  size_t alloc = 0;
  ssize_t len;
  char *root = NULL;
  char *mountpoint = NULL;

  char *p = string("%s/%zu/mountinfo", PROCFS_MOUNTPOINT, pid);
  FILE *f = fopen(p, "rb");
  if (f==NULL)
    error(1,errno,"open(%s) failed", p);

  errno = 0;
  while ( (len = getline(&line, &alloc, f)) != -1 ) {
    /* Format of /proc/<pid>/mountinfo according to 'proc(5)':
       1. Mount ID
       2. Parent Mount ID
       3. Major:Minor device ID
       4. src/root directory
       5. Mount Point
     */
    int i = sscanf(line, "%*u %*u %*u:%*u %ms %ms ", &root, &mountpoint);

    // either a bug in the scanf format string, or new/unexpected format
    if (i != 2)
      error(1,0, "input error in '%s': unrecognized line format '%s'\n",
            p, line);

    // we're looking for the root mount point
    if (strcmp("/",mountpoint)==0) {
      //don't free 'root' - it's the returned value
      free(mountpoint);
      break;
    }

    free (root);
    free (mountpoint);
    root = NULL;
    mountpoint = NULL;
  }
  if (errno != 0)
    error(1,errno,"reading(%s) failed", p);

  if (fclose(f)!=0)
    error(1,errno,"close(%s) failed",p);

  free (p);
  free (line);

  return root;
}

/* Read a single line from a file under /proc/<PID> .
   The returned value is a NUL-terminated  'char*' which must be freed.
   If the line was terminated with a newline (\x0a), it is removed.
   NUL is added if needed.

   terminates on any error. */
char* read_proc_pid_file(const size_t pid, const char* file)
{
  char *line = NULL;
  size_t alloc = 0;
  ssize_t len;

  char *p = string("%s/%zu/%s", PROCFS_MOUNTPOINT, pid, file);
  FILE *f = fopen(p, "rb");
  if (f==NULL)
    error(1,errno,"open(%s) failed", p);

  errno = 0;
  len = getline(&line, &alloc, f);
  if (errno != 0)
    error(1,errno,"reading(%s) failed", p);
  if (len<0)
    error(1,0,"reading(%s) failed (premature end-of-file)", p);

  /* TODO: if empty files are OK, remove this check */
  if (len==0)
    error(1,0,"reading(%s) failed (empty file)", p);

  if (fclose(f)!=0)
    error(1,errno,"close(%s) failed",p);

  free (p);

  // Chomp the newline
  if (line[len-1] == '\n')
    line[len-1] = '\0';

  // NOTE: 'getline' always NUL-terminates the buffer,
  //       so there's no need to check for it.
  return line;
}

/* Given a stat line (from /proc/<pid>/stat), extract the numeric parent ID,
   or terminate on failure.
   line format is defined in 'proc(5)'. */
size_t get_ppid_from_stat(const char* s)
{
  int ppid=0;
  int i = sscanf(s, "%*d %*s %*c %d ", &ppid);
  if (i!=1 || ppid<0)
    error(1,0,"internal error: invalid stat line '%s'", s);
  return (size_t)ppid;
}

/* Returns the Process-ID of the parent process of PID.
   Terminates on error.
   Uses /proc/<pid>/stat file. */
size_t get_process_ppid(const size_t pid)
{
    char *stat = read_proc_pid_file(pid,"stat");
    size_t ppid = get_ppid_from_stat (stat);
    free (stat);
    return ppid;
}


/* Iterates current processes on the host,
   identifies contained processes,
   and prints information about them to STDOUT. */
void find_container_processes()
{
  DIR *dirp;
  struct dirent *de;
  long pid;
  char *endp;
  bool found = false;

  if (!(dirp = opendir(PROCFS_MOUNTPOINT)))
    error(1, errno, "opendir(%s) failed", PROCFS_MOUNTPOINT);

  while ( (de=readdir(dirp)) ) {
    if ((de->d_type != DT_DIR) || !isdigit( (int)de->d_name[0] ))
      continue;

    errno = 0;
    pid = strtol(de->d_name, &endp, 10);
    if (errno != 0 || pid==0 || *endp != '\0')
      continue;

    if (!is_process_readable(pid))
      continue;

    if (!is_process_contained(pid))
      continue;

    /* If the parent process is also 'contained', then this is just
       another process in the container - not the 'init' process. */
    size_t ppid = get_process_ppid(pid);
    if (is_process_contained(ppid))
      continue;

    if (bare_mode) {
      printf("%zu\n", ppid);
      found = true;
      continue;
    }

    //Print header before the first container
    if (!found)
      printf("PID    init-pid(1)  init-cmd    root-dir\n");
    found = true;

    // Print full information about the container
    char *root = get_container_root_mount_dir(pid);
    char *cmdline = read_proc_pid_file(pid,"cmdline");
    printf("%-6zu %-12zu %-11s %s\n",
           ppid, pid,cmdline, root);

    free (root);
    free (cmdline);
  }

  if (closedir(dirp)==-1)
    error(1, errno, "closedir failed");

  if (!found && fail_if_no_containers)
    error(1, 0, "error: no containers found");
}

static struct option longopts[] = {
  {"bare",no_argument,0,'b'},
  {"fail-if-missing",no_argument,0,'f'},
  {"help",no_argument,0,'h'},
  {0,0,0,0}
};

void usage()
{
  printf("\
List active containers started by 'contain'\n\
\n\
Usage: list-containers [OPTIONS]\n\
\n\
OPTIONS:\n\
 -b, --bare      print only the PID of the container,\n\
                 without header or information about the init processs.\n\
\n\
 -f, --fail-if-missing\n\
                 exit with error if no containers found\n\
                 (useful for autmation)\n\
\n\
 -h, --help      this help screen.\n\
\n\
OUTPUT:\n\
1. PID      - process ID of the container (the 'contain' program).\n\
              Can be used with 'inject'.\n\
2. init-PID - The host process ID of the INIT program\n\
              (pid 1 inside the container)\n\
3. init-cmd - The init program (inside the container)\n\
4. root-dir - The host directory which serves as the container's root.\n\
\n\
See: https://github.com/arachsys/containers\n\
List-containers written By Assaf Gordon.\n\
License: MIT\n\
\n\
");
         exit(0);
}

int main(int argc, char* argv[])
{
  int c;

  while ( (c=getopt_long(argc,argv,"hbf", longopts, NULL))!=-1) {
    switch(c)
      {
      case 'b':
        bare_mode = true;
        break;

      case 'f':
        fail_if_no_containers = true;
        break;

      case 'h':
        usage();
        break;

      default:
        error(1,0,"invalid command line parameter. " \
                  "See --help for more information");

      }
  }

  find_container_processes ();

  return 0;
}
