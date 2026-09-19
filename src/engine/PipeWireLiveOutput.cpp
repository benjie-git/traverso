/*
    Copyright (C) 2026 Remon Sijrier, Ben Levitt

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

#include "PipeWireLiveOutput.h"

#if defined (PIPEWIRE_SUPPORT)

#include "defines.h"

#include <cstring>

// Always put me below _all_ includes, this is needed
// in case we run with memory leak detection enabled!
#include "Debugger.h"


PipeWireLiveOutput::PipeWireLiveOutput()
	: m_threadLoop(nullptr)
	, m_stream(nullptr)
	, m_events{}
	, m_pwInitialized(false)
{
}

PipeWireLiveOutput::~PipeWireLiveOutput()
{
	close();
}

struct pw_properties* PipeWireLiveOutput::build_properties(uint rate, nframes_t bufferSize, const QString& target) const
{
	const QByteArray latencyStr = QString("%1/%2").arg(bufferSize).arg(rate).toUtf8();
	const QByteArray quantumStr = QString::number(bufferSize).toUtf8();
	const QByteArray rateStr = QString("1/%1").arg(rate).toUtf8();

	struct pw_properties* props = pw_properties_new(
		PW_KEY_MEDIA_TYPE, "Audio",
		PW_KEY_MEDIA_CATEGORY, "Playback",
		PW_KEY_MEDIA_ROLE, "Production",
		PW_KEY_APP_NAME, "Traverso",
		PW_KEY_NODE_NAME, "Traverso Live Playback",
		PW_KEY_NODE_DESCRIPTION, "Traverso DAW Live Playback",
		PW_KEY_NODE_LATENCY, latencyStr.constData(),
		PW_KEY_NODE_RATE, rateStr.constData(),
		PW_KEY_NODE_FORCE_RATE, rateStr.constData(),
		PW_KEY_NODE_FORCE_QUANTUM, quantumStr.constData(),
		PW_KEY_NODE_LOCK_QUANTUM, "true",
		PW_KEY_NODE_LOCK_RATE, "true",
		(const char*)nullptr
	);
	if (!props) {
		return nullptr;
	}

	// Target a specific sink when one was chosen; otherwise let PipeWire
	// auto-connect to the default sink.
	if (!target.isEmpty() && target != "default" && target != "none") {
		pw_properties_set(props, PW_KEY_TARGET_OBJECT, target.toUtf8().constData());
	}

	return props;
}

int PipeWireLiveOutput::open(const QString& uid, uint rate, nframes_t bufferSize, uint channels)
{
	if (m_open) {
		return 0;
	}

	if (channels == 0 || bufferSize == 0 || rate == 0) {
		return -1;
	}

	const QString target = (uid.isEmpty() || uid == "default") ? QString() : uid;

	pw_init(nullptr, nullptr);
	m_pwInitialized = true;

	m_threadLoop = pw_thread_loop_new("traverso-live-playback", nullptr);
	if (!m_threadLoop) {
		close();
		return -1;
	}

	m_events.version = PW_VERSION_STREAM_EVENTS;
	m_events.process = on_process;

	struct pw_properties* props = build_properties(rate, bufferSize, target);
	if (!props) {
		close();
		return -1;
	}

	pw_thread_loop_lock(m_threadLoop);

	// pw_stream_new_simple takes ownership of props.
	m_stream = pw_stream_new_simple(
		pw_thread_loop_get_loop(m_threadLoop),
		"Traverso Live Playback",
		props,
		&m_events,
		this
	);
	if (!m_stream) {
		pw_thread_loop_unlock(m_threadLoop);
		close();
		return -1;
	}

	uint8_t buffer[1024];
	struct spa_pod_builder b = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
	struct spa_audio_info_raw info = {};
	info.format = SPA_AUDIO_FORMAT_F32;
	info.channels = channels;
	if (channels == 1) {
		info.position[0] = SPA_AUDIO_CHANNEL_MONO;
	} else if (channels >= 2) {
		info.position[0] = SPA_AUDIO_CHANNEL_FL;
		info.position[1] = SPA_AUDIO_CHANNEL_FR;
	}
	info.rate = rate;

	const struct spa_pod* params[1];
	params[0] = spa_format_audio_raw_build(&b, SPA_PARAM_EnumFormat, &info);

	enum pw_stream_flags flags = static_cast<enum pw_stream_flags>(
		PW_STREAM_FLAG_AUTOCONNECT |
		PW_STREAM_FLAG_MAP_BUFFERS |
		PW_STREAM_FLAG_RT_PROCESS |
		PW_STREAM_FLAG_INACTIVE
	);

	int res = pw_stream_connect(
		m_stream,
		PW_DIRECTION_OUTPUT,
		PW_ID_ANY,
		flags,
		params,
		1
	);

	pw_thread_loop_unlock(m_threadLoop);

	if (res < 0) {
		close();
		return -1;
	}

	if (!ensure_buffers(bufferSize, channels)) {
		close();
		return -1;
	}

	if (pw_thread_loop_start(m_threadLoop) < 0) {
		close();
		return -1;
	}

	m_deviceName = target.isEmpty() ? QString("Default (PipeWire)") : target;
	m_open = true;
	return 0;
}

void PipeWireLiveOutput::close()
{
	// Mark closed first; the rings are kept for the object's lifetime, so a
	// concurrent push can never see freed storage.
	m_open = false;
	stop();

	cleanup();
	m_deviceName.clear();
}

void PipeWireLiveOutput::cleanup()
{
	if (m_threadLoop) {
		pw_thread_loop_stop(m_threadLoop);
	}
	if (m_stream) {
		pw_stream_destroy(m_stream);
		m_stream = nullptr;
	}
	if (m_threadLoop) {
		pw_thread_loop_destroy(m_threadLoop);
		m_threadLoop = nullptr;
	}
	if (m_pwInitialized) {
		pw_deinit();
		m_pwInitialized = false;
	}
}

void PipeWireLiveOutput::start()
{
	if (!is_open() || started() || !m_stream || !m_threadLoop) {
		return;
	}

	reset_buffers();
	request_flush();

	pw_thread_loop_lock(m_threadLoop);
	pw_stream_set_active(m_stream, true);
	pw_thread_loop_unlock(m_threadLoop);

	set_started(true);
}

void PipeWireLiveOutput::stop()
{
	if (!started()) {
		return;
	}

	if (m_stream && m_threadLoop) {
		pw_thread_loop_lock(m_threadLoop);
		pw_stream_set_active(m_stream, false);
		pw_thread_loop_unlock(m_threadLoop);
	}

	set_started(false);
}

void PipeWireLiveOutput::on_process(void* data)
{
	static_cast<PipeWireLiveOutput*>(data)->render();
}

// Called from PipeWire's stream thread. Only drains the ring buffers; it must
// never touch the engine (the primary PipeWireDriver stream is the clock).
void PipeWireLiveOutput::render()
{
	if (!m_stream) {
		return;
	}

	struct pw_buffer* b = pw_stream_dequeue_buffer(m_stream);
	if (!b) {
		return;
	}

	struct spa_buffer* buf = b->buffer;
	if (!buf || buf->n_datas == 0 || !buf->datas[0].data) {
		pw_stream_queue_buffer(m_stream, b);
		return;
	}

	float* dst = static_cast<float*>(buf->datas[0].data);
	const uint channelCount = channels();
	const nframes_t nframes = b->requested > 0
		? static_cast<nframes_t>(b->requested)
		: buffer_size();

	const size_t sampleCount = static_cast<size_t>(nframes) * channelCount;

	if (!started()) {
		std::memset(dst, 0, sampleCount * sizeof(float));
	} else {
		pull_interleaved(nframes, dst);
	}

	if (buf->datas[0].chunk) {
		buf->datas[0].chunk->offset = 0;
		buf->datas[0].chunk->stride = sizeof(float) * channelCount;
		buf->datas[0].chunk->size = sampleCount * sizeof(float);
	}

	pw_stream_queue_buffer(m_stream, b);
}

LiveOutput* create_pipewire_live_output()
{
	return new PipeWireLiveOutput();
}

#endif // PIPEWIRE_SUPPORT
