#ifndef _SWITCH_SHIM_DLFCN_H
#define _SWITCH_SHIM_DLFCN_H

#define RTLD_LAZY 0x1
#define RTLD_NOW 0x2
#define RTLD_LOCAL 0x0
#define RTLD_GLOBAL 0x100
#define RTLD_DEFAULT ((void *)0)
#define RTLD_NEXT ((void *)-1)

#ifdef __cplusplus
extern "C" {
#endif

static inline void *dlopen(const char *f, int flag) {
  (void)f;
  (void)flag;
  return (void *)0;
}
static inline int dlclose(void *h) {
  (void)h;
  return 0;
}
static inline void *dlsym(void *h, const char *s) {
  (void)h;
  (void)s;
  return (void *)0;
}
static inline char *dlerror(void) {
  return (char *)"dlfcn not supported on Switch";
}

typedef struct {
  const char *dli_fname;
  void *dli_fbase;
  const char *dli_sname;
  void *dli_saddr;
} Dl_info;

/* No dynamic loader on Switch homebrew; dladdr always fails so callers
   (e.g. unix_executable_pathname) fall back to other path resolution. */
static inline int dladdr(const void *addr, Dl_info *info) {
  (void)addr;
  (void)info;
  return 0;
}

#ifdef __cplusplus
}
#endif

#endif
