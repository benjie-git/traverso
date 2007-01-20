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

$Id: ProjectManager.cpp,v 1.19 2007/01/20 17:29:35 r_sijrier Exp $
*/

#include "ProjectManager.h"

#include <QApplication>
#include <QFileInfo>

#include "Project.h"
#include "Song.h"
#include "ContextPointer.h"
#include "AudioSourceManager.h"
#include "Information.h"
#include "Config.h"
#include "FileHelpers.h"
#include <AudioDevice.h>

// Always put me below _all_ includes, this is needed
// in case we run with memory leak detection enabled!
#include "Debugger.h"

QUndoGroup ProjectManager::undogroup;

ProjectManager::ProjectManager()
		: ContextItem()
{
	PENTERCONS;
	currentProject = (Project*) 0;
	m_exitInProgress = false;

	cpointer().add_contextitem(this);
}

ProjectManager& pm()
{
	static ProjectManager projMan;
	return projMan;
}

int ProjectManager::save_song(const QString& songName)
{
	if (currentProject) {
		currentProject->save();
	}

	return 0;
}

void ProjectManager::set_current_project(Project* pProject)
{
	PENTER;

	if (currentProject) {
		currentProject->save();
		delete currentProject;
	}

	currentProject = pProject;

	emit projectLoaded(currentProject);

	QString title = "";

	if (currentProject) {
		title = currentProject->get_title();
		config().set_property("Project", "current", title);
	}

}

int ProjectManager::create_new_project(const QString& projectName, int numSongs)
{
	PENTER;

	if (project_exists(projectName)) {
		PERROR("project %s already exists\n", projectName.toAscii().data());
		return -1;
	}

	Project *newProject = new Project(projectName);

	if (newProject->create(numSongs) < 0) {
		delete newProject;
		PERROR("couldn't create new project %s", projectName.toAscii().data());
		return -1;
	}

	return 0;
}

int ProjectManager::load_project(const QString& projectName)
{
	PENTER;

	if( ! project_exists(projectName) ) {
		PERROR("project %s doesn't exist\n", projectName.toAscii().data());
		return -1;
	}

	Project* newProject = new Project(projectName);

	if (!newProject)
		return -1;

	set_current_project(newProject);

	if (currentProject->load() < 0) {
		delete currentProject;
		currentProject = 0;
		set_current_project( (Project*) 0 );
		PERROR("couldn't load project %s", projectName.toAscii().data());
		return -1;
	}

	return 0;
}

int ProjectManager::remove_project( const QString& name )
{
	// check if we are removing the currentProject, and delete it before removing its files
	if (project_is_current(name)) {
		PMESG("removing current project\n");
		set_current_project( 0 );
	}

	return FileHelper::remove_recursively( name );
}

bool ProjectManager::project_is_current(const QString& title)
{
	QString path = config().get_property("Project", "directory", "/directory/unknown").toString();
	path += title;

	if (currentProject && (currentProject->get_root_dir() == path)) {
		return true;
	}

	return false;
}

bool ProjectManager::project_exists(const QString& title)
{
	QString project_dir = config().get_property("Project", "directory", "/directory/unknown").toString();
	QString project_path = project_dir + title;
	QFileInfo fileInfo(project_path);

	if (fileInfo.exists()) {
		return true;
	}

	return false;
}

Command* ProjectManager::save_project()
{
	if (currentProject) {
		currentProject->save();
	} else {
		info().information( tr("Open or create a project first!"));
	}

	return (Command*) 0;
}

Project * ProjectManager::get_project( )
{
	return currentProject;
}

void ProjectManager::start( )
{
	int loadProjectAtStartUp = config().get_property("Project", "loadLastUsed", 1).toInt();

	if (loadProjectAtStartUp != 0) {
		QString projectToLoad = config().get_property("Project", "current", "").toString();

		if ( projectToLoad.isNull() || projectToLoad.isEmpty() )
			projectToLoad="Untitled";

		if (project_exists(projectToLoad)) {
			if ( load_project(projectToLoad) < 0 ) {
				PWARN("Cannot load project %s. Continuing anyway...", projectToLoad.toAscii().data());
				info().warning( tr("Could not load project %1").arg(projectToLoad) );
			}
		} else {
			if (create_new_project("Untitled", 1) < 0) {
				PWARN("Cannot create project Untitled. Continuing anyway...");
			} else {
				load_project("Untitled");
			}

		}
	}
}


QUndoGroup* ProjectManager::get_undogroup() const
{
	return &undogroup;
}


Command* ProjectManager::exit()
{
	PENTER;
	m_exitInProgress = true;
	
	if (currentProject) {
		set_current_project(0);
	} else {
		QApplication::exit();
	}


	return (Command*) 0;
}

void ProjectManager::scheduled_for_deletion( Song * song )
{
	PENTER;
	m_deletionSongList.append(song);
}

void ProjectManager::delete_song( Song * song )
{
	PENTER;
	m_deletionSongList.removeAll(song);
	delete song;
	
	if (m_deletionSongList.isEmpty() && m_exitInProgress) {
		QApplication::exit();
	}
		
}

//eof
