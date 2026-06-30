/* Copyright (C) 2025 Wildfire Games.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

/*
 * Bring in gloox header+library, with compatibility fixes.
 */

#ifndef INCLUDED_GLOOX
#define INCLUDED_GLOOX

// Needs NOMINMAX or including windows.h breaks c++ standard library. Include
// our version which has extra fixes.
#include "lib/sysdep/os.h"
#if OS_WIN
#include  "lib/sysdep/os/win/win.h"
#endif

// Conflicts with mozjs
#pragma push_macro("lookup")
#undef lookup

#include <gloox/client.h> // IWYU pragma: export
#include <gloox/connectionlistener.h> // IWYU pragma: export
#include <gloox/delayeddelivery.h> // IWYU pragma: export
#include <gloox/disco.h> // IWYU pragma: export
#include <gloox/error.h> // IWYU pragma: export
#include <gloox/gloox.h> // IWYU pragma: export
#include <gloox/glooxversion.h> // IWYU pragma: export
#include <gloox/iq.h> // IWYU pragma: export
#include <gloox/iqhandler.h> // IWYU pragma: export
#include <gloox/jid.h> // IWYU pragma: export
#include <gloox/jinglecontent.h> // IWYU pragma: export
#include <gloox/jingleiceudp.h> // IWYU pragma: export
#include <gloox/jingleplugin.h> // IWYU pragma: export
#include <gloox/jinglesession.h> // IWYU pragma: export
#include <gloox/jinglesessionhandler.h> // IWYU pragma: export
#include <gloox/jinglesessionmanager.h> // IWYU pragma: export
#include <gloox/loghandler.h> // IWYU pragma: export
#include <gloox/message.h> // IWYU pragma: export
#include <gloox/messagehandler.h> // IWYU pragma: export
#include <gloox/mucroom.h> // IWYU pragma: export
#include <gloox/mucroomhandler.h> // IWYU pragma: export
#include <gloox/presence.h> // IWYU pragma: export
#include <gloox/registration.h> // IWYU pragma: export
#include <gloox/registrationhandler.h> // IWYU pragma: export
#include <gloox/stanzaextension.h> // IWYU pragma: export
#include <gloox/tag.h> // IWYU pragma: export
#include <gloox/util.h> // IWYU pragma: export

#pragma pop_macro("lookup")

#endif	// #ifndef INCLUDED_GLOOX
