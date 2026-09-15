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

#include "PADriverLiveOutput.h"

#include "defines.h"

#include <algorithm>
#include <cstring>

// Always put me below _all_ includes, this is needed
// in case we run with memory leak detection enabled!
#include "Debugger.h"


PADriverLiveOutput::PADriverLiveOutput()
	: m_stream(nullptr)
{
}

PADriverLiveOutput::~PADriverLiveOutput()
{
	close();
}

PaDeviceIndex PADriverLiveOutput::resolve_device(const QString& uid)
{
	if (uid.isEmpty() || uid == "default" || uid == "none") {
		return Pa_GetDefaultOutputDevice();
	}

	const int count = Pa_GetDeviceCount();
	for (int i = 0; i < count; ++i) {
		const PaDeviceInfo* info = Pa_GetDeviceInfo(i);
		if (info && info->maxOutputChannels > 0 && QString::fromUtf8(info->name) == uid) {
			return i;
		}
	}

	return paNoDevice;
}

int PADriverLiveOutput::open(const QString& uid, uint rate, nframes_t bufferSize, uint channels)
{
	if (m_open) {
		return 0;
	}

	if (channels == 0 || bufferSize == 0 || rate == 0) {
		return -1;
	}

	// PortAudio is reference counted: our Pa_Initialize is balanced by the
	// Pa_Terminate in close(), while the main PADriver keeps its own.
	if (Pa_Initialize() != paNoError) {
		return -1;
	}

	PaDeviceIndex device = resolve_device(uid);
	const PaDeviceInfo* info = device == paNoDevice ? nullptr : Pa_GetDeviceInfo(device);
	if (!info || info->maxOutputChannels <= 0) {
		Pa_Terminate();
		return -1;
	}

	const uint outChannels = std::min<uint>(channels, info->maxOutputChannels);

	PaStreamParameters outputParameters;
	std::memset(&outputParameters, 0, sizeof(outputParameters));
	outputParameters.device = device;
	outputParameters.channelCount = outChannels;
	outputParameters.sampleFormat = paFloat32;
	outputParameters.suggestedLatency = info->defaultLowOutputLatency;
	outputParameters.hostApiSpecificStreamInfo = nullptr;

	PaError err = Pa_OpenStream(&m_stream,
				    nullptr,
				    &outputParameters,
				    rate,
				    bufferSize,
				    paNoFlag,
				    &PADriverLiveOutput::process_callback,
				    this);
	if (err != paNoError || !m_stream) {
		m_stream = nullptr;
		Pa_Terminate();
		return -1;
	}

	m_deviceName = QString::fromUtf8(info->name);

	if (!init_buffers(bufferSize, outChannels)) {
		close();
		return -1;
	}

	m_open = true;
	return 0;
}

void PADriverLiveOutput::close()
{
	m_open = false;
	stop();

	if (m_stream) {
		Pa_CloseStream(m_stream);
		m_stream = nullptr;
	}
	Pa_Terminate();

	free_buffers();

	m_deviceName.clear();
	m_open = false;
}

void PADriverLiveOutput::start()
{
	if (!m_open || started()) {
		return;
	}

	reset_buffers();

	if (Pa_StartStream(m_stream) == paNoError) {
		set_started(true);
	}
}

void PADriverLiveOutput::stop()
{
	if (m_stream && started()) {
		Pa_StopStream(m_stream);
		set_started(false);
	}
}

int PADriverLiveOutput::process_callback(const void* inputBuffer,
					 void* outputBuffer,
					 unsigned long framesPerBuffer,
					 const PaStreamCallbackTimeInfo* timeInfo,
					 PaStreamCallbackFlags statusFlags,
					 void* arg)
{
	Q_UNUSED(inputBuffer);
	Q_UNUSED(timeInfo);
	Q_UNUSED(statusFlags);

	PADriverLiveOutput* self = static_cast<PADriverLiveOutput*>(arg);
	return self->render(framesPerBuffer, static_cast<audio_sample_t*>(outputBuffer));
}

int PADriverLiveOutput::render(unsigned long frames, audio_sample_t* out)
{
	if (!out) {
		return paContinue;
	}

	pull_interleaved(frames, out);

	return paContinue;
}

LiveOutput* create_portaudio_live_output()
{
	return new PADriverLiveOutput();
}
