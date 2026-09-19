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

#include "CoreAudioLiveOutput.h"

#include "RingBufferNPT.h"
#include "defines.h"

#include <algorithm>
#include <cstring>

// Always put me below _all_ includes, this is needed
// in case we run with memory leak detection enabled!
#include "Debugger.h"


namespace {

OSStatus property_data(AudioObjectID object,
		       AudioObjectPropertySelector selector,
		       AudioObjectPropertyScope scope,
		       void* value,
		       UInt32* size)
{
	AudioObjectPropertyAddress address{selector, scope, kAudioObjectPropertyElementMain};
	return AudioObjectGetPropertyData(object, &address, 0, nullptr, size, value);
}

OSStatus device_for_uid(const QString& uid, AudioDeviceID* device)
{
	const QByteArray uidBytes = uid.toUtf8();
	if (uidBytes.isEmpty() || !device) {
		return kAudioHardwareBadDeviceError;
	}

	CFStringRef value = CFStringCreateWithCString(kCFAllocatorDefault, uidBytes.constData(), kCFStringEncodingUTF8);
	if (!value) {
		return kAudioHardwareUnspecifiedError;
	}

	AudioValueTranslation translation{&value, sizeof(value), device, sizeof(*device)};
	UInt32 size = sizeof(translation);
	AudioObjectPropertyAddress address{kAudioHardwarePropertyDeviceForUID,
					   kAudioObjectPropertyScopeGlobal,
					   kAudioObjectPropertyElementMain};

	OSStatus status = AudioObjectGetPropertyData(kAudioObjectSystemObject, &address, 0, nullptr, &size, &translation);
	CFRelease(value);

	if (status == noErr && *device == kAudioDeviceUnknown) {
		return kAudioHardwareBadDeviceError;
	}

	return status;
}

AudioDeviceID default_output_device()
{
	AudioDeviceID device = kAudioDeviceUnknown;
	UInt32 size = sizeof(device);
	property_data(kAudioObjectSystemObject,
		      kAudioHardwarePropertyDefaultOutputDevice,
		      kAudioObjectPropertyScopeGlobal,
		      &device,
		      &size);
	return device;
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

QString device_display_name(AudioDeviceID device)
{
	CFStringRef name = nullptr;
	UInt32 size = sizeof(name);
	if (property_data(device, kAudioObjectPropertyName,
			  kAudioObjectPropertyScopeGlobal, &name, &size) != noErr || !name) {
		return QString();
	}

	char buffer[256]{};
	QString result = CFStringGetCString(name, buffer, sizeof(buffer), kCFStringEncodingUTF8)
	                     ? QString::fromUtf8(buffer)
	                     : QString();
	CFRelease(name);
	return result;
}

} // anonymous namespace


CoreAudioLiveOutput::CoreAudioLiveOutput()
	: m_audioUnit{}
	, m_deviceId{kAudioDeviceUnknown}
{
}

CoreAudioLiveOutput::~CoreAudioLiveOutput()
{
	close();
}

int CoreAudioLiveOutput::open(const QString& uid, uint rate, nframes_t bufferSize, uint channels)
{
	if (m_open) {
		return 0;
	}

	if (channels == 0 || bufferSize == 0 || rate == 0) {
		return -1;
	}

	AudioDeviceID deviceId = kAudioDeviceUnknown;
	if (uid.isEmpty() || uid == "default") {
		deviceId = default_output_device();
	} else if (device_for_uid(uid, &deviceId) != noErr) {
		deviceId = kAudioDeviceUnknown;
	}
	if (deviceId == kAudioDeviceUnknown) {
		return -1;
	}

	m_deviceId = deviceId;
	m_deviceName = device_display_name(m_deviceId);

	AudioComponentDescription description{kAudioUnitType_Output,
					      kAudioUnitSubType_HALOutput,
					      kAudioUnitManufacturer_Apple,
					      0,
					      0};
	AudioComponent component = AudioComponentFindNext(nullptr, &description);
	if (!component) {
		return -1;
	}

	if (AudioComponentInstanceNew(component, &m_audioUnit) != noErr) {
		m_audioUnit = nullptr;
		return -1;
	}

	// Output-only: enable the output bus (element 0, scope Output),
	// keep the input bus disabled.
	UInt32 enabled = 0;
	AudioUnitSetProperty(m_audioUnit, kAudioOutputUnitProperty_EnableIO,
			     kAudioUnitScope_Input, 1, &enabled, sizeof(enabled));
	enabled = 1;
	AudioUnitSetProperty(m_audioUnit, kAudioOutputUnitProperty_EnableIO,
			     kAudioUnitScope_Output, 0, &enabled, sizeof(enabled));

	if (AudioUnitSetProperty(m_audioUnit, kAudioOutputUnitProperty_CurrentDevice,
				 kAudioUnitScope_Global, 0, &m_deviceId, sizeof(m_deviceId)) != noErr) {
		close();
		return -1;
	}

	UInt32 maximumFrames = std::max<UInt32>(bufferSize, 4096);
	AudioUnitSetProperty(m_audioUnit, kAudioUnitProperty_MaximumFramesPerSlice,
			     kAudioUnitScope_Global, 0, &maximumFrames, sizeof(maximumFrames));

	AudioStreamBasicDescription format = client_format(rate, channels);
	if (AudioUnitSetProperty(m_audioUnit, kAudioUnitProperty_StreamFormat,
				 kAudioUnitScope_Input, 0, &format, sizeof(format)) != noErr) {
		close();
		return -1;
	}

	AURenderCallbackStruct callback{&CoreAudioLiveOutput::render_callback, this};
	if (AudioUnitSetProperty(m_audioUnit, kAudioUnitProperty_SetRenderCallback,
				 kAudioUnitScope_Input, 0, &callback, sizeof(callback)) != noErr) {
		close();
		return -1;
	}

	if (AudioUnitInitialize(m_audioUnit) != noErr) {
		close();
		return -1;
	}

	if (!ensure_buffers(bufferSize, channels)) {
		close();
		return -1;
	}

	m_open = true;
	return 0;
}

void CoreAudioLiveOutput::close()
{
	// Mark closed first so process() bails out even if a push is somehow
	// in flight. The rings themselves are kept for the object's lifetime, so
	// a concurrent push can never see freed storage.
	m_open = false;
	stop();

	if (m_audioUnit) {
		AudioUnitUninitialize(m_audioUnit);
		AudioComponentInstanceDispose(m_audioUnit);
		m_audioUnit = nullptr;
	}

	m_deviceId = kAudioDeviceUnknown;
	m_deviceName.clear();
}

void CoreAudioLiveOutput::start()
{
	if (!is_open() || started()) {
		return;
	}

	// The writer is gated while !started(), so rewinding the pointers here is
	// safe. request_flush() makes the first callback discard whatever the
	// writer queues in the meantime and emit zeros instead.
	reset_buffers();
	request_flush();

	if (AudioOutputUnitStart(m_audioUnit) == noErr) {
		set_started(true);
	}
}

void CoreAudioLiveOutput::stop()
{
	if (m_audioUnit && started()) {
		AudioOutputUnitStop(m_audioUnit);
		set_started(false);
	}
}

OSStatus CoreAudioLiveOutput::render_callback(void* inRefCon,
					      AudioUnitRenderActionFlags* ioActionFlags,
					      const AudioTimeStamp* inTimeStamp,
					      UInt32 inBusNumber,
					      UInt32 inNumberFrames,
					      AudioBufferList* ioData)
{
	Q_UNUSED(ioActionFlags);
	Q_UNUSED(inTimeStamp);
	Q_UNUSED(inBusNumber);

	CoreAudioLiveOutput* self = static_cast<CoreAudioLiveOutput*>(inRefCon);
	return self->render(inNumberFrames, ioData);
}

OSStatus CoreAudioLiveOutput::render(UInt32 inNumberFrames, AudioBufferList* ioData)
{
	if (!ioData) {
		return noErr;
	}

	if (!started() || channels() == 0 || channels() > uint(m_rings.size())) {
		for (UInt32 i = 0; i < ioData->mNumberBuffers; ++i) {
			std::memset(ioData->mBuffers[i].mData, 0, ioData->mBuffers[i].mDataByteSize);
		}
		return noErr;
	}

	if (ioData->mNumberBuffers == 1) {
		audio_sample_t* out = static_cast<audio_sample_t*>(ioData->mBuffers[0].mData);
		pull_interleaved(inNumberFrames, out);
	} else {
		for (UInt32 i = 0; i < ioData->mNumberBuffers; ++i) {
			audio_sample_t* out = static_cast<audio_sample_t*>(ioData->mBuffers[i].mData);
			std::memset(out, 0, ioData->mBuffers[i].mDataByteSize);
			pull_channel(i, inNumberFrames, out);
		}
	}

	return noErr;
}

LiveOutput* create_coreaudio_live_output()
{
	return new CoreAudioLiveOutput();
}
