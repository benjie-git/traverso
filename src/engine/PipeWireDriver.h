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

#include <pipewire/pipewire.h>
#include <pipewire/stream.h>
#include <pipewire/loop.h>
#include <pipewire/main-loop.h>
#include <pipewire/core.h>
#include <spa/param/audio/format-utils.h>
#include <spa/param/audio/layout.h>

#include <QObject>
#include <QStringList>
#include <QSocketNotifier>
#include <atomic>

class PipeWireDriver : public TAudioDriver
{
    Q_OBJECT
public:
    explicit PipeWireDriver(AudioDevice* device, uint rate, nframes_t bufferSize);
    ~PipeWireDriver() override;

    int process_callback();
    int process_capture_callback();

    int setup(bool capture = true, bool playback = true, const QString& cardDevice = "");
    int attach() override;
    int start() override;
    int stop() override;

    QString get_device_name() override;
    QString get_device_longname() override;

    static QStringList devices_info(bool capture = false);

    bool is_running() const { return m_running.load() == 1; }

    bool supports_software_channels() override {
        return false;
    }

protected:
    int _run_cycle() override;
    int _read(nframes_t nframes) override;
    int _write(nframes_t nframes) override;

private:
    std::atomic<size_t>                 m_running{0};

    struct pw_loop*                     m_pwLoop{nullptr};
    struct pw_stream*                   m_playbackStream{nullptr};
    struct pw_stream_events             m_playbackStreamEvents{};
    struct pw_stream*                   m_captureStream{nullptr};
    struct pw_stream_events             m_captureStreamEvents{};

    QSocketNotifier*                    m_notifier{nullptr};

    bool                                m_enableCapture{true};
    bool                                m_enablePlayback{true};
    QString                             m_cardDevice;

    int setup_failed(const QString& message);

    // callback functions for pipewire
    static void _on_process_playback(void* userdata);
    static void _on_process_capture(void* userdata);
    static void _on_state_changed(void* userdata, enum pw_stream_state old_state, enum pw_stream_state state, const char* error);

    void handle_state_changed(enum pw_stream_state old_state, enum pw_stream_state state, const char* error);

private slots:
    void handle_pipewire_events();

signals:
    void pipewireShutDown();
};

#endif // PIPEWIRE_SUPPORT

#endif // PIPEWIREDRIVER_H
