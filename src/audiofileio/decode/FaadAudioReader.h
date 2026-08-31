/*
Copyright (C) 2026 Traverso Team

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

#ifndef FAADAUDIOREADER_H
#define FAADAUDIOREADER_H

#include "AbstractAudioReader.h"

class FaadAudioReader : public AbstractAudioReader
{
public:
	FaadAudioReader(const QString& filename);
	~FaadAudioReader() override;

	QString decoder_type() const override { return "faad"; }
	void clear_buffers() override;

	static bool can_decode(const QString& filename);

protected:
	bool seek_private(nframes_t start) override;
	nframes_t read_private(DecodeBuffer* buffer, nframes_t frameCount) override;

	void create_buffers();
	bool initDecoderInternal();

	class FaadDecoderPrivate;
	FaadDecoderPrivate* d;
};

#endif // FAADAUDIOREADER_H
