/*
Copyright (C) 2005-2010 Remon Sijrier

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

#ifndef SONG_H
#define SONG_H

#include "TSession.h"
#include <QDomNode>
#include <QSet>
#include <QTimer>
#include "defines.h"
#include "APILinkedList.h"

class Project;
class AudioTrack;
class AudioSource;
class WriteSource;
class AudioTrack;
class AudioClip;
class DiskIO;
class AudioClipManager;
class TAudioDeviceClient;
class AudioBus;
class SnapList;
class TimeLine;
class Snappable;
class DecodeBuffer;
class TBusTrack;
class Track;

struct ExportSpecification;

class Sheet : public TSession, public APILinkedListNode
{
	Q_OBJECT

public:

        Sheet(Project* project, int numtracks=0);
	Sheet(Project* project, const QDomNode &node);
	~Sheet();
	
	// Get functions
    int get_audio_track_count() const {return m_audioTracks.size();}

        QString get_artists() const {return m_artists;}
	QDomNode get_state(QDomDocument doc, bool istemplate=false);
        QList<AudioTrack*> get_audio_tracks() const;
	
        Project* get_project() const {return m_project;}
	DiskIO*	get_diskio() const;
	AudioClipManager* get_audioclip_manager() const;
	AudioBus* get_render_bus() const {return m_renderBus;}
	AudioBus* get_clip_render_bus() const {return m_clipRenderBus;}
        AudioTrack* get_audio_track_for_index(int index);
        QString get_audio_sources_dir() const;
        TimeRef get_last_location() const;

	// Set functions
	void set_artists(const QString& pArtistis);
        void set_work_at(TimeRef location, bool isFolder=false);
        void set_work_at_for_sheet_as_track_folder(const TimeRef& location);
	void set_snapping(bool snap);
        int set_state( const QDomNode & node );
	void set_recording(bool recording, bool realtime);
        void set_audio_sources_dir(const QString& dir);

	void skip_to_start();
	void skip_to_end();
	

	int process(nframes_t nframes);
	int render_pass(PlayheadId playhead, nframes_t nframes);
	TimeRef get_render_location(PlayheadId playhead) const;
	// jackd only feature
	int transport_control(transport_state_t state);
	int process_export(nframes_t nframes);
	int prepare_export(ExportSpecification* spec);
	int render(ExportSpecification* spec);
        int start_export(ExportSpecification* spec);

        void solo_track(Track* track);
	void create(int tracksToCreate);
        TCommand* add_track(Track* api, bool historable=true);

        bool any_audio_track_armed();
	bool realtime_path() const {return m_realtimepath;}
        bool is_changed() const {return m_changed;}
	bool is_snap_on() const	{return m_isSnapOn;}
        bool is_recording() const {return m_recording;}
	bool is_smaller_then(APILinkedListNode* node) {Q_UNUSED(node); return false;}

        audio_sample_t*		readbuffer{};
        DecodeBuffer*		renderDecodeBuffer{};

#if defined (THREAD_CHECK)
	unsigned long	threadId;
#endif

private:
        QList<AudioClip*>	m_recordingClips;
	QTimer			m_skipTimer;
	Project*		m_project;
    WriteSource*		m_exportSource{};
        TAudioDeviceClient*	m_audiodeviceClient{};
        AudioBus*		m_renderBus{};
    AudioBus*		m_clipRenderBus{};
    DiskIO*			m_diskio{};
    AudioClipManager*	m_acmanager{};
	QList<TimeRef>		m_xposList;
        QString                 m_audioSourcesDir;

	// The following data could be read/written by multiple threads
	// (gui, audio and m_diskio thread). Therefore they should have 
	// atomic behaviour, still not sure if volatile size_t declaration
	// would suffice, or should we use t_atomic_int_set/get() to make
	// it 100% portable and working on all platforms...?
    volatile size_t		m_transportFrame{};
    volatile size_t		m_newTransportFramePos{};
    volatile size_t		m_seeking{};
    volatile size_t		m_startSeek{};
        volatile size_t		m_stopTransport{};


        QString 	m_artists;
    uint		m_currentSampleRate{};
    bool 		m_rendering{};
        bool 		m_changed{};
    bool		m_resumeTransport{};
    bool		m_realtimepath{};
        bool		m_recording{};
    bool		m_prepareRecording{};
    bool		m_readyToRecord{};

	// Clips locked by the currently running Live transport. They are tracked
	// by id so a clip deleted mid-run cannot leave a dangling pointer, and
	// they are unlocked again when the Live transport is stopped. Clips that
	// already were locked (by the user) are never added here, so stopping the
	// Live transport cannot silently undo a manual lock.
	QSet<qint64>	m_liveAutoLockedClipIds;
	QSet<qint64>	m_liveHandledClipIds;
	
	void init();

	int finish_audio_export();
	void start_seek();
        void initiate_seek_start(TimeRef location);
	void start_transport_rolling(bool realtime);
	void stop_transport_rolling();
	void update_skip_positions();
	
        void resize_buffer(nframes_t size);

	friend class AudioClipManager;

public slots :
	void seek_finished();
        void audiodevice_params_changed();
        void set_gain(float gain);
        void set_transport_pos(TimeRef location);


	TCommand* next_skip_pos();
	TCommand* prev_skip_pos();
	TCommand* start_transport();
	TCommand* set_recordable();
	TCommand* set_recordable_and_start_transport();
	TCommand* toggle_snap();
	void start_live_transport();
	void stop_live_transport();
	void lock_clips_reached_by_live();

signals:
	void seekStart();
	void snapChanged();
	void setCursorAtEdge();
	void recordingStateChanged();
	void prepareRecording();
        void stateChanged();
	
private slots:
	void handle_diskio_writebuffer_overrun();
	void handle_diskio_readbuffer_underrun();
	void prepare_recording();
	void clip_finished_recording(AudioClip* clip);
	void config_changed();
	void update_disk_io_state();
	void update_live_sources_active_state();
	void unlock_live_locked_clips();
};

#endif

//eof
