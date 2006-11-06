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
 
    $Id: VUMeter.h,v 1.2 2006/11/06 19:22:27 n_doebelin Exp $
*/

#ifndef VUMETER_H
#define VUMETER_H

#include <QWidget>
#include <QString>
#include <QGridLayout>
#include <QLabel>
#include <QVector>

#include "VUMeterRuler.h"

class AudioBus;

class VUMeter : public QWidget
{

public:
        VUMeter(QWidget* parent, AudioBus* bus);
        ~VUMeter();

	static QVector<float>* vumeter_lut();

protected:
        void resizeEvent( QResizeEvent* e);
        void paintEvent( QPaintEvent* e);

private:
        bool			isActive;
        int 			m_id;
        int			m_channels;
	int			minSpace;
        QString			m_name;
	QLabel*			channelNameLabel;
	QGridLayout*		glayout;
	QBrush			bgBrush;
	QLinearGradient		bgGradient;
	VUMeterRuler*		ruler;
	bool			drawTickmarks;
	static QVector<float>	lut;

	static void calculate_lut_data();
};

inline QVector<float>* VUMeter::vumeter_lut()
{
	if (lut.isEmpty()) {
		calculate_lut_data();
	}
	return &lut;
}

#endif

