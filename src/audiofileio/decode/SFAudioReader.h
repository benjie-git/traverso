/*
Copyright (C) 2007 Ben Levitt 

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

#ifndef SFAUDIOREADER_H
#define SFAUDIOREADER_H

#include <AbstractAudioReader.h>
#include "sndfile.h"

#include <QFile>

class SFAudioReader : public AbstractAudioReader
{
public:
	SFAudioReader(const QString &filename);
	~SFAudioReader();
	
	QString decoder_type() const {return "sndfile";}
	
	static bool can_decode(QString filename);

protected:
	bool seek_private(nframes_t start);
	nframes_t read_private(DecodeBuffer* buffer, nframes_t frameCount);
	
	SNDFILE*	m_sf;
	SF_INFO		m_sfinfo{};
	
private:
	QFile m_file;
};

#endif
