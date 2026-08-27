/*
Copyright (C) 2026 Ben Levitt
Copyright (C) 2026 Traverso Authors

This file is part of Traverso

Traverso is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA.

*/

#ifndef FAACAUDIOWRITER_H
#define FAACAUDIOWRITER_H

#include "AbstractAudioWriter.h"
#include "defines.h"

#include <stdio.h>

class QString;

class FaacAudioWriter : public AbstractAudioWriter
{
public:
	FaacAudioWriter();
	~FaacAudioWriter();

	bool set_format_attribute(const QString& key, const QString& value);
	const char* get_extension();

protected:
	bool open_private();
	nframes_t write_private(void* buffer, nframes_t frameCount);
	bool close_private();

	struct FaacInfo;
	FaacInfo* m_faacInfo;

	char*	m_buffer;
	long	m_bufferSize;
	FILE*	m_fid;

	int	m_bitrate;	// bitrate in kbps (e.g. 192)
	int	m_quality;	// quality/quantizer setting (default 100)
	bool	m_useQuality;	// true if quality mode, false if bitrate mode
	int	m_objectType;	// AAC object type: 2=LOW (AAC-LC)
	int	m_useTns;	// temporal noise shaping
	int	m_useMidside;	// mid-side stereo
};

#endif
