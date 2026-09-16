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

#include "AudioClipDualTrim.h"

#include "AudioClip.h"
#include "AudioTrack.h"

#include "ContextPointer.h"
#include "SheetView.h"

// Allow dual-trimming clips with edges within 10 sec of eachother
static const qint64 DUAL_TRIM_EDGE_TOLERANCE = 10 * UNIVERSAL_SAMPLE_RATE;

AudioClipDualTrim::AudioClipDualTrim(SheetView* sv, AudioTrack* audioTrack)
    : TMoveCommand(sv, audioTrack, tr("AudioClip Dual Trim"))
    , m_audioTrack(audioTrack)
{
}

int AudioClipDualTrim::prepare_actions()
{
    return 1;
}

int AudioClipDualTrim::do_action()
{
    TimeRef delta = m_newLocation - m_origLocationLeft;
    m_leftAudioClip->set_right_edge(m_origLocationLeft + delta);
    m_rightAudioClip->set_left_edge(m_origLocationRight + delta);

    return 1;
}

int AudioClipDualTrim::undo_action()
{
    m_leftAudioClip->set_right_edge(m_origLocationLeft);
    m_rightAudioClip->set_left_edge(m_origLocationRight);

    return 1;
}

int AudioClipDualTrim::begin_hold()
{
    TimeRef cursorLocation(cpointer().on_first_input_event_scene_x() * d->sv->timeref_scalefactor);

    AudioClip* pointedAudioClip = m_audioTrack->get_clip_at_location(cursorLocation);
    if (!pointedAudioClip) {
        set_canvas_cursor_text(tr("Dual Trim: No AudioClip at this location"));
        return -1;
    }

    AudioClip* audioClipBefore = m_audioTrack->get_audio_clip_before(pointedAudioClip);
    AudioClip* audioClipAfter = m_audioTrack->get_audio_clip_after(pointedAudioClip);

    bool leftEdgeCanTrim = false;
    bool rightEdgeCanTrim = false;
    bool trimLeftEdge = false;
    bool trimRightEdge = false;
    if (audioClipBefore &&
        qAbs(audioClipBefore->get_track_end_location().universal_frame() -
             pointedAudioClip->get_track_start_location().universal_frame()) <= DUAL_TRIM_EDGE_TOLERANCE) {
        leftEdgeCanTrim = true;
    }
    if (audioClipAfter &&
        qAbs(audioClipAfter->get_track_start_location().universal_frame() -
             pointedAudioClip->get_track_end_location().universal_frame()) <= DUAL_TRIM_EDGE_TOLERANCE) {
        rightEdgeCanTrim = true;
    }

    if (leftEdgeCanTrim && rightEdgeCanTrim) {
        TimeRef leftEdgeDistance = cursorLocation - pointedAudioClip->get_track_start_location();
        TimeRef rightEdgeDistance = pointedAudioClip->get_track_end_location() - cursorLocation;
        if (leftEdgeDistance < rightEdgeDistance) {
            trimLeftEdge = true;
        } else {
            trimRightEdge = true;
        }
    } else if (leftEdgeCanTrim) {
        trimLeftEdge = true;
    } else if (rightEdgeCanTrim) {
        trimRightEdge = true;
    }

    if (!(trimLeftEdge || trimRightEdge)) {
        set_canvas_cursor_text(tr("Dual Trim: No AudioClip before/after this one"));
        return -1;
    }

    if (trimLeftEdge) {
        m_leftAudioClip = audioClipBefore;
        m_rightAudioClip = pointedAudioClip;
    }
    if (trimRightEdge) {
        m_leftAudioClip = pointedAudioClip;
        m_rightAudioClip = audioClipAfter;
    }

    TimeRef leftExtendableRight = m_leftAudioClip->get_source_length() - m_leftAudioClip->get_source_end_location();
    TimeRef rightExtendableLeft = m_rightAudioClip->get_source_start_location();
    if (leftExtendableRight <= TimeRef() || rightExtendableLeft <= TimeRef()) {
        set_canvas_cursor_text(tr("Dual Trim: Edges at max and min length"));
        return -1;
    }

    m_origLocationLeft = m_leftAudioClip->get_track_end_location();
    m_origLocationRight = m_rightAudioClip->get_track_start_location();
    m_newLocation = m_origLocationLeft;

    return 1;
}

int AudioClipDualTrim::finish_hold()
{
    return 1;
}

void AudioClipDualTrim::cancel_action()
{
    undo_action();
}

int AudioClipDualTrim::jog()
{
    m_newLocation = TimeRef(cpointer().scene_x() * d->sv->timeref_scalefactor);

    cpointer().set_canvas_cursor_pos(cpointer().on_first_input_event_scene_pos());

    do_action();

    return 1;
}

void AudioClipDualTrim::set_canvas_cursor_text(const QString& text)
{
    cpointer().set_canvas_cursor_text(text, 2000);
}
