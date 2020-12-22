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
 
    $Id: TrackPanelViewPort.h,v 1.1 2008/01/21 16:17:30 r_sijrier Exp $
*/

#ifndef TRACK_PANEL_VIEW_PORT_H
#define TRACK_PANEL_VIEW_PORT_H

#include "ViewPort.h"
		
class SheetWidget;
		
class TrackPanelViewPort : public ViewPort
{
public:
	TrackPanelViewPort(QGraphicsScene* scene, SheetWidget* sw);
	~TrackPanelViewPort() {};

protected:
	void wheelEvent ( QWheelEvent * e );

private:
	SheetWidget*	m_sw;
};

#endif

//eof

 
 
 
 
