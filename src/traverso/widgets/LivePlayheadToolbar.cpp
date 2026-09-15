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

#include "LivePlayheadToolbar.h"

#include "Sheet.h"
#include "TSession.h"
#include "ProjectManager.h"
#include "Project.h"
#include "Utils.h"
#include "TConfig.h"

#include <QAction>
#include <QIcon>
#include <QMessageBox>

// Always put me below _all_ includes, this is needed
// in case we run with memory leak detection enabled!
#include "Debugger.h"


LivePlayheadToolbar::LivePlayheadToolbar(QWidget* parent)
	: QToolBar(tr("Live Play Head"), parent)
{
	setObjectName("livePlayheadToolbar");
	m_sheet = 0;
	m_enabled = false;

	setEnabled(false);

	m_playStopAction = addAction(QIcon(":/playstart"), tr("Start Live Play Head"), this, SLOT(play_stop_clicked()));

	connect(&pm(), SIGNAL(projectLoaded(Project*)), this, SLOT(set_project(Project*)));

	update_state();
}


LivePlayheadToolbar::~LivePlayheadToolbar()
{
}


void LivePlayheadToolbar::set_enabled(bool enabled)
{
	m_enabled = enabled;
	// The toolbar is created disabled; enable the widget itself as well as
	// the Play/Stop action, otherwise the action can never be pressed.
	setEnabled(enabled);
	update_state();
}


void LivePlayheadToolbar::set_project(Project* project)
{
	if (project) {
		connect(project, SIGNAL(currentSessionChanged(TSession*)), this, SLOT(set_session(TSession*)));
	} else {
		set_session(nullptr);
	}
}


void LivePlayheadToolbar::set_session(TSession* session)
{
	Project* project = qobject_cast<Project*>(session);
	// If the view was changed to the Project's session (mixer) then keep
	// the current active sheet.
	if (project) {
		return;
	}

	if (m_sheet) {
		disconnect(m_sheet, nullptr, this, nullptr);
		m_sheet = 0;
	}

	m_sheet = qobject_cast<Sheet*>(session);
	if (!m_sheet && session) {
		m_sheet = qobject_cast<Sheet*>(session->get_parent_session());
	}

	if (!m_sheet) {
		update_state();
		return;
	}

	connect(m_sheet, SIGNAL(liveTransportStarted()), this, SLOT(update_state()));
	connect(m_sheet, SIGNAL(liveTransportStopped()), this, SLOT(update_state()));

	update_state();
}


void LivePlayheadToolbar::play_stop_clicked()
{
	if (!m_sheet) {
		return;
	}

	if (m_sheet->is_live_transport_rolling()) {
		QMessageBox::StandardButton answer = QMessageBox::question(this,
			tr("Stop Live Play Head"),
			tr("Stop the Live play head?\n\nThe live output will be silenced."),
			QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

		if (answer == QMessageBox::Yes) {
			m_sheet->stop_live_transport();
		}
	} else {
		QMessageBox::StandardButton answer = QMessageBox::question(this,
			tr("Start Live Play Head"),
			tr("Start the Live play head from the current cue position (%1)?\n\nThe live output will start playing from there.")
				.arg(timeref_to_ms_2(m_sheet->get_transport_location())),
			QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

		if (answer == QMessageBox::Yes) {
			m_sheet->start_live_transport();
		}
	}
}


void LivePlayheadToolbar::update_state()
{
	if (!m_sheet) {
		m_playStopAction->setEnabled(false);
		m_playStopAction->setIcon(QIcon(":/playstart"));
		m_playStopAction->setText(tr("Start Live Play Head"));
		return;
	}

	if (m_sheet->is_live_transport_rolling()) {
		m_playStopAction->setIcon(QIcon(":/playstop"));
		m_playStopAction->setText(tr("Stop Live Play Head"));
		// Stopping is always allowed.
		m_playStopAction->setEnabled(true);
	} else {
		m_playStopAction->setIcon(QIcon(":/playstart"));
		m_playStopAction->setText(tr("Start Live Play Head"));
		m_playStopAction->setEnabled(m_enabled);
	}
}

//eof
