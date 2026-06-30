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

#ifndef INCLUDED_OGGDATA_H
#define INCLUDED_OGGDATA_H

#include "lib/config2.h"

#if CONFIG2_AUDIO

#include "ogg.h"
#include "SoundData.h"

#include "lib/file/vfs/vfs_path.h"

#include <AL/al.h>
#include <array>

/*
* 50 buffers of 98304 bytes each gives us 4.9 seconds of audio, which is a good amount to have buffered at once.
*/
constexpr int OGG_DEFAULT_BUFFER_COUNT = 50;

class COggData final : public CSoundData
{
public:
	COggData();
	~COggData();

	bool InitOggFile(const VfsPath& itemPath);
	bool IsFileFinished();
	bool IsOneShot() override;
	bool IsStereo() override;

	int FetchDataIntoBuffer(int count, ALuint* buffers);
	void ResetFile();

private:
	ALuint m_Format;
	ALsizei m_Frequency;
protected:
	OggStreamPtr m_Stream;
	bool m_FileFinished;
	bool m_OneShot;
	std::array<ALuint, OGG_DEFAULT_BUFFER_COUNT> m_Buffer{};
	int m_BuffersCount;

	void SetFormatAndFreq(ALenum form, ALsizei freq);
	int GetBufferCount() override;
	unsigned int GetBuffer() override;
	unsigned int* GetBufferPtr() override;
};

#endif // CONFIG2_AUDIO
#endif // INCLUDED_OGGDATA_H
