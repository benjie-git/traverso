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

#ifndef PIPEWIRELIVEOUTPUT_H
#define PIPEWIRELIVEOUTPUT_H

#include "LiveOutput.h"

#include <QString>

#if defined (PIPEWIRE_SUPPORT)

#include <pipewire/pipewire.h>
#include <pipewire/stream.h>
#include <spa/param/audio/format-utils.h>
#include <spa/param/props.h>

/**
 * PipeWireLiveOutput streams the live mix to a second PipeWire sink through a
 * dedicated client, thread loop and output stream, running on that sink's own
 * clock.
 *
 * The live stream is deliberately independent of the primary PipeWireDriver
 * stream: it has its own client and thread loop, and its process callback only
 * drains the shared ring buffers (BufferedLiveOutput). It must never call
 * AudioDevice::run_engine_cycle(); the primary playback stream stays the sole
 * engine clock.
 */
class PipeWireLiveOutput : public BufferedLiveOutput
{
public:
	PipeWireLiveOutput();
	~PipeWireLiveOutput() override;

	bool is_supported() const override { return true; }
	QString device_name() const override { return m_deviceName; }

	int open(const QString& uid, uint rate, nframes_t bufferSize, uint channels) override;
	void close() override;
	void start() override;
	void stop() override;

private:
	void cleanup();
	struct pw_properties* build_properties(uint rate, nframes_t bufferSize, const QString& target) const;
	static void on_process(void* data);
	void render();

	struct pw_thread_loop*  m_threadLoop;
	struct pw_stream*       m_stream;
	struct pw_stream_events m_events;
	QString                 m_deviceName;
	bool                    m_pwInitialized;
};

#endif // PIPEWIRE_SUPPORT

#endif
