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
 
*/

#include "Zoom.h"

#include "SheetView.h"
#include "TrackView.h"
#include "Sheet.h"
#include "Track.h"
#include "ClipsViewPort.h"
#include "ContextPointer.h"
#include "TInputEventDispatcher.h"
#include <QPoint>

// Always put me below _all_ includes, this is needed
// in case we run with memory leak detection enabled!
#include "Debugger.h"

Zoom::Zoom(SheetView* sv, const QVariantList& args)
	: TCommand("Zoom")
{
	m_tv = sv->get_trackview_at_scene_pos(cpointer().scene_pos());
	m_sv = sv;
    m_canvasCursorFollowsMouseCursor = false;

	m_jogHorizontal = m_jogVertical = false;

	if (!args.empty()) {
		QString type = args.at(0).toString();
		if (type == "JogZoom") {
			m_jogHorizontal = m_jogVertical = true;
		} else if (type == "HJogZoom") {
			m_jogHorizontal = true;
		} else if (type == "VJogZoom") {
			m_jogVertical = true;
		}
	}
	if (args.size() > 1) {
		m_xScalefactor = args.at(1).toDouble();
	} else {
		m_xScalefactor = 1;
	}
	if (args.size() > 2) {
		m_yScalefactor = args.at(2).toDouble();
	} else {
		m_yScalefactor = 0;
	}

	m_trackHeight = collected_number_to_track_height(ied().get_collected_number());
}

int Zoom::prepare_actions()
{
	return 1;
}


int Zoom::begin_hold()
{
        m_verticalJogZoomLastY = cpointer().mouse_viewport_y();
        m_horizontalJogZoomLastX = cpointer().mouse_viewport_x();
        m_origPos = cpointer().scene_pos();

	return 1;
}


int Zoom::finish_hold()
{
        QCursor::setPos(m_mousePos);
	return -1;
}


void Zoom::set_cursor_shape( int useX, int useY )
{
	Q_UNUSED(useX);
	Q_UNUSED(useY);
	
	if (useX && useY) {
		cpointer().set_canvas_cursor_shape(":/cursorZoom");
	} else if(useX) {
		cpointer().set_canvas_cursor_shape(":/cursorZoomHorizontal");
	} else if (useY) {
		cpointer().set_canvas_cursor_shape(":/cursorZoomVertical");
	}

        m_mousePos = QCursor::pos();
}

int Zoom::jog()
{
        PENTER;
	
	if (m_jogVertical) {
		int y = cpointer().mouse_viewport_y();
                int dy = y - m_verticalJogZoomLastY;
		
		if (abs(dy) > 8) {
                        m_verticalJogZoomLastY = y;
			if (dy > 0) {
				m_sv->vzoom(1 + m_yScalefactor);
			} else {
				m_sv->vzoom(1 - m_yScalefactor);
			}
		}
	} 
	
	if (m_jogHorizontal) {
                int x = cpointer().mouse_viewport_x();
                int dx = x - m_horizontalJogZoomLastX;
		
		if (abs(dx) > 10  /*1*/) {
                        m_horizontalJogZoomLastX = x;
			if (dx > 0) {
                                hzoom_in();
			} else {
                                hzoom_out();
			}
		}
	}

	cpointer().set_canvas_cursor_pos(m_origPos);
	
        return 1;
}

int Zoom::do_action( )
{
	if (m_yScalefactor != 0) {
		m_sv->vzoom(1 + m_yScalefactor);
	}
	if (m_xScalefactor != 1) {
		m_sv->hzoom(m_xScalefactor);
// 		m_sv->center();
	}
	
	return -1;
}

int Zoom::undo_action( )
{
	return -1;
}

void Zoom::vzoom_in()
{

        m_sv->vzoom(1.3);
}

void Zoom::vzoom_out()
{

        m_sv->vzoom(0.7);
}

void Zoom::hzoom_in()
{

        m_sv->hzoom(0.5);
}

void Zoom::hzoom_out()
{

        m_sv->hzoom(2.0);
}


void Zoom::track_vzoom_in()
{


        if (!m_tv) {
                return;
        }

        int trackheight = m_sv->get_track_height(m_tv->get_track());
        trackheight *= 1.3;

        m_sv->set_track_height(m_tv, trackheight);
}

void Zoom::track_vzoom_out()
{


        if (!m_tv) {
                return;
        }

        int trackheight = m_sv->get_track_height(m_tv->get_track());
        trackheight *= 0.7;

        m_sv->set_track_height(m_tv, trackheight);
}

void Zoom::process_collected_number(const QString &collected)
{
        if (!m_tv) {
                return;
        }

        if (collected.isEmpty()) {
                return;
        }

        int number = 0;
        bool ok = false;
        QString cleared = collected;
        cleared = cleared.remove(".").remove("-").remove(",");

        if (cleared.size() >= 1) {
                //FIXME number is not used for anything?
                number = QString(cleared.data()[cleared.size() -1]).toInt(&ok);
        }

        int newHeight = collected_number_to_track_height(collected);
        // - 1 means  full height.
        if (newHeight == -1) {
                m_sv->set_track_height(m_tv, m_sv->get_clips_viewport()->height());
        } else {
                m_sv->set_track_height(m_tv, newHeight);
        }
}


int Zoom::collected_number_to_track_height(const QString& collected) const
{
        int number = 0;
        int trackHeight = Track::INITIAL_HEIGHT;
        bool ok = false;
        QString cleared = collected;
        cleared = cleared.remove(".").remove("-").remove(",");

        if (cleared.size() >= 1) {
                number = QString(cleared.data()[cleared.size() -1]).toInt(&ok);
        } else {
                return -1;
        }

        if (ok && m_tv) {
                switch(number) {
                case 2: trackHeight = 60; break;
                case 3: trackHeight = 100; break;
                case 4: trackHeight = 180; break;
                case 5: trackHeight = 320; break;
                case 6: trackHeight = 640; break;
                case 7: trackHeight = -1; break;
                default: trackHeight = 40;
                }
        }

        return trackHeight;
}


void Zoom::toggle_vertical_horizontal_jog_zoom()
{
	if (m_jogVertical) {
		cpointer().set_canvas_cursor_shape(":/cursorZoomHorizontal");
		cpointer().set_canvas_cursor_text(tr("Vertical Off"), 1000);
		cpointer().set_canvas_cursor_pos(m_origPos);
                m_jogVertical = false;
		m_jogHorizontal = true;
	} else {
		cpointer().set_canvas_cursor_shape(":/cursorZoomVertical");
		cpointer().set_canvas_cursor_text(tr("Vertical On"), 1000);
		cpointer().set_canvas_cursor_pos(m_origPos);
		m_jogVertical = true;
		m_jogHorizontal = false;
	}
}

void Zoom::toggle_expand_all_tracks()
{
	m_sv->toggle_expand_all_tracks(-1);
}

