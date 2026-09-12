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


#include "PipeWireDriver.h"

#if defined (PIPEWIRE_SUPPORT)

#include "AudioDevice.h"
#include "AudioChannel.h"
#include "Debugger.h"

#include <cstring>
#include <cstdlib>

PipeWireDriver::PipeWireDriver(AudioDevice* device, uint rate, nframes_t bufferSize)
    : TAudioDriver(device, rate, bufferSize)
{
}

PipeWireDriver::~PipeWireDriver()
{
    PENTER;
    stop();
    cleanup();
}

void PipeWireDriver::cleanup()
{
    if (m_threadLoop) {
        pw_thread_loop_stop(m_threadLoop);
    }
    if (m_playbackStream) {
        pw_stream_destroy(m_playbackStream);
        m_playbackStream = nullptr;
    }
    if (m_captureStream) {
        pw_stream_destroy(m_captureStream);
        m_captureStream = nullptr;
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

int PipeWireDriver::fail_setup(const QString& message)
{
    cleanup();
    device->driverSetupMessage(message, AudioDevice::DRIVER_SETUP_FAILURE);
    return -1;
}

struct pw_stream* PipeWireDriver::create_stream(
    const char* streamName,
    const char* nodeName,
    const char* nodeDescription,
    const char* mediaCategory,
    enum pw_direction direction,
    uint32_t channelCount,
    const struct pw_stream_events* events)
{
    QByteArray latencyStr = QString("%1/%2").arg(frames_per_cycle).arg(frame_rate).toUtf8();
    QByteArray quantumStr = QString::number(frames_per_cycle).toUtf8();
    QByteArray rateStr = QString("1/%1").arg(frame_rate).toUtf8();

    struct pw_properties* props = pw_properties_new(
        PW_KEY_MEDIA_TYPE, "Audio",
        PW_KEY_MEDIA_CATEGORY, mediaCategory,
        PW_KEY_MEDIA_ROLE, "Production",
        PW_KEY_APP_NAME, "Traverso",
        PW_KEY_NODE_NAME, nodeName,
        PW_KEY_NODE_DESCRIPTION, nodeDescription,
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

    if (!m_cardDevice.isEmpty()) {
        pw_properties_set(props, PW_KEY_TARGET_OBJECT, m_cardDevice.toUtf8().constData());
    }

    struct pw_stream* stream = pw_stream_new_simple(
        pw_thread_loop_get_loop(m_threadLoop),
        streamName,
        props,
        events,
        this
    );
    if (!stream) {
        return nullptr;
    }

    uint8_t buffer[1024];
    struct spa_pod_builder b = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
    struct spa_audio_info_raw info = {};
    info.format = SPA_AUDIO_FORMAT_F32;
    info.channels = channelCount;
    if (channelCount == 1) {
        info.position[0] = SPA_AUDIO_CHANNEL_MONO;
    } else if (channelCount >= 2) {
        info.position[0] = SPA_AUDIO_CHANNEL_FL;
        info.position[1] = SPA_AUDIO_CHANNEL_FR;
    }
    info.rate = frame_rate;

    const struct spa_pod* params[1];
    params[0] = spa_format_audio_raw_build(&b, SPA_PARAM_EnumFormat, &info);

    enum pw_stream_flags flags = static_cast<enum pw_stream_flags>(
        PW_STREAM_FLAG_AUTOCONNECT |
        PW_STREAM_FLAG_MAP_BUFFERS |
        PW_STREAM_FLAG_RT_PROCESS |
        PW_STREAM_FLAG_INACTIVE
    );

    int res = pw_stream_connect(
        stream,
        direction,
        PW_ID_ANY,
        flags,
        params,
        1
    );
    if (res < 0) {
        pw_stream_destroy(stream);
        return nullptr;
    }

    return stream;
}

int PipeWireDriver::setup(bool capture, bool playback, const QString& cardDevice)
{
    PENTER;

    cleanup();

    m_enableCapture = capture;
    m_enablePlayback = playback;
    frame_rate = device->get_sample_rate();
    frames_per_cycle = device->get_buffer_size();
    if (frame_rate == 0) frame_rate = 48000;
    if (frames_per_cycle == 0) frames_per_cycle = 1024;
    m_cardDevice = cardDevice;
    m_ioPosition = nullptr;
    capture_frame_latency = playback_frame_latency = 0;

    period_usecs = static_cast<trav_time_t>(
        static_cast<double>(frames_per_cycle) / frame_rate * 1000000.0);

    if (m_enableCapture) {
        for (uint chn = 0; chn < 2; ++chn) {
            AudioChannel* chan = add_capture_channel(QString("capture_%1").arg(chn + 1));
            chan->set_latency(frames_per_cycle + capture_frame_latency);
        }

        m_captureRingBuffer = std::make_unique<RingBufferNPT<audio_sample_t>>(
            static_cast<size_t>(frames_per_cycle) * m_captureChannels.size() * 8);
        m_captureProcessBuffer = std::make_unique<audio_sample_t[]>(
            static_cast<size_t>(frames_per_cycle) * m_captureChannels.size());
    }

    if (m_enablePlayback) {
        for (uint chn = 0; chn < 2; ++chn) {
            AudioChannel* chan = add_playback_channel(QString("playback_%1").arg(chn + 1));
            chan->set_latency(frames_per_cycle + playback_frame_latency);
        }
    }

    printf("Connecting to the PipeWire server...\n");

    pw_init(nullptr, nullptr);
    m_pwInitialized = true;

    m_threadLoop = pw_thread_loop_new("traverso-pipewire", nullptr);
    if (!m_threadLoop) {
        return fail_setup(tr("Couldn't create PipeWire thread loop"));
    }

    m_playbackEvents.version = PW_VERSION_STREAM_EVENTS;
    m_playbackEvents.destroy = _on_playback_destroy;
    m_playbackEvents.state_changed = _on_playback_state_changed;
    m_playbackEvents.io_changed = _on_io_changed;
    m_playbackEvents.param_changed = _on_param_changed;
    m_playbackEvents.process = _on_playback_process;

    m_captureEvents.version = PW_VERSION_STREAM_EVENTS;
    m_captureEvents.destroy = _on_capture_destroy;
    m_captureEvents.state_changed = _on_capture_state_changed;
    m_captureEvents.io_changed = _on_io_changed;
    m_captureEvents.param_changed = _on_param_changed;
    m_captureEvents.process = _on_capture_process;

    pw_thread_loop_lock(m_threadLoop);

    if (m_enablePlayback) {
        m_playbackStream = create_stream(
            "Traverso Playback",
            "Traverso",
            "Traverso DAW",
            "Playback",
            PW_DIRECTION_OUTPUT,
            static_cast<uint32_t>(m_playbackChannels.size()),
            &m_playbackEvents
        );
        if (!m_playbackStream) {
            pw_thread_loop_unlock(m_threadLoop);
            return fail_setup(tr("Couldn't create PipeWire playback stream"));
        }
    }

    if (m_enableCapture) {
        m_captureStream = create_stream(
            "Traverso Capture",
            "Traverso Capture",
            "Traverso DAW Capture",
            "Capture",
            PW_DIRECTION_INPUT,
            static_cast<uint32_t>(m_captureChannels.size()),
            &m_captureEvents
        );
        if (!m_captureStream) {
            pw_thread_loop_unlock(m_threadLoop);
            return fail_setup(tr("Couldn't create PipeWire capture stream"));
        }
    }

    pw_thread_loop_unlock(m_threadLoop);

    if (pw_thread_loop_start(m_threadLoop) < 0) {
        return fail_setup(tr("Failed to start PipeWire thread loop"));
    }

    device->driverSetupMessage(tr("Successfully connected to PipeWire server!"), AudioDevice::DRIVER_SETUP_SUCCESS);

    return 1;
}

int PipeWireDriver::attach()
{
    device->set_buffer_size(frames_per_cycle);
    device->set_sample_rate(frame_rate);
    return 1;
}

int PipeWireDriver::start()
{
    PENTER;
    if (!m_threadLoop) {
        return -1;
    }

    // silence playback buffers
    TAudioDriver::start();

    if (m_captureRingBuffer) {
        m_captureRingBuffer->reset();
    }

    m_running.store(1);

    pw_thread_loop_lock(m_threadLoop);
    if (m_playbackStream) {
        pw_stream_set_active(m_playbackStream, true);
    }
    if (m_captureStream) {
        pw_stream_set_active(m_captureStream, true);
    }
    pw_thread_loop_unlock(m_threadLoop);

    device->driverSetupMessage(tr("Successfully connected to PipeWire server!"), AudioDevice::DRIVER_SETUP_SUCCESS);

    return 1;
}

int PipeWireDriver::stop()
{
    PENTER;
    m_running.store(0);

    if (m_threadLoop) {
        pw_thread_loop_lock(m_threadLoop);
        if (m_playbackStream) {
            pw_stream_set_active(m_playbackStream, false);
        }
        if (m_captureStream) {
            pw_stream_set_active(m_captureStream, false);
        }
        pw_thread_loop_unlock(m_threadLoop);
    }

    // silence capture channels
    TAudioDriver::stop();

    return 1;
}

void PipeWireDriver::run_engine_cycle(nframes_t nframes)
{
    device->transport_cycle_start(get_microseconds());

    device->run_cycle(nframes, 0.0);

    device->transport_cycle_end(get_microseconds());
}

// Pull samples as needed from the ring buffer, on the traverso engine's schedule
void PipeWireDriver::drain_capture_ringbuffer(nframes_t nframes)
{
    const uint channelCount = static_cast<uint>(m_captureChannels.size());
    if (!channelCount || !m_captureRingBuffer || !m_captureProcessBuffer) {
        return;
    }

    const size_t needed = static_cast<size_t>(nframes) * channelCount;
    const size_t readSamples = m_captureRingBuffer->read(m_captureProcessBuffer.get(), needed);
    if (readSamples < needed) {
        std::memset(m_captureProcessBuffer.get() + readSamples, 0,
                    (needed - readSamples) * sizeof(audio_sample_t));
    }

    for (uint chan = 0; chan < channelCount; ++chan) {
        m_captureChannels.at(chan)->read_from_hardware_port_interleaved(
            m_captureProcessBuffer.get(), nframes, channelCount, chan);
    }
}

void PipeWireDriver::_on_playback_destroy(void *data)
{
    static_cast<PipeWireDriver*>(data)->m_playbackStream = nullptr;
}

void PipeWireDriver::on_stream_state_changed(const char* streamName, enum pw_stream_state oldState, enum pw_stream_state state, const char *error)
{
    // printf("PipeWire %s stream state: %s -> %s\n", streamName, pw_stream_state_as_string(oldState), pw_stream_state_as_string(state));

    bool shutdown = false;
    if (state == PW_STREAM_STATE_ERROR) {
        printf("PipeWire %s stream error: %s\n", streamName, error ? error : "unknown");
        shutdown = m_running.exchange(2) != 2;
    } else if (state == PW_STREAM_STATE_UNCONNECTED && m_running.load() == 1) {
        printf("PipeWire %s stream disconnected\n", streamName);
        shutdown = m_running.exchange(2) != 2;
    }

    if (shutdown) {
        emit pipewireShutDown();
    }
}

void PipeWireDriver::_on_playback_state_changed(void *data, enum pw_stream_state oldState, enum pw_stream_state state, const char *error)
{
    static_cast<PipeWireDriver*>(data)->on_stream_state_changed("playback", oldState, state, error);
}

void PipeWireDriver::_on_playback_process(void *data)
{
    PipeWireDriver* driver = static_cast<PipeWireDriver*>(data);
    if (!driver->m_playbackStream) {
        return;
    }

    struct pw_buffer* b = pw_stream_dequeue_buffer(driver->m_playbackStream);
    if (!b) {
        return;
    }

    struct spa_buffer* buf = b->buffer;
    float* dst = static_cast<float*>(buf->datas[0].data);
    uint channelCount = static_cast<uint>(driver->m_playbackChannels.size());
    nframes_t nframes = driver->frames_per_cycle;

    if (dst && channelCount > 0) {
        uint32_t stride = sizeof(float) * channelCount;
        size_t sampleCount = nframes * channelCount;

        if (!driver->is_running()) {
            std::memset(dst, 0, sampleCount * sizeof(float));
        } else {
            if (driver->m_enableCapture) {
                driver->drain_capture_ringbuffer(nframes);
            }

            driver->run_engine_cycle(nframes);

            for (nframes_t frame = 0; frame < nframes; ++frame) {
                for (uint chan = 0; chan < channelCount; ++chan) {
                    dst[frame * channelCount + chan] = driver->m_playbackChannels.at(chan)->get_buffer(nframes)[frame];
                }
            }

            for (uint chan = 0; chan < channelCount; ++chan) {
                driver->m_playbackChannels.at(chan)->silence_buffer(nframes);
            }
        }

        if (buf->datas[0].chunk) {
            buf->datas[0].chunk->offset = 0;
            buf->datas[0].chunk->stride = stride;
            buf->datas[0].chunk->size = sampleCount * sizeof(float);
        }
    }

    pw_stream_queue_buffer(driver->m_playbackStream, b);
}

void PipeWireDriver::_on_capture_destroy(void *data)
{
    static_cast<PipeWireDriver*>(data)->m_captureStream = nullptr;
}

void PipeWireDriver::_on_capture_state_changed(void *data, enum pw_stream_state oldState, enum pw_stream_state state, const char *error)
{
    static_cast<PipeWireDriver*>(data)->on_stream_state_changed("capture", oldState, state, error);
}

// Called by PipeWire to give us captured samples.  We add them to the ringbuffer.
void PipeWireDriver::_on_capture_process(void *data)
{
    PipeWireDriver* driver = static_cast<PipeWireDriver*>(data);
    if (!driver->m_captureStream) {
        return;
    }

    struct pw_buffer* b = pw_stream_dequeue_buffer(driver->m_captureStream);
    if (!b) {
        return;
    }

    struct spa_buffer* buf = b->buffer;
    float* src = static_cast<float*>(buf->datas[0].data);
    uint channelCount = static_cast<uint>(driver->m_captureChannels.size());

    if (src && channelCount > 0 && driver->is_running() && driver->m_captureRingBuffer) {
        uint32_t actualFrames = driver->frames_per_cycle;
        if (buf->datas[0].chunk && buf->datas[0].chunk->size > 0) {
            actualFrames = buf->datas[0].chunk->size / (sizeof(float) * channelCount);
        }

        const size_t samples = static_cast<size_t>(actualFrames) * channelCount;
        if (driver->m_captureRingBuffer->write_space() < samples) {
            driver->m_captureRingBuffer->increment_read_ptr(samples - driver->m_captureRingBuffer->write_space());
        }
        driver->m_captureRingBuffer->write(reinterpret_cast<audio_sample_t*>(src), samples);

        if (!driver->m_enablePlayback) {
            driver->drain_capture_ringbuffer(driver->frames_per_cycle);
            driver->run_engine_cycle(driver->frames_per_cycle);
        }
    }

    pw_stream_queue_buffer(driver->m_captureStream, b);
}

void PipeWireDriver::_on_io_changed(void *data, uint32_t id, void *area, uint32_t size)
{
    Q_UNUSED(size);
    PipeWireDriver* driver = static_cast<PipeWireDriver*>(data);
    if (id == SPA_IO_Position) {
        driver->m_ioPosition = static_cast<struct spa_io_position*>(area);
    }
}

void PipeWireDriver::_on_param_changed(void *data, uint32_t id, const struct spa_pod *param)
{
    Q_UNUSED(data);
    if (!param || id != SPA_PARAM_Format) {
        return;
    }

    struct spa_audio_info_raw info = {};
    if (spa_format_audio_raw_parse(param, &info) < 0) {
        return;
    }

    // printf("PipeWire negotiated format: rate=%u channels=%u\n", info.rate, info.channels);
}

QString PipeWireDriver::get_device_name()
{
    return "PipeWire";
}

QString PipeWireDriver::get_device_longname()
{
    return "PipeWire Audio Server";
}

#endif // PIPEWIRE_SUPPORT
