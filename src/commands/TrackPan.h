/*
    Copyright (C) 2005-2006 Remon Sijrier 
 
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
 
    $Id: TrackPan.h,v 1.4 2007/03/16 00:09:43 r_sijrier Exp $
*/

#ifndef TRACKPAN_H
#define TRACKPAN_H

#include "Command.h"

class Song;
class Track;

class TrackPan : public Command
{
public :
        TrackPan(Track* track);

        int begin_hold();
        int finish_hold();
        int prepare_actions();
        int do_action();
        int undo_action();

        int jog();

private :
        float origPan;
        float newPan;
        int origX;

        Song* m_song;
        Track* m_track;
};

#endif


