/*
    Copyright (C) 2008 Nicola Doebelin

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

#ifndef LIVEPLAYHEADTOOLBAR_H
#define LIVEPLAYHEADTOOLBAR_H

#include <QToolBar>
#include <QString>

class QAction;
class TSession;
class Sheet;
class Project;

class LivePlayheadToolbar : public QToolBar
{
	Q_OBJECT

public:
	LivePlayheadToolbar(QWidget* parent = 0);
	~LivePlayheadToolbar();

	void set_enabled(bool enabled);

signals:
	void configure_requested();

private:
	Sheet*		m_sheet;
	bool		m_enabled;
	QAction*	m_playStopAction;
	QAction*	m_configureAction;

private slots:
	void set_project(Project* project);
	void set_session(TSession* session);
	void play_stop_clicked();
	void configure_clicked();
	void update_state();
};

#endif
