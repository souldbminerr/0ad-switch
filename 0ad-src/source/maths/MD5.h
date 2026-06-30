/* Copyright (C) 2025 Wildfire Games.
 * This file is part of 0 A.D.
 *
 * 0 A.D. is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * 0 A.D. is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with 0 A.D.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef INCLUDED_MD5
#define INCLUDED_MD5

#include "lib/code_annotation.h"
#include "lib/types.h"

#include <cstring>

/**
 * MD5 hashing algorithm. Note that MD5 is broken and must not be used for
 * anything that requires security.
 */
class MD5
{
public:
	static const size_t DIGESTSIZE = 16;

	MD5();

	void Update(const u8* data, size_t len)
	{
		// (Defined inline for efficiency in the common fixed-length fits-in-buffer case)

		const size_t CHUNK_SIZE = sizeof(m_Buf);

		m_InputLen += len;

		// GCC thinks `m_Buf + m_BufLen` can be outside the bound of `m_Buf`. That is because
		// `m_BufLen + len < CHUNK_SIZE` can evaluate to `true` even if `m_BufLen` is bigger than
		// `CHUNK_SIZE` due to wrapping. Tell GCC that it's not possible.
		if (m_BufLen >= CHUNK_SIZE)
			UNREACHABLE;

		// If we have enough space in m_Buf and won't flush, simply append the input
		if (m_BufLen + len < CHUNK_SIZE)
		{
			memcpy(m_Buf + m_BufLen, data, len);
			m_BufLen += len;
			return;
		}

		// Fall back to non-inline function if we have to do more work
		UpdateRest(data, len);
	}

	void Final(u8* digest);

private:
	void InitState();
	void UpdateRest(const u8* data, size_t len);
	void Transform(const u32* in);
	u32 m_Digest[4]; // internal state
	u8 m_Buf[64]; // buffered input bytes
	size_t m_BufLen; // bytes in m_Buf that are valid
	u64 m_InputLen; // bytes
};

#endif // INCLUDED_MD5
