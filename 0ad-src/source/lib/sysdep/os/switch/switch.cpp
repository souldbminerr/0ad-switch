/* Nintendo Switch (Horizon/libnx) misc sysdep. Replaces the bits of
 * lib/sysdep/os/linux/linux.cpp the engine links against. */

#include "precompiled.h"

#include "lib/sysdep/sysdep.h"
#include "lib/sysdep/os/unix/unix_executable_pathname.h"

OsPath sys_ExecutablePathname()
{
	// dladdr is a no-op on Switch, so this is empty; path resolution falls back to
	// the working directory / explicit data roots set up at startup. (See M6.)
	return unix_ExecutablePathname();
}
