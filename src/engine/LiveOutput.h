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

#ifndef LIVEOUTPUT_H
#define LIVEOUTPUT_H

#include "defines.h"

#include <QList>
#include <QString>
#include <QVarLengthArray>
#include <atomic>

class AudioChannel;
template<class T> class RingBufferNPT;

/**
 * LiveOutput is a secondary audio sink, used by the Live Play Head to play
 * the same session out of a second physical output device.
 *
 * The primary audio thread pushes the live mix (a list of AudioChannel
 * buffers) into the sink after the live render pass; the sink's own callback
 * drains it to the secondary device.
 *
 * The base class is a no-op / unsupported implementation so that platforms
 * without a backend simply produce silence instead of crashing.
 *
 * Backend implementations live in their own translation units
 * (CoreAudioLiveOutput, PADriverLiveOutput, ...) and are created through
 * create_live_output().
 */
class LiveOutput
{
public:
	LiveOutput() : m_open(false) {}
	virtual ~LiveOutput() {}

	virtual bool is_supported() const { return false; }

	virtual int open(const QString& uid, uint rate, nframes_t bufferSize, uint channels)
	{
		Q_UNUSED(uid);
		Q_UNUSED(rate);
		Q_UNUSED(bufferSize);
		Q_UNUSED(channels);
		return -1;
	}

	virtual void close() {}
	virtual void start() {}
	virtual void stop() {}

	// Push one period of the live mix. channels holds one AudioChannel per
	// output channel; each provides nframes of audio_sample_t.
	virtual void process(nframes_t nframes, const QList<AudioChannel*>& channels, uint channelCount)
	{
		Q_UNUSED(nframes);
		Q_UNUSED(channels);
		Q_UNUSED(channelCount);
	}

	bool is_open() const { return m_open.load(std::memory_order_acquire); }

	// True while the sink is actively streaming to the device. The primary
	// audio thread only pushes when this is true, so a stopped Live playhead
	// fully closes out the streaming operation (no push loop, device stopped).
	virtual bool is_running() const { return false; }

	// Human readable name of the device currently opened (empty if unknown).
	virtual QString device_name() const { return QString(); }

protected:
	std::atomic<bool> m_open;
};

/**
 * BufferedLiveOutput implements the double-buffering shared by every backend
 * that streams to a device on its own clock: a ring buffer per channel that
 * the primary audio thread writes (process()) and the device callback drains
 * (pull_interleaved()/pull_channel()). Underruns are zero-filled and overruns
 * are dropped, so the two clocks may drift without blocking either thread.
 *
 * Subclasses only handle device acquisition and their callback shim.
 */
class BufferedLiveOutput : public LiveOutput
{
public:
	void process(nframes_t nframes, const QList<AudioChannel*>& channels, uint channelCount) override;

	bool is_running() const override { return m_started.load(std::memory_order_acquire); }

protected:
	BufferedLiveOutput();
	~BufferedLiveOutput() override;

	// Allocate the per-channel rings and scratch space. Call from open()
	// once the number of channels and the buffer size are known. The
	// allocation happens only on the first call: the rings are then kept for
	// the lifetime of the object, so the device callback and the primary
	// audio thread always have valid storage even while a later open()/close()
	// changes the device.
	bool ensure_buffers(nframes_t bufferSize, uint channels);
	void free_buffers();
	void reset_buffers();

	// Ask the device callback to discard anything queued before the next
	// start, so that a (re)start produces a few zero samples rather than
	// whatever was left in the rings. Applied on the reader side, which only
	// moves the read pointer forward.
	void request_flush() { m_flush.store(true, std::memory_order_release); }

	uint channels() const { return m_channels; }
	nframes_t buffer_size() const { return m_bufferSize; }

	bool started() const { return m_started.load(std::memory_order_acquire); }
	void set_started(bool started) { m_started.store(started, std::memory_order_release); }

	// Drain the rings for a device callback. `frames` is clamped to the
	// configured buffer size; any shortfall or tail is zero-filled.
	void pull_interleaved(nframes_t frames, audio_sample_t* out);
	void pull_channel(uint channel, nframes_t frames, audio_sample_t* dest);

	uint m_channels;
	nframes_t m_bufferSize;
	std::atomic<bool> m_started;
	std::atomic<bool> m_flush;
	QList<RingBufferNPT<audio_sample_t>*> m_rings;
	QVarLengthArray<audio_sample_t> m_scratch;

private:
	void apply_pending_flush();
};

// Factory: returns an implementation for the given driver type ("CoreAudio",
// "PortAudio", ...), or an unsupported no-op when no backend is available.
LiveOutput* create_live_output(const QString& driverType);

#endif
