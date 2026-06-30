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
#include "precompiled.h"

#include "ogg.h"

#if CONFIG2_AUDIO

#include "lib/byte_order.h"
#include "lib/code_annotation.h"
#include "lib/debug.h"
#include "lib/file/io/io.h"
#include "lib/posix/posix_types.h"
#include "maths/MathUtil.h"
#include "ps/CLogger.h"

#include <AL/al.h>
#include <algorithm>
#include <fcntl.h>
#include <fmt/format.h>
#include <iterator>
#include <ogg/config_types.h>
#include <stdexcept>
#include <vorbis/codec.h>
#include <vorbis/vorbisfile.h>

static Status LibErrorFromVorbis(int err)
{
	switch(err)
	{
	case 0:
		return INFO::OK;
	case OV_HOLE:
		return ERR::AGAIN;
	case OV_EREAD:
		return ERR::IO;
	case OV_EFAULT:
		return ERR::LOGIC;
	case OV_EIMPL:
		return ERR::NOT_SUPPORTED;
	case OV_EINVAL:
		return ERR::INVALID_PARAM;
	case OV_ENOTVORBIS:
		return ERR::NOT_SUPPORTED;
	case OV_EBADHEADER:
		return ERR::CORRUPTED;
	case OV_EVERSION:
		return ERR::INVALID_VERSION;
	case OV_ENOTAUDIO:
		return ERR::_1;
	case OV_EBADPACKET:
		return ERR::_2;
	case OV_EBADLINK:
		return ERR::_3;
	case OV_ENOSEEK:
		return ERR::_4;
	default:
		return ERR::FAIL;
	}
}

//-----------------------------------------------------------------------------

class VorbisBufferAdapter
{
public:
	VorbisBufferAdapter(const std::shared_ptr<u8>& buffer, size_t size)
		: m_Buffer(buffer)
		, m_Size(size)
		, m_Offset(0)
	{
	}

	static size_t Read(void* bufferToFill, size_t itemSize, size_t numItems, void* context)
	{
		VorbisBufferAdapter* adapter{static_cast<VorbisBufferAdapter*>(context)};

		const off_t sizeRequested{static_cast<off_t>(numItems * itemSize)};
		const off_t sizeRemaining{adapter->m_Size - adapter->m_Offset};
		const size_t sizeToRead{static_cast<size_t>(std::min(sizeRequested, sizeRemaining))};

		std::copy_n(
			adapter->m_Buffer.get() + adapter->m_Offset,
			sizeToRead,
			static_cast<u8*>(bufferToFill)
		);

		adapter->m_Offset += sizeToRead;
		return sizeToRead;
	}

	static int Seek(void* context, ogg_int64_t offset, int whence)
	{
		VorbisBufferAdapter* adapter{static_cast<VorbisBufferAdapter*>(context)};

		off_t origin{0};
		switch(whence)
		{
		case SEEK_SET:
			origin = 0;
			break;
		case SEEK_CUR:
			origin = adapter->m_Offset;
			break;
		case SEEK_END:
			origin = adapter->m_Size + 1;
			break;
			NODEFAULT;
		}

		adapter->m_Offset = Clamp(static_cast<off_t>(origin + offset), static_cast<off_t>(0), adapter->m_Size);
		return 0;
	}

	static int Close(void* context)
	{
		VorbisBufferAdapter* adapter{static_cast<VorbisBufferAdapter*>(context)};
		adapter->m_Buffer.reset();
		return 0;	// return value is ignored
	}

	static long Tell(void* context)
	{
		VorbisBufferAdapter* adapter{static_cast<VorbisBufferAdapter*>(context)};
		return adapter->m_Offset;
	}

private:
	std::shared_ptr<u8> m_Buffer;
	off_t m_Size;
	off_t m_Offset;
};


//-----------------------------------------------------------------------------

template <typename Adapter>
class OggStreamImpl : public OggStream
{
public:
	OggStreamImpl(const Adapter& adapter)
		: m_Adapter(adapter),
		m_FileEOF(false),
		m_Info(nullptr)
	{
		Open();
	}

	~OggStreamImpl()
	{
		ov_clear(&m_VorbisFile);
	}

	virtual ALenum Format()
	{
		return (m_Info->channels == 1)? AL_FORMAT_MONO16 : AL_FORMAT_STEREO16;
	}

	virtual ALsizei SamplingRate()
	{
		return m_Info->rate;
	}
	virtual bool AtFileEOF()
	{
		return m_FileEOF;
	}

	virtual Status ResetFile()
	{
	    ov_time_seek(&m_VorbisFile, 0);
	    m_FileEOF = false;
		return INFO::OK;
	}

	virtual size_t GetNextChunk(std::span<u8> buffer)
	{
		// We may have to call ov_read multiple times because it
		// treats the buffer size "as a limit and not a request".
		size_t bytesRead{0};
		while (bytesRead < buffer.size())
		{
			constexpr int isBigEndian{(BYTE_ORDER == BIG_ENDIAN)};
			constexpr int wordSize{sizeof(i16)};
			constexpr int isSigned{1};
			// Unused.
			int bitstream;
			const int ret{static_cast<int>(ov_read(
				&m_VorbisFile,
				reinterpret_cast<char*>(buffer.data() + bytesRead),
				static_cast<int>(buffer.size() - bytesRead),
				isBigEndian,
				wordSize,
				isSigned,
				&bitstream
			))};
			if(ret == 0) {
				m_FileEOF = true;
				return bytesRead;
			}

			if(ret < 0)
				WARN_RETURN(LibErrorFromVorbis(ret));

			bytesRead += ret;
		}
		return bytesRead;
	}

private:
	void Open()
	{
		ov_callbacks callbacks;
		callbacks.read_func = Adapter::Read;
		callbacks.close_func = Adapter::Close;
		callbacks.seek_func = Adapter::Seek;
		callbacks.tell_func = Adapter::Tell;
		const int ret{ov_open_callbacks(&m_Adapter, &m_VorbisFile, 0, 0, callbacks)};
		switch (ret)
		{
			case 0:
				break;
			case OV_EBADHEADER:
			case OV_EREAD:
			case OV_ENOTVORBIS:
			case OV_EVERSION:
				throw std::runtime_error(fmt::format("Failed to open Ogg Vorbis file: {}", LibErrorFromVorbis(ret)));
			default:
				throw std::runtime_error(fmt::format("Unknown error opening Ogg Vorbis file: {}", LibErrorFromVorbis(ret)));
		}

		// Retrieve info for current bitstream.
		const int link{-1};
		m_Info = ov_info(&m_VorbisFile, link);
		if(!m_Info)
			DEBUG_WARN_ERR(ERR::INVALID_HANDLE);
	}

	Adapter m_Adapter;
	OggVorbis_File m_VorbisFile;
	vorbis_info* m_Info;
	bool m_FileEOF;
};


//-----------------------------------------------------------------------------

Status OpenOggNonstream(const PIVFS& vfs, const VfsPath& pathname, OggStreamPtr& stream)
{
	std::shared_ptr<u8> contents;
	size_t size;
	RETURN_STATUS_IF_ERR(vfs->LoadFile(pathname, contents, size));

	try
	{
		std::shared_ptr<OggStreamImpl<VorbisBufferAdapter>> tmp{std::make_shared<OggStreamImpl<VorbisBufferAdapter>>(VorbisBufferAdapter(contents, size))};
		stream = tmp;
		return INFO::OK;
	}
	catch(const std::runtime_error& e)
	{
		LOGERROR(e.what());
		return ERR::IO;
	}
}

#endif	// CONFIG2_AUDIO

