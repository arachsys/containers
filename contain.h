#ifndef CONTAIN_H
#define CONTAIN_H

#define GID 0
#define UID 1
#define INVALID ((unsigned) -1)

#define getid(type) ((unsigned) ((type) == GID ? getgid() : getuid()))
#define idfile(type) ((type) == GID ? "gid_map" : "uid_map")
#define idname(type) ((type) == GID ? "GID" : "UID")
#define subpath(type) ((type) == GID ? "/etc/subgid" : "/etc/subuid")

extern char *append(char **destination, const char *format, ...);
extern void createroot(char *src, char *console, char *helper);
extern int getconsole(void);
extern void mountproc(void);
extern void mountsys(void);
extern void setconsole(char *name);
extern char *string(const char *format, ...);
extern int supervise(pid_t child, int console);
extern char *tmpdir(void);
extern void waitforstop(pid_t child);
extern void waitforexit(pid_t child);
extern void writemap(pid_t pid, int type, char *map);

#endif
