#ifndef _SWITCH_SHIM_SYS_UTSNAME_H
#define _SWITCH_SHIM_SYS_UTSNAME_H
/* newlib/libnx has no uname(); provide a minimal one so 0 A.D.'s HWDetect can
   report an OS string. Values are static (a real version could come from libnx
   setsysGetSystemVersion, but that's not needed just to identify the platform). */
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

#define _UTSNAME_LENGTH 65

struct utsname {
	char sysname[_UTSNAME_LENGTH];
	char nodename[_UTSNAME_LENGTH];
	char release[_UTSNAME_LENGTH];
	char version[_UTSNAME_LENGTH];
	char machine[_UTSNAME_LENGTH];
};

static inline int uname(struct utsname* buf) {
	if (!buf) return -1;
	strcpy(buf->sysname, "Horizon");
	strcpy(buf->nodename, "switch");
	strcpy(buf->release, "0");
	strcpy(buf->version, "0");
	strcpy(buf->machine, "aarch64");
	return 0;
}

#ifdef __cplusplus
}
#endif
#endif
