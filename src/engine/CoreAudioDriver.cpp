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

#include "CoreAudioDriver.h"

#include "AudioChannel.h"
#include "AudioDevice.h"
#include "defines.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <QUrl>

namespace {

OSStatus property_data(AudioObjectID object, AudioObjectPropertySelector selector,
                       AudioObjectPropertyScope scope, void* value, UInt32* size)
{
    AudioObjectPropertyAddress address{selector, scope, kAudioObjectPropertyElementMain};
    return AudioObjectGetPropertyData(object, &address, 0, nullptr, size, value);
}

OSStatus channel_count(AudioDeviceID device, AudioObjectPropertyScope scope, UInt32* count)
{
    AudioObjectPropertyAddress address{kAudioDevicePropertyStreamConfiguration, scope,
                                       kAudioObjectPropertyElementMain};
    UInt32 size = 0;
    OSStatus status = AudioObjectGetPropertyDataSize(device, &address, 0, nullptr, &size);
    if (status != noErr) {
        return status;
    }

    std::unique_ptr<AudioBufferList, decltype(&std::free)> buffers(
        static_cast<AudioBufferList*>(std::malloc(size)), &std::free);
    if (!buffers) {
        return kAudioHardwareUnspecifiedError;
    }
    status = AudioObjectGetPropertyData(device, &address, 0, nullptr, &size, buffers.get());
    if (status != noErr) {
        return status;
    }

    *count = 0;
    for (UInt32 index = 0; index < buffers->mNumberBuffers; ++index) {
        *count += buffers->mBuffers[index].mNumberChannels;
    }
    return noErr;
}

AudioDeviceID default_device(bool captureOnly)
{
    AudioDeviceID device = kAudioDeviceUnknown;
    UInt32 size = sizeof(device);
    property_data(kAudioObjectSystemObject,
                  captureOnly ? kAudioHardwarePropertyDefaultInputDevice
                              : kAudioHardwarePropertyDefaultOutputDevice,
                  kAudioObjectPropertyScopeGlobal, &device, &size);
    return device;
}

AudioDeviceID duplex_device()
{
    AudioObjectPropertyAddress address{kAudioHardwarePropertyDevices,
                                       kAudioObjectPropertyScopeGlobal,
                                       kAudioObjectPropertyElementMain};
    UInt32 size = 0;
    if (AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &address, 0, nullptr, &size) != noErr) {
        return kAudioDeviceUnknown;
    }

    const UInt32 count = size / sizeof(AudioDeviceID);
    std::unique_ptr<AudioDeviceID[]> devices(new AudioDeviceID[count]);
    if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &address, 0, nullptr, &size,
                                   devices.get()) != noErr) {
        return kAudioDeviceUnknown;
    }

    for (UInt32 index = 0; index < count; ++index) {
        UInt32 inputs = 0;
        UInt32 outputs = 0;
        if (channel_count(devices[index], kAudioObjectPropertyScopeInput, &inputs) == noErr &&
            channel_count(devices[index], kAudioObjectPropertyScopeOutput, &outputs) == noErr &&
            inputs > 0 && outputs > 0) {
            return devices[index];
        }
    }
    return kAudioDeviceUnknown;
}

OSStatus device_for_uid(const QString& uid, AudioDeviceID* device)
{
    const QByteArray uidBytes = uid.toUtf8();
    if (uidBytes.isEmpty() || !device) {
        return kAudioHardwareBadDeviceError;
    }
    CFStringRef value = CFStringCreateWithCString(kCFAllocatorDefault, uidBytes.constData(),
                                                   kCFStringEncodingUTF8);
    if (!value) {
        return kAudioHardwareUnspecifiedError;
    }

    AudioValueTranslation translation{
        &value,
        sizeof(value),
        device,
        sizeof(*device)
    };
    UInt32 size = sizeof(translation);
    AudioObjectPropertyAddress address{kAudioHardwarePropertyDeviceForUID,
                                       kAudioObjectPropertyScopeGlobal,
                                       kAudioObjectPropertyElementMain};
    OSStatus status = AudioObjectGetPropertyData(kAudioObjectSystemObject, &address,
                                                  0, nullptr, &size, &translation);
    CFRelease(value);
    if (status == noErr && *device == kAudioDeviceUnknown) {
        return kAudioHardwareBadDeviceError;
    }
    return status;
}

AudioStreamBasicDescription client_format(double sampleRate, UInt32 channels)
{
    AudioStreamBasicDescription format{};
    format.mSampleRate = sampleRate;
    format.mFormatID = kAudioFormatLinearPCM;
    format.mFormatFlags = kAudioFormatFlagsNativeFloatPacked;
    format.mBytesPerPacket = sizeof(audio_sample_t) * channels;
    format.mFramesPerPacket = 1;
    format.mBytesPerFrame = sizeof(audio_sample_t) * channels;
    format.mChannelsPerFrame = channels;
    format.mBitsPerChannel = sizeof(audio_sample_t) * 8;
    return format;
}

QString device_name(AudioDeviceID device)
{
    CFStringRef name = nullptr;
    UInt32 size = sizeof(name);
    if (property_data(device, kAudioObjectPropertyName, kAudioObjectPropertyScopeGlobal,
                      &name, &size) != noErr || !name) {
        return QStringLiteral("CoreAudio");
    }

    char buffer[256]{};
    QString result = CFStringGetCString(name, buffer, sizeof(buffer), kCFStringEncodingUTF8)
                         ? QString::fromUtf8(buffer)
                         : QStringLiteral("CoreAudio");
    CFRelease(name);
    return result;
}

QString device_uid(AudioDeviceID device)
{
    CFStringRef uid = nullptr;
    UInt32 size = sizeof(uid);
    if (property_data(device, kAudioDevicePropertyDeviceUID, kAudioObjectPropertyScopeGlobal,
                      &uid, &size) != noErr || !uid) {
        return {};
    }
    char buffer[512]{};
    QString result = CFStringGetCString(uid, buffer, sizeof(buffer), kCFStringEncodingUTF8)
                         ? QString::fromUtf8(buffer)
                         : QString();
    CFRelease(uid);
    return result;
}

} // namespace

CoreAudioDriver::CoreAudioDriver(AudioDevice* device, int rate, nframes_t bufferSize)
    : TAudioDriver(device, rate, bufferSize)
{
    read = MakeDelegate(this, &CoreAudioDriver::_read);
    write = MakeDelegate(this, &CoreAudioDriver::_write);
    run_cycle = RunCycleCallback(this, &CoreAudioDriver::_run_cycle);
}

CoreAudioDriver::~CoreAudioDriver()
{
    stop();
    std::free(m_inputBuffer);
    m_inputBuffer = nullptr;
    std::free(m_processInputBuffer);
    m_processInputBuffer = nullptr;
    std::free(m_inputList);
    m_inputList = nullptr;
    if (m_audioUnit) {
        AudioUnitUninitialize(m_audioUnit);
        AudioComponentInstanceDispose(m_audioUnit);
        m_audioUnit = nullptr;
    }
    if (m_inputAudioUnit) {
        AudioUnitUninitialize(m_inputAudioUnit);
        AudioComponentInstanceDispose(m_inputAudioUnit);
        m_inputAudioUnit = nullptr;
    }
}

int CoreAudioDriver::fail_setup(const QString& message, OSStatus status)
{
    const QString detail = status == noErr
                               ? message
                               : tr("%1 (CoreAudio status %2)").arg(message).arg(status);
    device->driverSetupMessage(detail, AudioDevice::DRIVER_SETUP_FAILURE);
    return -1;
}

int CoreAudioDriver::setup(bool capture, bool playback, const QString& cardDevice)
{
    m_capture = capture;
    m_playback = playback;

    m_deviceId = default_device(capture && !playback);
    m_inputDeviceId = default_device(true);
    const QStringList selectedDevices = cardDevice.split(QStringLiteral("::"), Qt::KeepEmptyParts);
    const bool hasSeparateSelections = capture && playback && selectedDevices.size() == 2;
    if (hasSeparateSelections) {
        const QString inputUid = selectedDevices.at(0) == QStringLiteral("default")
                                     ? QStringLiteral("default")
                                     : QUrl::fromPercentEncoding(selectedDevices.at(0).toUtf8());
        const QString outputUid = selectedDevices.at(1) == QStringLiteral("default")
                                      ? QStringLiteral("default")
                                      : QUrl::fromPercentEncoding(selectedDevices.at(1).toUtf8());
        AudioDeviceID selectedInput = m_inputDeviceId;
        AudioDeviceID selectedOutput = m_deviceId;
        if (inputUid != QStringLiteral("default") &&
            device_for_uid(inputUid, &selectedInput) != noErr) {
            return fail_setup(tr("Could not find the selected CoreAudio input device"));
        }
        if (outputUid != QStringLiteral("default") &&
            device_for_uid(outputUid, &selectedOutput) != noErr) {
            return fail_setup(tr("Could not find the selected CoreAudio output device"));
        }
        if (selectedInput == kAudioDeviceUnknown || selectedOutput == kAudioDeviceUnknown) {
            return fail_setup(tr("The selected CoreAudio device is unavailable"));
        }
        m_inputDeviceId = selectedInput;
        m_deviceId = selectedOutput;
    }
    if (!cardDevice.isEmpty() && cardDevice != QStringLiteral("none") &&
        cardDevice != QStringLiteral("default") && !hasSeparateSelections) {
        AudioDeviceID selectedDevice = kAudioDeviceUnknown;
        if (device_for_uid(cardDevice, &selectedDevice) != noErr ||
            selectedDevice == kAudioDeviceUnknown) {
            return fail_setup(tr("Could not find CoreAudio device %1").arg(cardDevice));
        }
        if (capture && !playback) {
            m_inputDeviceId = selectedDevice;
            m_deviceId = selectedDevice;
        } else if (playback && !capture) {
            m_deviceId = selectedDevice;
        } else {
            m_deviceId = selectedDevice;
            m_inputDeviceId = selectedDevice;
        }
    }
    if (capture && !playback) {
        m_deviceId = m_inputDeviceId;
    }
    if (m_deviceId == kAudioDeviceUnknown) {
        return fail_setup(tr("No default CoreAudio device is available"));
    }
    if (capture && m_inputDeviceId == kAudioDeviceUnknown) {
        return fail_setup(tr("No default CoreAudio input device is available"));
    }

    const bool separateDevices = capture && playback && (m_inputDeviceId != m_deviceId);

    AudioObjectPropertyAddress rateAddress{kAudioDevicePropertyNominalSampleRate,
                                           kAudioObjectPropertyScopeGlobal,
                                           kAudioObjectPropertyElementMain};
    Float64 sampleRate = frame_rate;
    UInt32 rateSize = sizeof(sampleRate);
    OSStatus status = AudioObjectSetPropertyData(m_deviceId, &rateAddress, 0, nullptr,
                                                  rateSize, &sampleRate);
    if (status != noErr) {
        rateSize = sizeof(sampleRate);
        status = AudioObjectGetPropertyData(m_deviceId, &rateAddress, 0, nullptr,
                                            &rateSize, &sampleRate);
        if (status != noErr || sampleRate <= 0.0) {
            return fail_setup(tr("Could not determine the CoreAudio sample rate"), status);
        }
        frame_rate = static_cast<nframes_t>(sampleRate);
    }
    if (separateDevices) {
        Float64 inSampleRate = frame_rate;
        AudioObjectSetPropertyData(m_inputDeviceId, &rateAddress, 0, nullptr,
                                   sizeof(inSampleRate), &inSampleRate);
    }

    AudioObjectPropertyAddress bufferAddress{kAudioDevicePropertyBufferFrameSize,
                                             kAudioObjectPropertyScopeGlobal,
                                             kAudioObjectPropertyElementMain};
    UInt32 bufferSize = frames_per_cycle;
    AudioObjectSetPropertyData(m_deviceId, &bufferAddress, 0, nullptr,
                               sizeof(bufferSize), &bufferSize);
    UInt32 bufferSizeBytes = sizeof(bufferSize);
    status = AudioObjectGetPropertyData(m_deviceId, &bufferAddress, 0, nullptr,
                                        &bufferSizeBytes, &bufferSize);
    if (status != noErr || bufferSize == 0) {
        return fail_setup(tr("Could not determine the CoreAudio buffer size"), status);
    }
    frames_per_cycle = bufferSize;
    if (separateDevices) {
        UInt32 inBufferSize = frames_per_cycle;
        AudioObjectSetPropertyData(m_inputDeviceId, &bufferAddress, 0, nullptr,
                                   sizeof(inBufferSize), &inBufferSize);
    }

    device->set_buffer_size(frames_per_cycle);
    device->set_sample_rate(frame_rate);
    period_usecs = static_cast<trav_time_t>(
        static_cast<double>(frames_per_cycle) / frame_rate * 1000000.0);

    UInt32 inputChannels = 0;
    UInt32 outputChannels = 0;
    if (capture && channel_count(m_inputDeviceId, kAudioObjectPropertyScopeInput,
                                 &inputChannels) != noErr) {
        return fail_setup(tr("Could not query CoreAudio input channels"));
    }
    if (playback && channel_count(m_deviceId, kAudioObjectPropertyScopeOutput,
                                  &outputChannels) != noErr) {
        return fail_setup(tr("Could not query CoreAudio output channels"));
    }

    if (capture && playback && !separateDevices && (inputChannels == 0 || outputChannels == 0) &&
        (cardDevice.isEmpty() || cardDevice == QStringLiteral("none") ||
         cardDevice == QStringLiteral("default"))) {
        AudioDeviceID duplexDev = duplex_device();
        if (duplexDev != kAudioDeviceUnknown) {
            UInt32 duplexIn = 0, duplexOut = 0;
            if (channel_count(duplexDev, kAudioObjectPropertyScopeInput, &duplexIn) == noErr &&
                channel_count(duplexDev, kAudioObjectPropertyScopeOutput, &duplexOut) == noErr &&
                duplexIn > 0 && duplexOut > 0) {
                m_deviceId = duplexDev;
                m_inputDeviceId = duplexDev;
                inputChannels = duplexIn;
                outputChannels = duplexOut;
            }
        }
    }
    m_inputChannels = inputChannels;
    m_outputChannels = outputChannels;
    if ((capture && m_inputChannels == 0) || (playback && m_outputChannels == 0)) {
        return fail_setup(tr("The selected CoreAudio device does not support the requested mode"));
    }

    AudioComponentDescription description{kAudioUnitType_Output, kAudioUnitSubType_HALOutput,
                                          kAudioUnitManufacturer_Apple, 0, 0};
    AudioComponent component = AudioComponentFindNext(nullptr, &description);
    if (!component) {
        return fail_setup(tr("The CoreAudio HAL component is unavailable"));
    }
    status = AudioComponentInstanceNew(component, &m_audioUnit);
    if (status != noErr) {
        return fail_setup(tr("Could not create the CoreAudio HAL unit"), status);
    }

    UInt32 maximumFrames = frames_per_cycle;
    UInt32 enabled = 0;

    if (!playback && capture) {
        enabled = 1;
        status = AudioUnitSetProperty(m_audioUnit, kAudioOutputUnitProperty_EnableIO,
                                      kAudioUnitScope_Input, 1, &enabled, sizeof(enabled));
        if (status != noErr) {
            return fail_setup(tr("Could not enable CoreAudio input"), status);
        }
        enabled = 0;
        status = AudioUnitSetProperty(m_audioUnit, kAudioOutputUnitProperty_EnableIO,
                                      kAudioUnitScope_Output, 0, &enabled, sizeof(enabled));
        if (status != noErr) {
            return fail_setup(tr("Could not disable CoreAudio output"), status);
        }
        status = AudioUnitSetProperty(m_audioUnit, kAudioOutputUnitProperty_CurrentDevice,
                                      kAudioUnitScope_Global, 0, &m_deviceId, sizeof(m_deviceId));
        if (status != noErr) {
            return fail_setup(tr("Could not select the CoreAudio device"), status);
        }
        AudioStreamBasicDescription format = client_format(frame_rate, m_inputChannels);
        status = AudioUnitSetProperty(m_audioUnit, kAudioUnitProperty_StreamFormat,
                                      kAudioUnitScope_Output, 1, &format, sizeof(format));
        if (status != noErr) {
            return fail_setup(tr("Could not configure CoreAudio input format"), status);
        }
        AudioUnitSetProperty(m_audioUnit, kAudioUnitProperty_MaximumFramesPerSlice,
                             kAudioUnitScope_Global, 0, &maximumFrames, sizeof(maximumFrames));

        AURenderCallbackStruct inputCallback{&CoreAudioDriver::input_render_callback, this};
        status = AudioUnitSetProperty(m_audioUnit, kAudioOutputUnitProperty_SetInputCallback,
                                      kAudioUnitScope_Global, 0, &inputCallback, sizeof(inputCallback));
        if (status != noErr) {
            return fail_setup(tr("Could not install the CoreAudio input callback"), status);
        }
    } else {
        enabled = (capture && !separateDevices) ? 1 : 0;
        status = AudioUnitSetProperty(m_audioUnit, kAudioOutputUnitProperty_EnableIO,
                                      kAudioUnitScope_Input, 1, &enabled, sizeof(enabled));
        if (status != noErr) {
            return fail_setup(tr("Could not enable CoreAudio input"), status);
        }
        enabled = 1;
        status = AudioUnitSetProperty(m_audioUnit, kAudioOutputUnitProperty_EnableIO,
                                      kAudioUnitScope_Output, 0, &enabled, sizeof(enabled));
        if (status != noErr) {
            return fail_setup(tr("Could not enable CoreAudio output"), status);
        }
        status = AudioUnitSetProperty(m_audioUnit, kAudioOutputUnitProperty_CurrentDevice,
                                      kAudioUnitScope_Global, 0, &m_deviceId, sizeof(m_deviceId));
        if (status != noErr) {
            return fail_setup(tr("Could not select the CoreAudio device"), status);
        }
        AudioUnitSetProperty(m_audioUnit, kAudioUnitProperty_MaximumFramesPerSlice,
                             kAudioUnitScope_Global, 0, &maximumFrames, sizeof(maximumFrames));

        if (capture && !separateDevices) {
            AudioStreamBasicDescription format = client_format(frame_rate, m_inputChannels);
            status = AudioUnitSetProperty(m_audioUnit, kAudioUnitProperty_StreamFormat,
                                          kAudioUnitScope_Output, 1, &format, sizeof(format));
            if (status != noErr) {
                return fail_setup(tr("Could not configure CoreAudio input format"), status);
            }
        }

        AudioStreamBasicDescription format = client_format(frame_rate, m_outputChannels);
        status = AudioUnitSetProperty(m_audioUnit, kAudioUnitProperty_StreamFormat,
                                      kAudioUnitScope_Input, 0, &format, sizeof(format));
        if (status != noErr) {
            return fail_setup(tr("Could not configure CoreAudio output format"), status);
        }

        AURenderCallbackStruct callback{&CoreAudioDriver::render_callback, this};
        status = AudioUnitSetProperty(m_audioUnit, kAudioUnitProperty_SetRenderCallback,
                                      kAudioUnitScope_Input, 0, &callback, sizeof(callback));
        if (status != noErr) {
            return fail_setup(tr("Could not install the CoreAudio render callback"), status);
        }
    }

    if (separateDevices) {
        status = AudioComponentInstanceNew(component, &m_inputAudioUnit);
        if (status != noErr) {
            return fail_setup(tr("Could not create the CoreAudio input unit"), status);
        }
        enabled = 1;
        status = AudioUnitSetProperty(m_inputAudioUnit, kAudioOutputUnitProperty_EnableIO,
                                      kAudioUnitScope_Input, 1, &enabled, sizeof(enabled));
        if (status != noErr) {
            return fail_setup(tr("Could not enable the selected CoreAudio input"), status);
        }
        enabled = 0;
        status = AudioUnitSetProperty(m_inputAudioUnit, kAudioOutputUnitProperty_EnableIO,
                                      kAudioUnitScope_Output, 0, &enabled, sizeof(enabled));
        if (status != noErr) {
            return fail_setup(tr("Could not disable CoreAudio input-unit output"), status);
        }
        status = AudioUnitSetProperty(m_inputAudioUnit, kAudioOutputUnitProperty_CurrentDevice,
                                      kAudioUnitScope_Global, 0, &m_inputDeviceId,
                                      sizeof(m_inputDeviceId));
        if (status != noErr) {
            return fail_setup(tr("Could not select the CoreAudio input device"), status);
        }
        AudioStreamBasicDescription inputFormat = client_format(frame_rate, m_inputChannels);
        status = AudioUnitSetProperty(m_inputAudioUnit, kAudioUnitProperty_StreamFormat,
                                      kAudioUnitScope_Output, 1, &inputFormat,
                                      sizeof(inputFormat));
        if (status != noErr) {
            return fail_setup(tr("Could not configure the selected CoreAudio input"), status);
        }
        status = AudioUnitSetProperty(m_inputAudioUnit,
                                      kAudioUnitProperty_MaximumFramesPerSlice,
                                      kAudioUnitScope_Global, 0, &maximumFrames,
                                      sizeof(maximumFrames));
        if (status != noErr) {
            return fail_setup(tr("Could not configure the CoreAudio input buffer size"), status);
        }
        AURenderCallbackStruct inputCallback{&CoreAudioDriver::input_render_callback, this};
        status = AudioUnitSetProperty(m_inputAudioUnit, kAudioOutputUnitProperty_SetInputCallback,
                                      kAudioUnitScope_Global, 0, &inputCallback,
                                      sizeof(inputCallback));
        if (status != noErr) {
            return fail_setup(tr("Could not install the CoreAudio input callback"), status);
        }
    }

    status = AudioUnitInitialize(m_audioUnit);
    if (status != noErr) {
        return fail_setup(tr("Could not initialize the CoreAudio HAL unit"), status);
    }
    if (separateDevices) {
        status = AudioUnitInitialize(m_inputAudioUnit);
        if (status != noErr) {
            return fail_setup(tr("Could not initialize the CoreAudio input unit"), status);
        }
    }

    if (capture) {
        const size_t listSize = offsetof(AudioBufferList, mBuffers) + sizeof(AudioBuffer);
        m_inputList = static_cast<AudioBufferList*>(std::calloc(1, listSize));
        if (!m_inputList) {
            return fail_setup(tr("Could not allocate CoreAudio input buffers"));
        }
        m_inputList->mNumberBuffers = 1;
        m_inputList->mBuffers[0].mNumberChannels = m_inputChannels;
        m_inputList->mBuffers[0].mDataByteSize =
            frames_per_cycle * m_inputChannels * sizeof(audio_sample_t);
        m_inputBuffer = static_cast<audio_sample_t*>(std::calloc(
            frames_per_cycle * m_inputChannels, sizeof(audio_sample_t)));
        if (!m_inputBuffer) {
            return fail_setup(tr("Could not allocate CoreAudio capture storage"));
        }
        m_inputList->mBuffers[0].mData = m_inputBuffer;

        if (separateDevices) {
            m_processInputBuffer = static_cast<audio_sample_t*>(std::calloc(
                frames_per_cycle * m_inputChannels, sizeof(audio_sample_t)));
            if (!m_processInputBuffer) {
                return fail_setup(tr("Could not allocate CoreAudio process capture storage"));
            }
            m_inputRingBuffer = std::make_unique<RingBufferNPT<audio_sample_t>>(
                frames_per_cycle * m_inputChannels * 8);
        }
    }

    for (channel_t index = 0; index < m_inputChannels; ++index) {
        AudioChannel* channel = add_capture_channel(QStringLiteral("capture_%1").arg(index + 1));
        channel->set_latency(frames_per_cycle + capture_frame_latency);
    }
    for (channel_t index = 0; index < m_outputChannels; ++index) {
        AudioChannel* channel = add_playback_channel(QStringLiteral("playback_%1").arg(index + 1));
        channel->set_latency(frames_per_cycle + playback_frame_latency);
    }

    device->driverSetupMessage(tr("Connected to %1").arg(device_name(m_deviceId)),
                               AudioDevice::DRIVER_SETUP_SUCCESS);
    return 1;
}

int CoreAudioDriver::attach()
{
    device->set_buffer_size(frames_per_cycle);
    device->set_sample_rate(frame_rate);
    return 1;
}

int CoreAudioDriver::start()
{
    if (m_inputRingBuffer) {
        m_inputRingBuffer->reset();
    }
    if (m_inputAudioUnit && AudioOutputUnitStart(m_inputAudioUnit) != noErr) {
        return -1;
    }
    if (m_audioUnit && AudioOutputUnitStart(m_audioUnit) != noErr) {
        if (m_inputAudioUnit) {
            AudioOutputUnitStop(m_inputAudioUnit);
        }
        return -1;
    }
    m_running = true;
    return 1;
}

int CoreAudioDriver::stop()
{
    if (m_audioUnit && m_running) {
        AudioOutputUnitStop(m_audioUnit);
    }
    if (m_inputAudioUnit && m_running) {
        AudioOutputUnitStop(m_inputAudioUnit);
    }
    m_running = false;
    return 1;
}

int CoreAudioDriver::_read(nframes_t)
{
    return 1;
}

int CoreAudioDriver::_write(nframes_t)
{
    return 1;
}

int CoreAudioDriver::process_callback(AudioUnitRenderActionFlags* flags,
                                       const AudioTimeStamp* timestamp,
                                       nframes_t nframes,
                                       AudioBufferList* output)
{
    Q_UNUSED(flags);

    if (m_capture) {
        if (!m_inputAudioUnit) {
            m_inputList->mNumberBuffers = 1;
            m_inputList->mBuffers[0].mNumberChannels = m_inputChannels;
            m_inputList->mBuffers[0].mDataByteSize = nframes * m_inputChannels * sizeof(audio_sample_t);
            m_inputList->mBuffers[0].mData = m_inputBuffer;

            AudioUnitRenderActionFlags inActionFlags = 0;
            m_lastInputRenderStatus = AudioUnitRender(m_audioUnit, &inActionFlags, timestamp, 1,
                                                      nframes, m_inputList);
            if (m_lastInputRenderStatus != noErr) {
                if (m_lastInputRenderStatus != m_reportedInputRenderStatus) {
                    std::fprintf(stderr, "CoreAudioDriver: AudioUnitRender failed with status %d\n",
                                 m_lastInputRenderStatus);
                    m_reportedInputRenderStatus = m_lastInputRenderStatus;
                }
                device->xrun();
                return -1;
            }

            const auto* input = static_cast<const audio_sample_t*>(m_inputList->mBuffers[0].mData);
            for (channel_t channel = 0; channel < m_inputChannels; ++channel) {
                m_captureChannels.at(channel)->read_from_hardware_port_interleaved(input, nframes, m_inputChannels, channel);
            }
        } else if (m_inputRingBuffer && m_processInputBuffer) {
            const size_t neededSamples = nframes * m_inputChannels;
            size_t readSamples = m_inputRingBuffer->read(m_processInputBuffer, neededSamples);
            if (readSamples < neededSamples) {
                std::memset(m_processInputBuffer + readSamples, 0,
                            (neededSamples - readSamples) * sizeof(audio_sample_t));
            }

            for (channel_t channel = 0; channel < m_inputChannels; ++channel) {
                m_captureChannels.at(channel)->read_from_hardware_port_interleaved(m_processInputBuffer, nframes, m_inputChannels, channel);
            }
        }
    }

    device->transport_cycle_start(get_microseconds());
    if (device->run_cycle(nframes, 0) < 0) {
        return -1;
    }

    if (m_playback && output) {
        if (output->mNumberBuffers == 1 && output->mBuffers[0].mData) {
            auto* destination = static_cast<audio_sample_t*>(output->mBuffers[0].mData);
            for (nframes_t frame = 0; frame < nframes; ++frame) {
                for (channel_t channel = 0; channel < m_outputChannels; ++channel) {
                    destination[frame * m_outputChannels + channel] =
                        m_playbackChannels.at(channel)->get_buffer(nframes)[frame];
                }
            }
        } else if (output->mNumberBuffers >= m_outputChannels) {
            for (channel_t channel = 0; channel < m_outputChannels; ++channel) {
                if (output->mBuffers[channel].mData) {
                    std::memcpy(output->mBuffers[channel].mData,
                                m_playbackChannels.at(channel)->get_buffer(nframes),
                                nframes * sizeof(audio_sample_t));
                }
            }
        }
    }
    for (channel_t channel = 0; channel < m_outputChannels; ++channel) {
        m_playbackChannels.at(channel)->silence_buffer(nframes);
    }

    device->transport_cycle_end(get_microseconds());
    return 0;
}

OSStatus CoreAudioDriver::capture_callback(AudioUnitRenderActionFlags* flags,
                                             const AudioTimeStamp* timestamp,
                                             nframes_t nframes)
{
    Q_UNUSED(flags);

    if (!m_capture || !m_inputList || !m_inputBuffer) {
        return noErr;
    }

    AudioUnit unit = m_inputAudioUnit ? m_inputAudioUnit : m_audioUnit;

    m_inputList->mNumberBuffers = 1;
    m_inputList->mBuffers[0].mNumberChannels = m_inputChannels;
    m_inputList->mBuffers[0].mDataByteSize = nframes * m_inputChannels * sizeof(audio_sample_t);
    m_inputList->mBuffers[0].mData = m_inputBuffer;

    AudioUnitRenderActionFlags inActionFlags = 0;
    m_lastInputRenderStatus = AudioUnitRender(unit, &inActionFlags, timestamp, 1,
                                              nframes, m_inputList);
    if (m_lastInputRenderStatus != noErr) {
        if (m_lastInputRenderStatus != m_reportedInputRenderStatus) {
            std::fprintf(stderr, "CoreAudioDriver: input AudioUnitRender failed with status %d\n",
                         m_lastInputRenderStatus);
            m_reportedInputRenderStatus = m_lastInputRenderStatus;
        }
        device->xrun();
        return m_lastInputRenderStatus;
    }

    if (!m_playback) {
        const auto* input = static_cast<const audio_sample_t*>(m_inputList->mBuffers[0].mData);
        for (channel_t channel = 0; channel < m_inputChannels; ++channel) {
            m_captureChannels.at(channel)->read_from_hardware_port_interleaved(input, nframes, m_inputChannels, channel);
        }

        device->transport_cycle_start(get_microseconds());
        if (device->run_cycle(nframes, 0) < 0) {
            return kAudioHardwareUnspecifiedError;
        }
        device->transport_cycle_end(get_microseconds());
        return noErr;
    }

    if (m_inputRingBuffer) {
        const size_t samples = nframes * m_inputChannels;
        if (m_inputRingBuffer->write_space() < samples) {
            m_inputRingBuffer->increment_read_ptr(samples - m_inputRingBuffer->write_space());
        }
        m_inputRingBuffer->write(m_inputBuffer, samples);
    }

    return noErr;
}

OSStatus CoreAudioDriver::render_callback(void* refCon, AudioUnitRenderActionFlags* flags,
                                           const AudioTimeStamp* timestamp, UInt32, UInt32 frames,
                                           AudioBufferList* output)
{
    return static_cast<CoreAudioDriver*>(refCon)->process_callback(flags, timestamp, frames, output) == 0
               ? noErr
               : kAudioHardwareUnspecifiedError;
}

OSStatus CoreAudioDriver::input_render_callback(void* refCon, AudioUnitRenderActionFlags* flags,
                                                 const AudioTimeStamp* timestamp, UInt32,
                                                 UInt32 frames, AudioBufferList*)
{
    return static_cast<CoreAudioDriver*>(refCon)->capture_callback(flags, timestamp, frames);
}

QString CoreAudioDriver::get_device_name()
{
    if (m_inputAudioUnit && m_inputDeviceId != m_deviceId) {
        return QStringLiteral("%1 / %2").arg(device_name(m_inputDeviceId), device_name(m_deviceId));
    }
    return device_name(m_deviceId);
}

QString CoreAudioDriver::get_device_longname()
{
    return get_device_name();
}

QStringList CoreAudioDriver::devices_info(bool input)
{
    QStringList result;
    AudioObjectPropertyAddress address{kAudioHardwarePropertyDevices,
                                       kAudioObjectPropertyScopeGlobal,
                                       kAudioObjectPropertyElementMain};
    UInt32 size = 0;
    if (AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &address, 0, nullptr, &size) != noErr) {
        return result;
    }
    const UInt32 count = size / sizeof(AudioDeviceID);
    std::unique_ptr<AudioDeviceID[]> devices(new AudioDeviceID[count]);
    if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &address, 0, nullptr, &size,
                                   devices.get()) != noErr) {
        return result;
    }
    for (UInt32 index = 0; index < count; ++index) {
        UInt32 channels = 0;
        if (channel_count(devices[index], input ? kAudioObjectPropertyScopeInput
                                                : kAudioObjectPropertyScopeOutput,
                          &channels) == noErr && channels > 0) {
            const QString uid = device_uid(devices[index]);
            if (!uid.isEmpty()) {
                result.append(device_name(devices[index]) + QStringLiteral("###") + uid);
            }
        }
    }
    return result;
}
