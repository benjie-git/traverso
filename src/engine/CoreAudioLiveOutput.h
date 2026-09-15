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

#ifndef COREAUDIOLIVEOUTPUT_H
#define COREAUDIOLIVEOUTPUT_H

#include "LiveOutput.h"

#include <AudioUnit/AudioUnit.h>
#include <CoreAudio/CoreAudio.h>

/**
 * CoreAudioLiveOutput streams the live mix to a second output device through
 * a dedicated output-only HAL AudioUnit, running on that device's own clock.
 */
class CoreAudioLiveOutput : public BufferedLiveOutput
{
public:
	CoreAudioLiveOutput();
	~CoreAudioLiveOutput() override;

	bool is_supported() const override { return true; }
	QString device_name() const override { return m_deviceName; }

	int open(const QString& uid, uint rate, nframes_t bufferSize, uint channels) override;
	void close() override;
	void start() override;
	void stop() override;

private:
	static OSStatus render_callback(void* inRefCon,
					AudioUnitRenderActionFlags* ioActionFlags,
					const AudioTimeStamp* inTimeStamp,
					UInt32 inBusNumber,
					UInt32 inNumberFrames,
					AudioBufferList* ioData);
	OSStatus render(UInt32 inNumberFrames, AudioBufferList* ioData);

	AudioUnit m_audioUnit;
	AudioDeviceID m_deviceId;
	QString m_deviceName;
};

#endif
