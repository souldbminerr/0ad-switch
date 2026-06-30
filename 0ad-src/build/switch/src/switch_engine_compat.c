#include <errno.h>
#include <iconv.h>
#include <pwd.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

iconv_t iconv_open(const char *to, const char *from) {
  (void)to;
  (void)from;
  return (iconv_t)1;
}

size_t iconv(iconv_t cd, char **inbuf, size_t *inbytesleft, char **outbuf,
             size_t *outbytesleft) {
  (void)cd;
  if (!inbuf || !*inbuf)
    return 0;
  size_t n = (*inbytesleft < *outbytesleft) ? *inbytesleft : *outbytesleft;
  memcpy(*outbuf, *inbuf, n);
  *inbuf += n;
  *inbytesleft -= n;
  *outbuf += n;
  *outbytesleft -= n;
  if (*inbytesleft) {
    errno = E2BIG;
    return (size_t)-1;
  }
  return 0;
}

int iconv_close(iconv_t cd) {
  (void)cd;
  return 0;
}

FILE *popen(const char *command, const char *type) {
  (void)command;
  (void)type;
  errno = ENOSYS;
  return NULL;
}
int pclose(FILE *stream) {
  (void)stream;
  errno = ENOSYS;
  return -1;
}
struct passwd *getpwnam(const char *name) {
  (void)name;
  errno = ENOSYS;
  return NULL;
}
int setgid(gid_t gid) {
  (void)gid;
  errno = ENOSYS;
  return -1;
}
int setuid(uid_t uid) {
  (void)uid;
  errno = ENOSYS;
  return -1;
}

int execlp(const char *file, const char *arg, ...) {
  (void)file;
  (void)arg;
  errno = ENOSYS;
  return -1;
}
int execv(const char *path, char *const argv[]) {
  (void)path;
  (void)argv;
  errno = ENOSYS;
  return -1;
}
pid_t waitpid(pid_t pid, int *status, int options) {
  (void)pid;
  (void)status;
  (void)options;
  errno = ENOSYS;
  return -1;
}
uid_t geteuid(void) { return 0; }

char *getlogin(void) {
  static char name[] = "switch";
  return name;
}
