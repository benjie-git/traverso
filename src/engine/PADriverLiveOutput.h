/*
    Copyright (C) 2024

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

#ifndef PADRIVERLIVEOUTPUT_H
#define PADRIVERLIVEOUTPUT_H

#include "LiveOutput.h"

#include "portaudio.h"

/**
 * PADriverLiveOutput streams the live mix to a second output device through a
 * dedicated PortAudio stream, running on that device's own clock.
 */
class PADriverLiveOutput : public BufferedLiveOutput
{
public:
	PADriverLiveOutput();
	~PADriverLiveOutput() override;

	bool is_supported() const override { return true; }
	QString device_name() const override { return m_deviceName; }

	int open(const QString& uid, uint rate, nframes_t bufferSize, uint channels) override;
	void close() override;
	void start() override;
	void stop() override;

private:
	static PaDeviceIndex resolve_device(const QString& uid);

	static int process_callback(const void* inputBuffer,
				    void* outputBuffer,
				    unsigned long framesPerBuffer,
				    const PaStreamCallbackTimeInfo* timeInfo,
				    PaStreamCallbackFlags statusFlags,
				    void* arg);
	int render(unsigned long frames, audio_sample_t* out);

	PaStream* m_stream;
	QString m_deviceName;
};

#endif
