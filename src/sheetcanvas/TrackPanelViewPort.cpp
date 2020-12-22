/*
    Copyright (C) 2006 Remon Sijrier 
 
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
 
    $Id: TrackPanelViewPort.cpp,v 1.1 2008/01/21 16:17:30 r_sijrier Exp $
*/

#include "TrackPanelViewPort.h"
		
#include "SheetWidget.h"
#include "SheetView.h"
#include "TrackPanelView.h"
#include <ContextPointer.h>
#include <QWheelEvent>
#include <Debugger.h>


TrackPanelViewPort::TrackPanelViewPort(QGraphicsScene* scene, SheetWidget* sw)
	: ViewPort(scene, sw)
{
        setFixedWidth(230);

	m_sw = sw;
        m_sv = sw->get_sheetview();
	
        setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
}

void TrackPanelViewPort::wheelEvent ( QWheelEvent * e )
{
	SheetView* sv = m_sw->get_sheetview();
	
  if (e->orientation() == Qt::Horizontal) {
  	if (e->delta() > 0) {
  		sv->scroll_left_by(e->delta());
  	} else if (e->delta() < 0) {
  		sv->scroll_right_by(-e->delta());
  	}
  }
  else {
    if (e->delta() > 0) {
  		sv->scroll_up_by(e->delta());
  	} else if (e->delta() < 0) {
  		sv->scroll_down_by(-e->delta());
  	}
  }
}

//eof
