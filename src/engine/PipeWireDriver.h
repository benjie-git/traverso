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

#ifndef PIPEWIREDRIVER_H
#define PIPEWIREDRIVER_H

#if defined (PIPEWIRE_SUPPORT)

#include "TAudioDriver.h"
#include "defines.h"
#include "RingBufferNPT.h"

#include <pipewire/pipewire.h>
#include <pipewire/stream.h>
#include <spa/param/audio/format-utils.h>
#include <spa/param/props.h>

#include <QObject>
#include <atomic>
#include <memory>

class PipeWireDriver : public TAudioDriver
{
    Q_OBJECT
public:
    explicit PipeWireDriver(AudioDevice* device, uint rate, nframes_t bufferSize);
    ~PipeWireDriver() override;

    int _run_cycle() override { return 1; }
    int setup(bool capture = true, bool playback = true, const QString& cardDevice = "");
    int attach() override;
    int start() override;
    int stop() override;

    QString get_device_name() override;
    QString get_device_longname() override;

    bool is_running() const { return m_running.load() == 1; }

    bool supports_software_channels() override {
        return false;
    }

private:
    std::atomic<size_t>                 m_running{0};
    struct pw_thread_loop*              m_threadLoop{nullptr};
    struct pw_stream*                   m_playbackStream{nullptr};
    struct pw_stream*                   m_captureStream{nullptr};
    struct pw_stream_events             m_playbackEvents{};
    struct pw_stream_events             m_captureEvents{};

    struct spa_io_position*             m_ioPosition{nullptr};

    bool                                m_enableCapture{true};
    bool                                m_enablePlayback{true};
    QString                             m_cardDevice;
    bool                                m_pwInitialized{false};

    std::unique_ptr<RingBufferNPT<audio_sample_t>> m_captureRingBuffer;
    std::unique_ptr<audio_sample_t[]>              m_captureProcessBuffer;

    void run_engine_cycle(nframes_t nframes);
    void drain_capture_ringbuffer(nframes_t nframes);
    void cleanup();
    int fail_setup(const QString& message);
    struct pw_stream* create_stream(
        const char* streamName,
        const char* nodeName,
        const char* nodeDescription,
        const char* mediaCategory,
        enum pw_direction direction,
        uint32_t channelCount,
        const struct pw_stream_events* events
    );
    void on_stream_state_changed(const char* streamName, enum pw_stream_state oldState, enum pw_stream_state state, const char *error);

    static void _on_playback_destroy(void *data);
    static void _on_playback_state_changed(void *data, enum pw_stream_state oldState, enum pw_stream_state state, const char *error);
    static void _on_playback_process(void *data);

    static void _on_capture_destroy(void *data);
    static void _on_capture_state_changed(void *data, enum pw_stream_state oldState, enum pw_stream_state state, const char *error);
    static void _on_capture_process(void *data);

    static void _on_io_changed(void *data, uint32_t id, void *area, uint32_t size);
    static void _on_param_changed(void *data, uint32_t id, const struct spa_pod *param);

signals:
    void pipewireShutDown();
};

#endif // PIPEWIRE_SUPPORT

#endif // PIPEWIREDRIVER_H
