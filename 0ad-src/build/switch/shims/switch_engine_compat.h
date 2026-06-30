#ifndef _SWITCH_ENGINE_COMPAT_H
#define _SWITCH_ENGINE_COMPAT_H
/* Force-included for the Switch engine build. Fills small libc gaps where newlib
   lacks a POSIX function 0 A.D. uses. Keep this minimal and well-justified. */
#ifdef __cplusplus
extern "C" {
#endif
#include <unistd.h>
#include <string.h>

/* newlib has getlogin() but not the _r reentrant variant. */
static inline int getlogin_r(char* buf, size_t bufsize) {
	const char* l = getlogin();
	if (!l) { if (bufsize) buf[0] = '\0'; return -1; }
	strncpy(buf, l, bufsize);
	if (bufsize) buf[bufsize - 1] = '\0';
	return 0;
}

#ifdef __cplusplus
}
#endif
#endif
