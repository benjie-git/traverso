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

$Id: ViewPort.cpp,v 1.7 2007/03/06 15:14:16 r_sijrier Exp $
*/

#include <QMouseEvent>
#include <QResizeEvent>
#include <QEvent>
#include <QRect>
#include <QPainter>
#include <QPixmap>
#include <QGraphicsScene>
#include <QStyleOptionGraphicsItem>
#include <Utils.h>

#include "ViewPort.h"
#include "ContextPointer.h"

#include "Import.h"

// Always put me below _all_ includes, this is needed
// in case we run with memory leak detection enabled!
#include "Debugger.h"


/**
 * \class ViewPort
 * \brief An Interface class to create Contextual, or so called 'Soft Selection' enabled Widgets.

	The ViewPort class inherits QGraphicsView, and thus is a true Canvas type of Widget.<br />
	Reimplement ViewPort to create a 'Soft Selection' enabled widget. You have to create <br />
	a QGraphicsScene object yourself, and set it as the scene the ViewPort visualizes.

	ViewPort should be used to visualize 'core' data objects. This is done by creating a <br />
	ViewItem object for each core class that has to be visualized. The naming convention <br />
	for classes that inherit ViewItem is: core class name + View.<br />
	E.g. the ViewItem class that represents an AudioClip should be named AudioClipView.

	All keyboard and mouse events by default are propagated to the InputEngine, which in <br />
	turn will parse the events. In case the event sequence was recognized by the InputEngine <br />
	it will ask a list of (pointed) ContextItem's from ContextPointer, which in turns <br />
	call's the pure virtual function get_pointed_context_items(), which you have to reimplement.<br />
	In the reimplemented function, you have to fill the supplied list with ViewItems that are <br />
	under the mouse cursor, and if needed, ViewItem's that _always_ have to be taken into account. <br />
	One can use the convenience functions of QGraphicsView for getting ViewItem's under the mouse cursor!

	Since there can be a certain delay before a key sequence has been verified, the ContextPointer <br />
	stores the position of the first event of a new key fact. This improves the pointed ViewItem <br />
	detection a lot in case the mouse is moved during a key sequence.<br />
	You should use these x/y coordinates in the get_pointed_context_items() function, see:<br />
	ContextPointer::on_first_input_event_x(), ContextPointer::on_first_input_event_y()


 *	\sa ContextPointer, InputEngine
 */



ViewPort::ViewPort(QWidget* parent)
	: QGraphicsView(parent)
{
	PENTERCONS;
	setFrameStyle(QFrame::NoFrame);
	setAlignment(Qt::AlignLeft | Qt::AlignTop);
        
	setAttribute(Qt::WA_OpaquePaintEvent);
}

ViewPort::ViewPort(QGraphicsScene* scene, QWidget* parent)
	: QGraphicsView(scene, parent)
{
	PENTERCONS;
	setFrameStyle(QFrame::NoFrame);
	setAlignment(Qt::AlignLeft | Qt::AlignTop);
	
// 	setOptimizationFlag(DontAdjustForAntialiasing);
// 	setOptimizationFlag(DontSavePainterState);
// 	setOptimizationFlag(DontClipPainter);

	m_holdcursor = new HoldCursor();
	scene->addItem(m_holdcursor);
	m_holdcursor->hide();

//         setAttribute(Qt::WA_OpaquePaintEvent);
}

ViewPort::~ViewPort()
{
	PENTERDES;
	
	cpointer().set_current_viewport((ViewPort*) 0);
}


void ViewPort::mouseMoveEvent(QMouseEvent* e)
{
	PENTER3;
// 	printf("\nViewPort::mouseMoveEvent\n");
// 	if (!ie().is_holding())
	QGraphicsView::mouseMoveEvent(e);
	cpointer().set_point(e->x(), e->y());
	e->accept();
}

void ViewPort::resizeEvent(QResizeEvent* e)
{
	PENTER3;
	QGraphicsView::resizeEvent(e);
}

void ViewPort::enterEvent(QEvent* e)
{
	QGraphicsView::enterEvent(e);
	cpointer().set_current_viewport(this);
}


void ViewPort::leaveEvent(QEvent* e)
{
	QGraphicsView::leaveEvent(e);
}


void ViewPort::keyPressEvent( QKeyEvent * e)
{
	ie().catch_key_press(e);
	e->accept();
}

void ViewPort::keyReleaseEvent( QKeyEvent * e)
{
	ie().catch_key_release(e);
	e->accept();
}

void ViewPort::mousePressEvent( QMouseEvent * e )
{
	ie().catch_mousebutton_press(e);
	e->accept();
}

void ViewPort::mouseReleaseEvent( QMouseEvent * e )
{
	ie().catch_mousebutton_release(e);
	e->accept();
}

void ViewPort::mouseDoubleClickEvent( QMouseEvent * e )
{
	ie().catch_mousebutton_doubleclick(e);
	e->accept();
}

void ViewPort::wheelEvent( QWheelEvent * e )
{
	ie().catch_scroll(e);
	e->accept();
}

void ViewPort::paintEvent( QPaintEvent* e )
{
// 	PWARN("ViewPort::paintEvent()");
	QGraphicsView::paintEvent(e);
}


void ViewPort::reset_cursor( )
{
	viewport()->unsetCursor();
	m_holdcursor->hide();
	m_holdcursor->reset();
}

void ViewPort::set_hold_cursor( const QString & cursorName )
{
	viewport()->setCursor(Qt::BlankCursor);
	
	m_holdcursor->setPos(cpointer().scene_pos());
	m_holdcursor->set_type(cursorName);
	m_holdcursor->show();
}

void ViewPort::set_hold_cursor_text( const QString & text )
{
	m_holdcursor->set_text(text);
}



/**********************************************************************/
/*                      HoldCursor                                    */
/**********************************************************************/


HoldCursor::HoldCursor()
{
	setZValue(200);
}

HoldCursor::~ HoldCursor( )
{
}

void HoldCursor::paint( QPainter * painter, const QStyleOptionGraphicsItem * option, QWidget * widget )
{
	Q_UNUSED(widget);

// 	printf("HoldCursor:: exposed rect is: x=%f, y=%f, w=%f, h=%f\n", option->exposedRect.x(), option->exposedRect.y(), option->exposedRect.width(), option->exposedRect.height());
	
	painter->drawPixmap(0, 0, m_pixmap);
	
	if (!m_text.isEmpty()) {
		QFontMetrics fm(QFont("Bitstream Vera Sans", 11));
		int width = fm.width(m_text) + 4;
		int height = fm.height();
		QRect textArea = QRect(m_pixmap.width() + 10, m_pixmap.height() / 4, width, height);
		painter->setFont(QFont("Bitstream Vera Sans", 11));
		painter->fillRect(textArea, QBrush(Qt::white));
		painter->drawText(textArea, m_text);
	}
}


void HoldCursor::set_text( const QString & text )
{
	m_text = text;
	update();
}

void HoldCursor::set_type( const QString & type )
{
	m_pixmap = find_pixmap(type);
	int x = pos().x();
	int y = pos().y();
	setPos(x - m_pixmap.width() / 2, y - m_pixmap.height() / 2);
}

QRectF HoldCursor::boundingRect( ) const
{
	return QRectF(0, 0, 120, 40);
}

void HoldCursor::reset()
{
	m_text = "";
}

//eof
