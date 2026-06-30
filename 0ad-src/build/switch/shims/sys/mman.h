#ifndef _SWITCH_SHIM_SYS_MMAN_H
#define _SWITCH_SHIM_SYS_MMAN_H

#include <stddef.h>

#define PROT_NONE 0x0
#define PROT_READ 0x1
#define PROT_WRITE 0x2
#define PROT_EXEC 0x4

#define MAP_SHARED 0x01
#define MAP_PRIVATE 0x02
#define MAP_FIXED 0x10
#define MAP_ANONYMOUS 0x20
#define MAP_ANON MAP_ANONYMOUS
#define MAP_NORESERVE 0x4000
#define MAP_FAILED ((void *)-1)

#define MADV_NORMAL 0
#define MADV_DONTNEED 4
#define MADV_FREE 8

#define MREMAP_MAYMOVE 1
#define MREMAP_FIXED 2

#ifdef __cplusplus
extern "C" {
#endif

void *mmap(void *addr, size_t length, int prot, int flags, int fd, long offset);
int munmap(void *addr, size_t length);
int mprotect(void *addr, size_t len, int prot);
int madvise(void *addr, size_t length, int advice);
void *mremap(void *old_addr, size_t old_size, size_t new_size, int flags, ...);

#ifdef __cplusplus
}
#endif

#endif
