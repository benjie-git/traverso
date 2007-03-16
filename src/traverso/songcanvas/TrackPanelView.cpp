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

$Id: TrackPanelView.cpp,v 1.13 2007/03/16 00:10:26 r_sijrier Exp $
*/

#include <QGraphicsScene>
#include "TrackPanelView.h"
#include "TrackView.h"
#include <Themer.h>
#include "TrackPanelViewPort.h"
#include <Track.h>
#include "TrackPanelView.h"
#include <Utils.h>
#include <Mixer.h>
		
#include <Debugger.h>

const int MUTE_LED_X = 10;
const int SOLO_LED_X = 48;
const int REC_LED_X = 86;

TrackPanelView::TrackPanelView(TrackPanelViewPort* view, TrackView* trackView, Track * track )
	: ViewItem(0, trackView)
{
	PENTERCONS;
	
	m_viewPort = view;
	m_tv = trackView;
	m_track = track;
	
	m_viewPort->scene()->addItem(this);
	
	m_gainView = new TrackPanelGain(this, m_track);
	m_gainView->set_width(m_viewPort->width() - 20);
	m_gainView->setPos(10, 39);
	m_viewPort->scene()->addItem(m_gainView);
	
	m_panView = new TrackPanelPan(this, m_track);
	m_panView->set_width(m_viewPort->width() - 20);
	m_panView->setPos(10, 54);
	m_viewPort->scene()->addItem(m_panView);
	
	recLed = new TrackPanelLed(this, ":recled_on", ":/recled_off");
	soloLed = new TrackPanelLed(this, ":/sololed_on", ":/sololed_off");
	muteLed = new TrackPanelLed(this, ":/muteled_on", ":/muteled_off");
	
	m_viewPort->scene()->addItem(recLed);
	m_viewPort->scene()->addItem(soloLed);
	m_viewPort->scene()->addItem(muteLed);
	
	muteLed->setPos(10, 18);
	soloLed->setPos(48, 18);
	recLed->setPos(86, 18);
	
	if (m_track->armed()) {
		recLed->ison_changed(true);
	}
	if (m_track->is_solo()) {
		soloLed->ison_changed(true);
	}
	if (m_track->is_muted()) {
		muteLed->ison_changed(true);
	}
	
	inBus = new TrackPanelBus(this, m_track, ":/bus_in");
	outBus = new TrackPanelBus(this, m_track, ":/bus_out");
	
	m_viewPort->scene()->addItem(inBus);
	m_viewPort->scene()->addItem(outBus);
	
	inBus->setPos(10, 70);
	outBus->setPos(100, 70);

	
	connect(m_track, SIGNAL(armedChanged(bool)), recLed, SLOT(ison_changed(bool)));
	connect(m_track, SIGNAL(soloChanged(bool)), soloLed, SLOT(ison_changed(bool)));
	connect(m_track, SIGNAL(muteChanged(bool)), muteLed, SLOT(ison_changed(bool)));
	
	connect(m_track, SIGNAL(gainChanged()), this, SLOT(update_gain()));
	connect(m_track, SIGNAL(panChanged()), this, SLOT(update_pan()));
	
	connect(m_track, SIGNAL(inBusChanged()), inBus, SLOT(bus_changed()));
	connect(m_track, SIGNAL(outBusChanged()), outBus, SLOT(bus_changed()));
	
	
	m_boundingRect = QRectF(0, 0, 200, m_track->get_height());
// 	setFlags(ItemIsSelectable | ItemIsMovable);
// 	setAcceptsHoverEvents(true);

}

TrackPanelView::~TrackPanelView( )
{
	PENTERDES;
}


void TrackPanelView::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget)
{
	Q_UNUSED(widget);
	Q_UNUSED(option);
	
	int xstart = (int)option->exposedRect.x();
	int pixelcount = (int)option->exposedRect.width();
	
	if (m_tv->m_topborderwidth > 0) {
		QColor color = themer()->get_color("Track:cliptopoffset");
		painter->fillRect(xstart, 0, pixelcount, m_tv->m_topborderwidth, color);
	}
	
	if (m_tv->m_paintBackground) {
		QColor color = themer()->get_color("Track:background");
		painter->fillRect(xstart, m_tv->m_topborderwidth, pixelcount, m_track->get_height() - m_tv->m_bottomborderwidth, color);
	}
	
	if (m_tv->m_bottomborderwidth > 0) {
		QColor color = themer()->get_color("Track:clipbottomoffset");
		painter->fillRect(xstart, m_track->get_height() - m_tv->m_bottomborderwidth, pixelcount, m_tv->m_bottomborderwidth, color);
	}

	painter->setPen(themer()->get_color("Track:cliptopoffset"));
	painter->drawLine(m_viewPort->width() - 1, 0,  m_viewPort->width() - 1, m_track->get_height() - 1);
	painter->drawLine(m_viewPort->width() - 2, 0,  m_viewPort->width() - 2, m_track->get_height() - 1);
	
	draw_panel_track_name(painter);
}


void TrackPanelView::update_gain()
{
	m_gainView->update();
}


void TrackPanelView::update_pan()
{
	m_panView->update();
}


void TrackPanelView::draw_panel_track_name(QPainter* painter)
{
	QString sid = QString::number(m_track->get_sort_index() + 1);
	painter->setFont( QFont( "Bitstream Vera Sans", 8) );
	painter->drawText(4, 12, sid);
	painter->drawText(20, 12, m_track->get_name());
}


void TrackPanelView::calculate_bounding_rect()
{
	m_boundingRect = QRectF(0, 0, 200, m_track->get_height());
	int height =  m_track->get_height();
	
	if ((inBus->pos().y() + inBus->boundingRect().height()) > height) {
		inBus->hide();
		outBus->hide();
	} else {
		inBus->show();
		outBus->show();
	}
	
	if ( (m_panView->pos().y() + m_panView->boundingRect().height()) > height) {
		m_panView->hide();
	} else {
		m_panView->show();
	}
	
	if ( (m_gainView->pos().y() + m_panView->boundingRect().height()) > height) {
		m_gainView->hide();
	} else {
		m_gainView->show();
	}
	update();
}


TrackPanelGain::TrackPanelGain(TrackPanelView* parent, Track * track)
	: QGraphicsItem(parent)
{
	m_track = track;
}

void TrackPanelGain::paint( QPainter * painter, const QStyleOptionGraphicsItem * option, QWidget * widget )
{
	Q_UNUSED(option);
	Q_UNUSED(widget);
	const int GAIN_H = 8;

	int sliderWidth = (int)m_boundingRect.width() - 75;
	float gain = m_track->get_gain();
	QString sgain = coefficient_to_dbstring(gain);
	float db = coefficient_to_dB(gain);

	if (db < -60) {
		db = -60;
	}
	int sliderdbx =  (int) (sliderWidth - (sliderWidth*0.3)) - (int) ( ( (-1 * db) / 60 ) * sliderWidth);
	if (sliderdbx < 0) {
		sliderdbx = 0;
	}
	if (db > 0) {
		sliderdbx =  (int)(sliderWidth*0.7) + (int) ( ( db / 6 ) * (sliderWidth*0.3));
	}

	int cr = (gain >= 1 ? 30 + (int)(100 * gain) : (int)(50 * gain));
	int cb = ( gain < 1 ? 100 + (int)(50 * gain) : abs((int)(10 * gain)) );
	
	painter->setPen(themer()->get_color("TrackPanel:text"));
	painter->setFont( QFont( "Bitstream Vera Sans", (int)(GAIN_H*0.9)) );
	painter->drawText(0, GAIN_H + 1, "GAIN");
	painter->drawRect(30, 0, sliderWidth, GAIN_H);
	painter->fillRect(30, 0, sliderdbx, GAIN_H, QColor(cr,0,cb));
	painter->drawText(sliderWidth + 35, GAIN_H, sgain);
}

void TrackPanelGain::set_width(int width)
{
	m_boundingRect = QRectF(0, 0, width, 8);
}




TrackPanelPan::TrackPanelPan(TrackPanelView* parent, Track * track)
	: QGraphicsItem(parent)
{
	m_track = track;
}

void TrackPanelPan::paint( QPainter * painter, const QStyleOptionGraphicsItem * option, QWidget * widget )
{
	Q_UNUSED(option);
	Q_UNUSED(widget);
	const int PAN_H = 8;

	int sliderWidth = (int)m_boundingRect.width() - 75;
	float v;
	//	int y;
	QString s, span;
	painter->setPen(themer()->get_color("TrackPanel:text"));
	painter->setFont( QFont( "Bitstream Vera Sans", (int)(PAN_H*0.9)) );

	painter->drawText(0, PAN_H + 1, "PAN");

	v = m_track->get_pan();
	span = QByteArray::number(v,'f',1);
	s = ( v > 0 ? QString("+") + span :  span );
	painter->fillRect(30, 0, sliderWidth, PAN_H, themer()->get_color("TrackPanel:slider:background"));
	painter->drawRect(30, 0, sliderWidth, PAN_H);
	int pm= 30 + sliderWidth/2;
	int z = abs((int)(v*(sliderWidth/2)));
	int c = abs((int)(255*v));
	if (v>=0)
		painter->fillRect(pm, 0, z, PAN_H, QColor(c,0,0));
	else
		painter->fillRect(pm-z, 0, z, PAN_H, QColor(c,0,0));
	painter->drawText(30 + sliderWidth + 10, PAN_H + 1, s);
}

void TrackPanelPan::set_width(int width)
{
	m_boundingRect = QRectF(0, 0, width, 8);
}



TrackPanelLed::TrackPanelLed(TrackPanelView* parent, char * on, char * off )
	: ViewItem(parent, 0)
	,  onType(on)
	, offType(off)
	, m_isOn(false)
{
	Q_ASSERT_X(find_pixmap(onType), "TrackPanelLed constructor", "pixmap for ontype could not be found!");
	Q_ASSERT_X(find_pixmap(offType), "TrackPanelLed constructor", "pixmap for offtype could not be found!");
	m_boundingRect = find_pixmap(onType).rect();
}

void TrackPanelLed::paint(QPainter* painter, const QStyleOptionGraphicsItem * option, QWidget * widget )
{
	PENTER;
	Q_UNUSED(option);
	Q_UNUSED(widget);
	
	if (m_isOn) {
		painter->drawPixmap(0, 0, find_pixmap(onType));
	} else {
		painter->drawPixmap(0, 0, find_pixmap(offType));
	}
}

void TrackPanelLed::ison_changed(bool isOn)
{
        PENTER2;
        m_isOn = isOn;
	update();
}

TrackPanelBus::TrackPanelBus(TrackPanelView* parent, Track* track, char* type)
	: ViewItem(parent, 0)
	, m_track(track)
	, m_type(type)
{
	bus_changed();
}

void TrackPanelBus::paint(QPainter* painter, const QStyleOptionGraphicsItem * option, QWidget * widget )
{
	PENTER;
	Q_UNUSED(option);
	Q_UNUSED(widget);
	
	QPixmap pix = find_pixmap(m_type);
	
	painter->setPen(themer()->get_color("TrackPanel:text"));
	painter->setFont( QFont( "Bitstream Vera Sans", 8) );
	painter->drawPixmap(0, 0, pix);
	painter->drawText(pix.width() + 5, 8, m_busName);
}

void TrackPanelBus::bus_changed()
{
	QPixmap pix = find_pixmap(m_type);
	m_boundingRect = pix.rect();
	QFontMetrics fm(QFont( "Bitstream Vera Sans", 8));
	prepareGeometryChange();
	m_boundingRect.setWidth(pix.rect().width() + fm.width(m_busName) + 10);
	
	if (m_type == ":/bus_in") {
		m_busName =  m_track->get_bus_in();
	} else {
		m_busName = m_track->get_bus_out();
	}
	
	update();
}



//eof
