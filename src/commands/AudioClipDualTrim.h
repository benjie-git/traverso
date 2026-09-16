/*
    Copyright (C) 2024 Remon Sijrier

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

#ifndef AUDIOCLIPDUALTRIM_H
#define AUDIOCLIPDUALTRIM_H

#include "TMoveCommand.h"
#include "defines.h"

class SheetView;
class ContextItem;
class AudioClip;
class AudioTrack;

class AudioClipDualTrim : public TMoveCommand
{
    Q_OBJECT
public:
    AudioClipDualTrim(SheetView* sv, AudioTrack* audioTrack);
    ~AudioClipDualTrim() {}

    int prepare_actions();
    int do_action();
    int undo_action();

    int begin_hold();
    int finish_hold();
    void cancel_action();

    int jog();

private:
    void set_canvas_cursor_text(const QString& text);

    AudioTrack* m_audioTrack{nullptr};
    AudioClip* m_leftAudioClip{nullptr};
    AudioClip* m_rightAudioClip{nullptr};
    TimeRef m_origLocationLeft;
    TimeRef m_origLocationRight;
    TimeRef m_newLocation;
};

#endif // AUDIOCLIPDUALTRIM_H
