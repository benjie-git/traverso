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

#include <algorithm>
#include <cmath>
#include <cstring>
#include <ctime>
#include <iostream>
#include <string>

namespace {

struct DeviceEnumData {
    QStringList devices;
    bool capture = false;
    struct pw_main_loop* loop = nullptr;
};

void enum_registry_global(void* data, uint32_t id, uint32_t permissions,
                          const char* type, uint32_t version,
                          const struct spa_dict* props)
{
    Q_UNUSED(id)
    Q_UNUSED(permissions)
    Q_UNUSED(version)

    DeviceEnumData* enumData = static_cast<DeviceEnumData*>(data);
    if (!props || !type) {
        return;
    }
    if (std::strcmp(type, PW_TYPE_INTERFACE_Node) != 0) {
        return;
    }

    const char* mediaClass = spa_dict_lookup(props, PW_KEY_MEDIA_CLASS);
    if (!mediaClass) {
        return;
    }
    const bool isSink = std::strcmp(mediaClass, "Audio/Sink") == 0;
    const bool isSource = std::strcmp(mediaClass, "Audio/Source") == 0;
    if (enumData->capture ? !isSource : !isSink) {
        return;
    }

    const char* name = spa_dict_lookup(props, PW_KEY_NODE_NAME);
    if (!name || !*name) {
        return;
    }

    const char* description = spa_dict_lookup(props, PW_KEY_NODE_DESCRIPTION);
    const QString label = (description && *description)
        ? QString::fromUtf8(description)
        : QString::fromUtf8(name);
    enumData->devices.append(label + "###" + QString::fromUtf8(name));
}

void enum_registry_global_remove(void* data, uint32_t id)
{
    Q_UNUSED(data)
    Q_UNUSED(id)
}

void enum_core_done(void* data, uint32_t id, int seq)
{
    Q_UNUSED(id)
    Q_UNUSED(seq)
    DeviceEnumData* enumData = static_cast<DeviceEnumData*>(data);
    if (enumData->loop) {
        pw_main_loop_quit(enumData->loop);
    }
}

void enum_timeout(void* data, uint64_t expirations)
{
    Q_UNUSED(expirations)
    DeviceEnumData* enumData = static_cast<DeviceEnumData*>(data);
    if (enumData->loop) {
        pw_main_loop_quit(enumData->loop);
    }
}

} // namespace

QStringList PipeWireDriver::devices_info(bool capture)
{
    QStringList result;

    pw_init(nullptr, nullptr);

    struct pw_main_loop* loop = pw_main_loop_new(nullptr);
    if (!loop) {
        pw_deinit();
        return result;
    }

    struct pw_context* context = pw_context_new(pw_main_loop_get_loop(loop), nullptr, 0);
    if (!context) {
        pw_main_loop_destroy(loop);
        pw_deinit();
        return result;
    }

    struct pw_core* core = pw_context_connect(context, nullptr, 0);
    if (!core) {
        pw_context_destroy(context);
        pw_main_loop_destroy(loop);
        pw_deinit();
        return result;
    }

    DeviceEnumData data;
    data.capture = capture;
    data.loop = loop;

    // Guard against a daemon that never replies to the sync round-trip, which
    // would otherwise freeze the caller (the settings dialog) forever.
    struct spa_source* timer = pw_loop_add_timer(pw_main_loop_get_loop(loop), enum_timeout, &data);
    if (timer) {
        struct timespec value = { 0, 500000000 };
        pw_loop_update_timer(pw_main_loop_get_loop(loop), timer, &value, nullptr, false);
    }

    struct pw_registry_events registryEvents = {};
    registryEvents.version = PW_VERSION_REGISTRY_EVENTS;
    registryEvents.global = enum_registry_global;
    registryEvents.global_remove = enum_registry_global_remove;

    struct pw_core_events coreEvents = {};
    coreEvents.version = PW_VERSION_CORE_EVENTS;
    coreEvents.done = enum_core_done;

    struct spa_hook registryListener = {};
    struct spa_hook coreListener = {};

    struct pw_registry* registry = pw_core_get_registry(core, PW_VERSION_REGISTRY, 0);
    if (registry) {
        pw_registry_add_listener(registry, &registryListener, &registryEvents, &data);
    }
    pw_core_add_listener(core, &coreListener, &coreEvents, &data);

    pw_core_sync(core, PW_ID_CORE, 0);
    pw_main_loop_run(loop);

    result = data.devices;

    if (timer) {
        pw_loop_destroy_source(pw_main_loop_get_loop(loop), timer);
    }
    if (registry) {
        spa_hook_remove(&registryListener);
    }
    spa_hook_remove(&coreListener);
    pw_core_disconnect(core);
    pw_context_destroy(context);
    pw_main_loop_destroy(loop);
    pw_deinit();

    return result;
}

PipeWireDriver::PipeWireDriver(AudioDevice* device, uint rate, nframes_t bufferSize)
    : TAudioDriver(device, rate, bufferSize)
{
    read = MakeDelegate(this, &PipeWireDriver::_read);
    write = MakeDelegate(this, &PipeWireDriver::_write);
    run_cycle = RunCycleCallback(this, &PipeWireDriver::_run_cycle);
}

PipeWireDriver::~PipeWireDriver()
{
    PENTERDES;
    stop();
}

int PipeWireDriver::setup_failed(const QString& message)
{
    PENTER;
    device->driverSetupMessage(message, AudioDevice::DRIVER_SETUP_FAILURE);
    return -1;
}

int PipeWireDriver::setup(bool capture, bool playback, const QString& cardDevice)
{
    PENTER;

    m_enableCapture = capture;
    m_enablePlayback = playback;
    m_cardDevice = cardDevice;

    frame_rate = device->get_sample_rate();
    frames_per_cycle = device->get_buffer_size();
    if (frame_rate == 0) frame_rate = 48000;
    if (frames_per_cycle == 0) frames_per_cycle = 1024;
    capture_frame_latency = playback_frame_latency = 0;

    // FIXME:
    uint32_t channels = 2;

    period_usecs = static_cast<trav_time_t>(
        static_cast<double>(frames_per_cycle) / frame_rate * 1000000.0);

    pw_init(nullptr, nullptr);

    m_pwLoop = pw_loop_new(nullptr);

    if (!m_pwLoop) {
        return setup_failed(tr("Could not create PipeWire Main Loop"));
    }

    uint8_t paramBuffer[1024];
    struct spa_pod_builder b = SPA_POD_BUILDER_INIT(paramBuffer, sizeof(paramBuffer));
    struct spa_audio_info_raw info = {};
    info.format   = SPA_AUDIO_FORMAT_F32P; // Float 32-bit PLANAR (JACK stijl)
    info.rate     = frame_rate;
    info.channels = channels;
    info.flags    = 0;

    for (uint32_t i = 0; i < channels; ++i) {
        info.position[i] = (i == 0) ? SPA_AUDIO_CHANNEL_FL : ((i == 1) ? SPA_AUDIO_CHANNEL_FR : SPA_AUDIO_CHANNEL_UNKNOWN);
    }

    const struct spa_pod *duplexParameter = spa_format_audio_raw_build(&b, SPA_PARAM_EnumFormat, &info);
    const struct spa_pod *streamParameters[] = { duplexParameter };

    if (m_enablePlayback) {
        struct pw_properties *playbackProperties = pw_properties_new(
            "application.name", "Traverso DAW",
            "application.icon-name", "Traverso",
            "media.name", "Traverso Audio Output",
            "media.type", "Audio",
            "media.category", "Playback",
            "media.class", "Stream/Output/Audio",
            "node.name", "TraversoDAW Playback",
            "node.description", "Traverso DAW Playback",

            "node.link-group", "Traverso_DSP_Group",

            "node.force-quantum", std::to_string(frames_per_cycle).c_str(),
            "node.force-rate", std::to_string(frame_rate).c_str(),
            "node.lock-quantum", "true",
            "node.lock-rate", "true",
            nullptr
            );

        if (!m_cardDevice.isEmpty() &&
            m_cardDevice != QStringLiteral("default") &&
            m_cardDevice != QStringLiteral("none")) {
            pw_properties_set(playbackProperties, PW_KEY_TARGET_OBJECT, m_cardDevice.toUtf8().constData());
        }

        std::memset(&m_playbackStreamEvents, 0, sizeof(m_playbackStreamEvents));
        m_playbackStreamEvents.version = PW_VERSION_STREAM_EVENTS;
        m_playbackStreamEvents.process = &PipeWireDriver::_on_process_playback;
        m_playbackStreamEvents.state_changed = &PipeWireDriver::_on_state_changed;

        m_playbackStream = pw_stream_new_simple(
            m_pwLoop,
            "TraversoPlaybackStream",
            playbackProperties,
            &m_playbackStreamEvents,
            this
            );

        if (!m_playbackStream) {
            return setup_failed(tr("Could not create PipeWire Playback Stream"));
        }

        // Connect the playback stream to the graph
        int res = pw_stream_connect(
            m_playbackStream,
            PW_DIRECTION_OUTPUT,
            PW_ID_ANY,
            static_cast<enum pw_stream_flags>(PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_RT_PROCESS),
            streamParameters,
            1
            );

        if (res < 0) {
            return setup_failed(tr("Could not connect playback stream to server"));
        } else {
            device->driverSetupMessage(tr("Playback Stream connected to server"), AudioDevice::DRIVER_SETUP_SUCCESS);
        }
    }

    if (m_enableCapture) {
        struct pw_properties *captureProps = pw_properties_new(
            "application.name", "Traverso DAW",
            "application.icon-name", "Traverso",
            "media.name", "Traverso Audio Input",
            "media.type", "Audio",
            "media.category", "Capture",
            "media.class", "Stream/Input/Audio",
            "node.name", "TraversoDAW Capture",
            "node.description", "Traverso DAW Input",

            "node.link-group", "Traverso_DSP_Group",

            "node.force-quantum", std::to_string(frames_per_cycle).c_str(),
            "node.force-rate", std::to_string(frame_rate).c_str(),
            "node.lock-quantum", "true",
            "node.lock-rate", "true",
            nullptr
            );

        if (!m_cardDevice.isEmpty() &&
            m_cardDevice != QStringLiteral("default") &&
            m_cardDevice != QStringLiteral("none")) {
            pw_properties_set(captureProps, PW_KEY_TARGET_OBJECT, m_cardDevice.toUtf8().constData());
        }

        std::memset(&m_captureStreamEvents, 0, sizeof(m_captureStreamEvents));
        m_captureStreamEvents.version = PW_VERSION_STREAM_EVENTS;
        m_captureStreamEvents.process = &PipeWireDriver::_on_process_capture;
        m_captureStreamEvents.state_changed = &PipeWireDriver::_on_state_changed;

        m_captureStream = pw_stream_new_simple(
            m_pwLoop,
            "TraversoCaptureStream",
            captureProps,
            &m_captureStreamEvents,
            this
            );

        if (!m_captureStream) {
            return setup_failed(tr("Could not create PipeWire Capture Stream"));
        }

        int res = pw_stream_connect(
            m_captureStream,
            PW_DIRECTION_INPUT,
            PW_ID_ANY,
            static_cast<enum pw_stream_flags>(PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_RT_PROCESS),
            streamParameters,
            1
            );

        if (res < 0) {
            return setup_failed(tr("Could not connect capture stream to server"));
        }
    }

    int pipewire_fd = pw_loop_get_fd(m_pwLoop);
    m_notifier = new QSocketNotifier(pipewire_fd, QSocketNotifier::Read, this);
    m_notifier->setEnabled(false);

    connect(m_notifier, &QSocketNotifier::activated, this, &PipeWireDriver::handle_pipewire_events);

    return 1;
}

int PipeWireDriver::attach()
{
    period_usecs = static_cast<trav_time_t>(floor(((float) frames_per_cycle / frame_rate) * 1000000.0f));

    device->set_buffer_size(frames_per_cycle);
    device->set_sample_rate(frame_rate);

    AudioChannel* chan;

    if (m_enableCapture) {
        for (uint chn = 0; chn < 2; chn++) {
            chan = add_capture_channel(QString("capture_%1").arg(chn + 1));
            chan->set_latency(frames_per_cycle + capture_frame_latency);
        }
    }

    if (m_enablePlayback) {
        for (uint chn = 0; chn < 2; chn++) {
            chan = add_playback_channel(QString("playback_%1").arg(chn + 1));
            chan->set_latency(frames_per_cycle + playback_frame_latency);
        }
    }

    return 1;
}

int PipeWireDriver::start()
{
    PENTER;

    if (m_notifier) {
        m_notifier->setEnabled(true);
    }

    m_running.store(1);

    device->driverSetupMessage(tr("Successfully connected to PipeWire server!"), AudioDevice::DRIVER_SETUP_SUCCESS);

    return 1;
}

int PipeWireDriver::stop()
{
    PENTER;

    m_running.store(0);

    if (!m_pwLoop) {
        return 1;
    }

    if (m_notifier) {
        m_notifier->setEnabled(false);
        disconnect(m_notifier, &QSocketNotifier::activated, this, &PipeWireDriver::handle_pipewire_events);
        m_notifier->deleteLater();
        m_notifier = nullptr;
    }
    if (m_playbackStream) {
        pw_stream_destroy(m_playbackStream);
        m_playbackStream = nullptr;
    }
    if (m_captureStream) {
        pw_stream_destroy(m_captureStream);
        m_captureStream = nullptr;
    }
    if (m_pwLoop) {
        pw_loop_destroy(m_pwLoop);
        m_pwLoop = nullptr;
    }
    pw_deinit();

    return 1;
}

int PipeWireDriver::process_callback()
{
    device->run_cycle(frames_per_cycle, 0.0);

    device->transport_cycle_end(get_microseconds());

    return 1;
}

int PipeWireDriver::_read(nframes_t nframes)
{
    Q_UNUSED(nframes)
    // already got data in _on_process_capture() callback
    return 1;
}

int PipeWireDriver::_write(nframes_t nframes)
{
    if (!m_playbackStream) {
        return 1;
    }

    struct pw_buffer* b = pw_stream_dequeue_buffer(m_playbackStream);
    if (!b) {
        return -1;
    }

    struct spa_buffer* buf = b->buffer;
    uint channelCount = static_cast<uint>(m_playbackChannels.size());

    nframes_t available = nframes;
    for (uint chan = 0; chan < channelCount; ++chan) {
        available = std::min(available, static_cast<nframes_t>(m_playbackChannels.at(chan)->get_buffer_size()));
    }

    for (uint chan = 0; chan < channelCount; ++chan) {
        if (chan >= buf->n_datas) {
            break;
        }

        float* dst = static_cast<float*>(buf->datas[chan].data);

        if (dst) {
            std::memcpy(dst, m_playbackChannels.at(chan)->get_buffer(available), available * sizeof(float));
            if (available < nframes) {
                std::memset(dst + available, 0, (nframes - available) * sizeof(float));
            }
        }

        m_playbackChannels.at(chan)->silence_buffer(available);

        if (buf->datas[chan].chunk) {
            buf->datas[chan].chunk->offset = 0;
            buf->datas[chan].chunk->stride = sizeof(float);
            buf->datas[chan].chunk->size = nframes * sizeof(float);
        }
    }

    pw_stream_queue_buffer(m_playbackStream, b);
    return 1;
}

int PipeWireDriver::_run_cycle()
{
    return device->run_cycle(frames_per_cycle, 0);
}

void PipeWireDriver::_on_process_playback(void* userdata)
{
    PipeWireDriver* driver = static_cast<PipeWireDriver*>(userdata);

    driver->process_callback();
}

void PipeWireDriver::_on_process_capture(void* userdata)
{
    PipeWireDriver* driver = static_cast<PipeWireDriver*>(userdata);

    driver->process_capture_callback();
}

int PipeWireDriver::process_capture_callback()
{
    if (!m_captureStream) {
        return 0;
    }

    device->transport_cycle_start(get_microseconds());

    struct pw_buffer* b = pw_stream_dequeue_buffer(m_captureStream);
    if (!b) {
        return 0;
    }

    struct spa_buffer* buf = b->buffer;
    uint channelCount = static_cast<uint>(m_captureChannels.size());

    for (uint chan = 0; chan < channelCount; ++chan) {
        if (chan < buf->n_datas && buf->datas[chan].data) {
            float* src = static_cast<float*>(buf->datas[chan].data);
            m_captureChannels.at(chan)->read_from_hardware_port(src, frames_per_cycle);
        }
    }

    pw_stream_queue_buffer(m_captureStream, b);

    // Without a playback stream, the capture callback is what drives the engine.
    if (!m_enablePlayback && is_running()) {
        process_callback();
    }

    return 1;
}

void PipeWireDriver::_on_state_changed(void* userdata, enum pw_stream_state old_state, enum pw_stream_state state, const char* error)
{
    static_cast<PipeWireDriver*>(userdata)->handle_state_changed(old_state, state, error);
}

void PipeWireDriver::handle_state_changed(enum pw_stream_state old_state, enum pw_stream_state state, const char* error)
{
    Q_UNUSED(old_state)
    PENTER;

    bool shutdown = false;
    if (state == PW_STREAM_STATE_ERROR) {
        fprintf(stderr, "PipeWire stream error: %s\n", error ? error : "unknown");
        shutdown = m_running.exchange(2) != 2;
    } else if (state == PW_STREAM_STATE_UNCONNECTED && m_running.load() == 1) {
        printf("PipeWire stream disconnected\n");
        shutdown = m_running.exchange(2) != 2;
    }

    if (shutdown) {
        emit pipewireShutDown();
    }
}

void PipeWireDriver::handle_pipewire_events()
{
    PENTER;
    if (m_pwLoop) {
        pw_loop_iterate(m_pwLoop, 0);
    }
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
