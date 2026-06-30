#ifndef _SWITCH_SHIM_SYS_SYSCALL_H
#define _SWITCH_SHIM_SYS_SYSCALL_H

#include <errno.h>
#include <stdarg.h>
#include <stddef.h>
#include <sys/types.h>


#define __NR_gettid 178
#define __NR_getrandom 278
#define SYS_gettid __NR_gettid
#define SYS_getrandom __NR_getrandom

#ifdef __cplusplus
extern "C" {
#endif

extern void randomGet(void *buf, size_t len);
unsigned int threadGetCurHandle(void);

static inline long syscall(long number, ...) {
  va_list ap;
  va_start(ap, number);
  long r;
  switch (number) {
  case __NR_gettid:
    r = (long)threadGetCurHandle();
    break;
  case __NR_getrandom: {
    void *buf = va_arg(ap, void *);
    size_t len = va_arg(ap, size_t);
    randomGet(buf, len);
    r = (long)len;
    break;
  }
  default:
    errno = ENOSYS;
    r = -1;
  }
  va_end(ap);
  return r;
}

#ifdef __cplusplus
}
#endif

#endif
