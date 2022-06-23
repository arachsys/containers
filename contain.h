#ifndef CONTAIN_H
#define CONTAIN_H

#define GID 0
#define UID 1
#define INVALID ((unsigned) -1)
#define SHELL "/bin/sh"

#define getid(type) ((unsigned) ((type) == GID ? getgid() : getuid()))
#define idfile(type) ((type) == GID ? "gid_map" : "uid_map")
#define idname(type) ((type) == GID ? "GID" : "UID")
#define subpath(type) ((type) == GID ? "/etc/subgid" : "/etc/subuid")

char *append(char **destination, const char *format, ...);
void createroot(char *src, int console, char *helper);
void denysetgroups(pid_t pid);
void enterroot(void);
int getconsole(void);
void mountproc(void);
void mountsys(void);
void seal(char **argv, char **envp);
void setconsole(char *name);
char *string(const char *format, ...);
int supervise(pid_t child, int console);
char *tmpdir(void);
void waitforstop(pid_t child);
void waitforexit(pid_t child);
void writemap(pid_t pid, int type, char *map);

#endif
