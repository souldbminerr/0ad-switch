/* Nintendo Switch directory-watching stub. The Switch has no inotify/kqueue and
 * 0 A.D. only uses dir-watching for live mod hot-reloading (a dev convenience), so
 * these no-ops are sufficient. Replaces lib/sysdep/os/linux/dir_watch_inotify.cpp. */

#include "precompiled.h"

#include "lib/sysdep/dir_watch.h"

Status dir_watch_Add(const OsPath&, PDirWatch&)
{
	return INFO::OK;
}

Status dir_watch_Poll(DirWatchNotifications&)
{
	return INFO::OK;
}
