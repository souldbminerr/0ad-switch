#ifndef _SWITCH_SHIM_AIO_H
#define _SWITCH_SHIM_AIO_H
/* Nintendo Switch (newlib) has no POSIX async I/O. Provide the surface 0 A.D.'s
   lib/posix/posix_aio.h expects; calls fail with ENOSYS so the engine falls back
   to synchronous I/O. */
#include <sys/types.h>
#include <signal.h>
#include <errno.h>

/* newlib forward-declares sigevent but leaves it incomplete; aio is stubbed so a
   minimal concrete definition is enough to satisfy the struct layout. */
#ifndef SIGEV_NONE
#define SIGEV_NONE 1
struct sigevent { int sigev_notify; int sigev_signo; union sigval { int sival_int; void *sival_ptr; } sigev_value; };
#endif

#ifdef __cplusplus
extern "C" {
#endif

struct aiocb {
	int             aio_fildes;
	off_t           aio_offset;
	volatile void  *aio_buf;
	size_t          aio_nbytes;
	int             aio_reqprio;
	struct sigevent aio_sigevent;
	int             aio_lio_opcode;
};

#define LIO_READ   0
#define LIO_WRITE  1
#define LIO_NOP    2
#define LIO_WAIT   0
#define LIO_NOWAIT 1

static inline int aio_read(struct aiocb *a)            { (void)a; errno = ENOSYS; return -1; }
static inline int aio_write(struct aiocb *a)           { (void)a; errno = ENOSYS; return -1; }
static inline int aio_error(const struct aiocb *a)     { (void)a; return ENOSYS; }
static inline ssize_t aio_return(struct aiocb *a)      { (void)a; errno = ENOSYS; return -1; }
static inline int aio_cancel(int fd, struct aiocb *a)  { (void)fd; (void)a; errno = ENOSYS; return -1; }
static inline int aio_suspend(const struct aiocb *const l[], int n, const struct timespec *t)
                                                       { (void)l; (void)n; (void)t; errno = ENOSYS; return -1; }
static inline int lio_listio(int m, struct aiocb *const l[], int n, struct sigevent *s)
                                                       { (void)m; (void)l; (void)n; (void)s; errno = ENOSYS; return -1; }
#ifdef __cplusplus
}
#endif
#endif
