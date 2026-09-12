/*
Copyright (C) 2026 Ben Levitt

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

#ifndef COREAUDIODRIVER_H
#define COREAUDIODRIVER_H

#include "TAudioDriver.h"
#include "RingBufferNPT.h"

#include <AudioUnit/AudioUnit.h>
#include <CoreAudio/CoreAudio.h>

#include <memory>

class CoreAudioDriver : public TAudioDriver
{
    Q_OBJECT

public:
    explicit CoreAudioDriver(AudioDevice* device, int rate, nframes_t bufferSize);
    ~CoreAudioDriver() override;

    int _read(nframes_t nframes) override;
    int _write(nframes_t nframes) override;
    int _run_cycle() override { return 1; }
    int setup(bool capture = true, bool playback = true, const QString& cardDevice = "none");
    int attach() override;
    int start() override;
    int stop() override;

    QString get_device_name() override;
    QString get_device_longname() override;
    static QStringList devices_info(bool input);

private:
    AudioUnit m_audioUnit{};
    AudioUnit m_inputAudioUnit{};
    AudioBufferList* m_inputList{};
    audio_sample_t* m_inputBuffer{};
    audio_sample_t* m_processInputBuffer{};
    std::unique_ptr<RingBufferNPT<audio_sample_t>> m_inputRingBuffer;
    AudioDeviceID m_deviceId{kAudioDeviceUnknown};
    AudioDeviceID m_inputDeviceId{kAudioDeviceUnknown};
    channel_t m_inputChannels{0};
    channel_t m_outputChannels{0};
    bool m_capture{false};
    bool m_playback{false};
    bool m_running{false};
    OSStatus m_lastInputRenderStatus{noErr};
    OSStatus m_reportedInputRenderStatus{noErr};

    int process_callback(AudioUnitRenderActionFlags* flags,
                         const AudioTimeStamp* timestamp,
                         nframes_t nframes,
                         AudioBufferList* output);
    OSStatus capture_callback(AudioUnitRenderActionFlags* flags,
                              const AudioTimeStamp* timestamp,
                              nframes_t nframes);
    int fail_setup(const QString& message, OSStatus status = noErr);

    static OSStatus render_callback(void* refCon,
                                    AudioUnitRenderActionFlags* flags,
                                    const AudioTimeStamp* timestamp,
                                    UInt32 bus,
                                    UInt32 frames,
                                    AudioBufferList* output);
    static OSStatus input_render_callback(void* refCon,
                                          AudioUnitRenderActionFlags* flags,
                                          const AudioTimeStamp* timestamp,
                                          UInt32 bus,
                                          UInt32 frames,
                                          AudioBufferList* output);
};

#endif
