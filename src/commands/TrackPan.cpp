/*
    Copyright (C) 2005-2007 Remon Sijrier 
 
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

#include <libtraversocore.h>

#include "TrackPan.h"
#include <ViewPort.h>
		
// Always put me below _all_ includes, this is needed
// in case we run with memory leak detection enabled!
#include "Debugger.h"

TrackPan::TrackPan(Track* track)
	: Command(track, tr("Track Pan"))
{
        m_track = track;
        m_song = track->get_song();
}


int TrackPan::prepare_actions()
{
        return 1;
}


int TrackPan::begin_hold()
{
        origX = cpointer().x();
        origPan = m_track->get_pan();

        return 1;
}


int TrackPan::finish_hold()
{
	return 1;
}


int TrackPan::do_action()
{
        m_track->set_pan(newPan);
        return 1;
}


int TrackPan::undo_action()
{
        m_track->set_pan(origPan);
        return 1;
}


int TrackPan::jog()
{
        float w = 600.0;
        float ofx = (float) origX - cpointer().x();
        float p = -2.0f *  (ofx) / w ;
        newPan = p + origPan;
        m_track->set_pan( newPan );
        return 1;
}

// eof

