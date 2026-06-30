/* Nintendo Switch (Horizon/libnx) CPU/memory queries. Replaces the Linux
 * lib/sysdep/os/linux/lcpu.cpp (which uses cpu_set_t / sysfs, absent here). */

#include "precompiled.h"

#include "lib/sysdep/os_cpu.h"

#include <unistd.h>

#include <switch.h>

size_t os_cpu_NumProcessors()
{
	// Horizon exposes 3 application cores to homebrew (a 4th is reserved by the
	// OS). Our sysconf shim already reports this via _SC_NPROCESSORS_CONF.
	static size_t numProcessors;
	if (numProcessors == 0)
	{
		long res = sysconf(_SC_NPROCESSORS_CONF);
		numProcessors = (res > 0) ? (size_t)res : 3;
	}
	return numProcessors;
}

size_t os_cpu_PageSize()
{
	return 0x1000;	// 4 KiB
}

size_t os_cpu_LargePageSize()
{
	return 0;	// unsupported
}

size_t os_cpu_QueryMemorySize()
{
	u64 total = 0;
	if (R_FAILED(svcGetInfo(&total, InfoType_TotalMemorySize, CUR_PROCESS_HANDLE, 0)))
		return 2048;	// fallback (MiB)
	return (size_t)(total / (1024 * 1024));
}

size_t os_cpu_MemoryAvailable()
{
	u64 total = 0, used = 0;
	if (R_FAILED(svcGetInfo(&total, InfoType_TotalMemorySize, CUR_PROCESS_HANDLE, 0)) ||
	    R_FAILED(svcGetInfo(&used,  InfoType_UsedMemorySize,  CUR_PROCESS_HANDLE, 0)))
		return 1024;	// fallback (MiB)
	const u64 avail = (total > used) ? (total - used) : 0;
	return (size_t)(avail / (1024 * 1024));
}
