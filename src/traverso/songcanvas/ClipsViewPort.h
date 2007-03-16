/*
    Copyright (C) 2006-2007 Remon Sijrier 
 
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

#ifndef CLIPS_VIEW_PORT_H
#define CLIPS_VIEW_PORT_H

#include <QGraphicsScene>
#include <QGraphicsItem>
#include <QStyleOptionGraphicsItem>

#include "ViewPort.h"

class SongWidget;
		
class ClipsViewPort : public ViewPort
{
	Q_OBJECT

public:
	ClipsViewPort(QGraphicsScene* scene, SongWidget* sw);
	~ClipsViewPort() {};
	
        void get_pointed_context_items(QList<ContextItem* > &list);

protected:
        void resizeEvent(QResizeEvent* e);
	void paintEvent( QPaintEvent* e);

private:
	SongWidget*	m_sw;
};


#endif

//eof
