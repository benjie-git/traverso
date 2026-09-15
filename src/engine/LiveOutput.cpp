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

#include "LiveOutput.h"

#include "AudioChannel.h"
#include "RingBufferNPT.h"
#include "defines.h"

#include <algorithm>
#include <cstring>

// Backend factories, each defined in its own translation unit so that no
// backend's SDK headers leak into this file.
#if defined (COREAUDIO_SUPPORT)
LiveOutput* create_coreaudio_live_output();
#endif

#if defined (PORTAUDIO_SUPPORT)
LiveOutput* create_portaudio_live_output();
#endif

#if defined (PIPEWIRE_SUPPORT)
LiveOutput* create_pipewire_live_output();
#endif

// Always put me below _all_ includes, this is needed
// in case we run with memory leak detection enabled!
#include "Debugger.h"


BufferedLiveOutput::BufferedLiveOutput()
	: m_channels(0)
	, m_bufferSize(1024)
	, m_started(false)
{
}

bool BufferedLiveOutput::init_buffers(nframes_t bufferSize, uint channels)
{
	free_buffers();

	if (bufferSize == 0 || channels == 0) {
		return false;
	}

	m_bufferSize = bufferSize;
	m_channels = channels;

	for (uint i = 0; i < channels; ++i) {
		m_rings.append(new RingBufferNPT<audio_sample_t>(bufferSize * 8));
	}
	m_scratch.resize(bufferSize * channels);

	return true;
}

void BufferedLiveOutput::free_buffers()
{
	for (RingBufferNPT<audio_sample_t>* ring : m_rings) {
		delete ring;
	}
	m_rings.clear();
	m_scratch.resize(0);
}

void BufferedLiveOutput::reset_buffers()
{
	for (RingBufferNPT<audio_sample_t>* ring : m_rings) {
		ring->reset();
	}
}

// Called from the primary audio thread after the live render pass.
void BufferedLiveOutput::process(nframes_t nframes, const QList<AudioChannel*>& channels, uint channelCount)
{
	if (!m_open) {
		return;
	}

	const uint count = std::min<uint>(channelCount, m_channels);
	for (uint i = 0; i < count; ++i) {
		if (i >= uint(channels.size()) || i >= uint(m_rings.size())) {
			break;
		}
		AudioChannel* channel = channels.at(i);
		if (!channel) {
			continue;
		}

		// The live sink tolerates clock drift; likewise it must tolerate a
		// channel whose buffer is momentarily a different size than nframes
		// (e.g. while the primary driver is being reinitialised).
		const nframes_t available = nframes_t(channel->get_buffer_size());
		const nframes_t bufferFrames = std::min(nframes, available);
		if (bufferFrames == 0) {
			continue;
		}

		RingBufferNPT<audio_sample_t>* ring = m_rings.at(i);
		const audio_sample_t* source = channel->get_buffer(bufferFrames);
		const size_t space = ring->write_space();
		const size_t toWrite = std::min<size_t>(space, bufferFrames);

		// Overrun: drop the excess rather than block.
		if (toWrite > 0) {
			ring->write(const_cast<audio_sample_t*>(source), toWrite);
		}
	}
}

void BufferedLiveOutput::pull_channel(uint channel, nframes_t frames, audio_sample_t* dest)
{
	if (!dest) {
		return;
	}

	if (!m_started || channel >= m_channels || channel >= uint(m_rings.size())) {
		std::memset(dest, 0, frames * sizeof(audio_sample_t));
		return;
	}

	const nframes_t n = std::min<nframes_t>(frames, m_bufferSize);
	std::memset(dest, 0, frames * sizeof(audio_sample_t));

	const size_t available = m_rings.at(channel)->read_space();
	const nframes_t toRead = std::min<nframes_t>(n, available);
	if (toRead > 0) {
		m_rings.at(channel)->read(dest, toRead);
	}
}

void BufferedLiveOutput::pull_interleaved(nframes_t frames, audio_sample_t* out)
{
	const uint count = m_channels;

	if (!out) {
		return;
	}

	if (!m_started || count == 0 || uint(m_rings.size()) < count) {
		std::memset(out, 0, frames * (count ? count : 1) * sizeof(audio_sample_t));
		return;
	}

	const nframes_t n = std::min<nframes_t>(frames, m_bufferSize);

	for (uint c = 0; c < count; ++c) {
		pull_channel(c, n, m_scratch.data() + (c * m_bufferSize));
	}

	for (nframes_t f = 0; f < n; ++f) {
		for (uint c = 0; c < count; ++c) {
			out[f * count + c] = m_scratch[c * m_bufferSize + f];
		}
	}

	if (frames > n) {
		std::memset(out + (n * count), 0, (frames - n) * count * sizeof(audio_sample_t));
	}
}


LiveOutput* create_live_output(const QString& driverType)
{
#if defined (COREAUDIO_SUPPORT)
	if (driverType == "CoreAudio") {
		return create_coreaudio_live_output();
	}
#endif
#if defined (PORTAUDIO_SUPPORT)
	if (driverType == "PortAudio") {
		return create_portaudio_live_output();
	}
#endif
#if defined (PIPEWIRE_SUPPORT)
	if (driverType == "PipeWire") {
		return create_pipewire_live_output();
	}
#endif
	Q_UNUSED(driverType);
	return new LiveOutput();
}

//eof
