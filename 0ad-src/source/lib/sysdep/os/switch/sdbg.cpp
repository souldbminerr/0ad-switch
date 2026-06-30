/* Nintendo Switch debug-symbol stubs. devkitPro newlib has no execinfo/backtrace
 * and homebrew has no runtime symbol resolution, so stack dumps are empty (use a
 * host debugger / crash report PC instead). Replaces lib/sysdep/os/linux/ldbg.cpp. */

#include "precompiled.h"

#include "lib/sysdep/sysdep.h"
#include "lib/debug.h"

void* debug_GetCaller(void* /*context*/, const wchar_t* /*lastFuncToSkip*/)
{
	return nullptr;
}

Status debug_DumpStack(wchar_t* buf, size_t max_chars, void* /*context*/, const wchar_t* /*lastFuncToSkip*/)
{
	if (buf && max_chars)
		buf[0] = L'\0';
	return INFO::OK;
}

Status debug_ResolveSymbol(void* /*ptr_of_interest*/, wchar_t* sym_name, wchar_t* file, int* line)
{
	if (sym_name)
		sym_name[0] = L'\0';
	if (file)
		file[0] = L'\0';
	if (line)
		*line = 0;
	return ERR::FAIL;
}

void debug_SetThreadName(char const* /*name*/)
{
	// Horizon thread names are set at creation; nothing to do here.
}
