/*
    Copyright (C) 2007 Remon Sijrier 
 
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

#include "OpenProjectDialog.h"
#include "ui_OpenProjectDialog.h"

#include <QDir>
#include <QStringList>
#include <QMessageBox>
#include <QTextStream>
#include <QDomDocument>
#include <QFileDialog>
#include <QHeaderView>
#include <QFileSystemWatcher>


#include <Config.h>
#include <Information.h>
#include <ProjectManager.h>
#include <Project.h>
#include <Utils.h>
#include "Interface.h"

// Always put me below _all_ includes, this is needed
// in case we run with memory leak detection enabled!
#include "Debugger.h"

OpenProjectDialog::OpenProjectDialog( QWidget * parent )
	: QDialog(parent)
{
	setupUi(this);
	
	QString path = config().get_property("Project", "directory", getenv("HOME")).toString();
	m_watcher = new QFileSystemWatcher(this);
	m_watcher->addPath(path);
	m_dirchangeDetected = false;
	
	projectListView->setColumnCount(2);
	update_projects_list();
	QStringList stringList;
	stringList << "Project Name" << "Sheets";
	projectListView->setHeaderLabels(stringList);
	
	projectListView->header()->resizeSection(0, 160);
	projectListView->header()->resizeSection(1, 30);
	
	connect(projectListView, SIGNAL(itemClicked(QTreeWidgetItem*,int)), this, SLOT(projectitem_clicked(QTreeWidgetItem*,int)));
	connect(m_watcher, SIGNAL(directoryChanged(const QString&)), this, SLOT(project_dir_rename_detected(const QString&)));
}

OpenProjectDialog::~ OpenProjectDialog( )
{}

void OpenProjectDialog::update_projects_list()
{
	printf("update_projects_list()\n");
	projectListView->clear();
	
	QString path = config().get_property("Project", "directory", getenv("HOME")).toString();

	QDir dir(path);

	QStringList list = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
	
	foreach(QString dirname, list) {
	
		/************ FROM HERE ****************/
		QDomDocument doc("Project");
		QString fileToOpen = path + "/" + dirname + "/project.tpf";
		
		QFile file(fileToOpen);

		if (!file.open(QIODevice::ReadOnly)) {
			PWARN("OpenProjectDialog:: Cannot open project properties file (%s)", fileToOpen.toAscii().data());
			continue;
		}

		QString errorMsg;
		if (!doc.setContent(&file, &errorMsg)) {
			file.close();
			PWARN("OpenProjectDialog:: Cannot set content of XML file (%s)", errorMsg.toAscii().data());
			continue;
		}

		file.close();

		QDomElement docElem = doc.documentElement();
		QDomNode propertiesNode = docElem.firstChildElement("Properties");
		QDomElement e = propertiesNode.toElement();
		QString title = e.attribute( "title", "" );
		QString description = e.attribute("description", "No description set");
		qint64 id = e.attribute( "id", "" ).toLongLong();
		

		QDomNode songsNode = docElem.firstChildElement("Sheets");
		QDomNode songNode = songsNode.firstChild();
		int songCounter = 0;
		
		// count to get Songs number....
		while(!songNode.isNull()) {
			songCounter++;
			songNode = songNode.nextSibling();
		}

		QString sNumSongs = QString::number(songCounter);

		/*********** TO HERE THIS CODE IS DUPLICATE FROM THAT IN PROJECT.CC :-( 
		Don't know if this is avoidable at all *********/


		QTreeWidgetItem* item = new QTreeWidgetItem(projectListView);
		item->setTextAlignment(0, Qt::AlignLeft);
		item->setTextAlignment(1, Qt::AlignHCenter);
		
		if (title != dirname) {
			item->setIcon(0, style()->standardIcon(QStyle::SP_MessageBoxWarning));
			QString html;
			html += tr("<p>Project directory name <b>%1</b> is different from the Project title <b>%2</b>!</p>"
				"<p>Did you rename the Project directory ? </p><p>Please rename the directory back to the "
				"Project title <b>%1</b>, and change the Project title with the Project Manager Dialog!</p>")
					.arg(dirname).arg(title);
				item->setToolTip(0, html);
		} else {
			QString html = "<html><head></head><body>Project: " + title + "<br /><br />";
			html += tr("Description:") + "<br />";
			html += description + "<br /><br />";
			html += tr("Created on:") + " " + extract_date_time(id).toString() + "<br />";
			html += "</body></html>";
			item->setToolTip(0, html);
		}
		
		item->setText(0, title);
		item->setText(1, sNumSongs);
	}
}

void OpenProjectDialog::projectitem_clicked( QTreeWidgetItem* item, int)
{
	if (item) {
		selectedProjectName->setText(item->text(0));
	}
}

void OpenProjectDialog::on_loadProjectButton_clicked( )
{
        // do we have the name of the project to load ?
	QString title;
	if (projectListView->currentItem()) {
		title = projectListView->currentItem()->text(0);
	}

	if (title.isEmpty()) {
		info().warning(tr("No Project selected!") );
		info().information(tr("Select a project and click the 'Load' button again") );
		return;
	}
	
	Project* project = pm().get_project();

	
	if (project && (project->get_title() == title)) {
		QMessageBox::StandardButton button = QMessageBox::question(this,
			"Traverso - Question",
   			"Are you sure you want to reopen the current project ?",
      			QMessageBox::Ok | QMessageBox::Cancel,
	 		QMessageBox::Cancel );
		if (button == QMessageBox::Cancel) {
			return;
		}
	}
		
	// first test if project exists
	// Note: this shouldn't be needed really, the projects in the view
	// should exist, but just in case someone removed it, you never know!
	if (!pm().project_exists(title)) {
		info().warning(tr("Project %1 does not exist, did you rename or remove the directory what that name ?").arg(title));
		return;
	}
	
	if (pm().load_project(title)<0) {
		PERROR("Could not load project %s", title.toAscii().data());
	}
	
	hide();
}

void OpenProjectDialog::on_deleteProjectbutton_clicked( )
{
        // do we have the name of the project to delete ?
	QString title = selectedProjectName->text();

	if (title.isEmpty()) {
		info().information(tr("You must supply a name for the project!") );
		return;
	}

        // first test if project exists
	if (!pm().project_exists(title)) {
		info().warning(tr("Project does not exist! (%1)").arg(title));
		return;
	}

	switch (QMessageBox::information(this,
		tr("Traverso - Question"),
		   tr("Are you sure that you want to remove the project %1 ? It's not possible to undo it !").arg(title).toAscii().data(),
		      "Yes", "No", QString::null, 1, -1)) {
			      case 0:
				      pm().remove_project(title);
				      update_projects_list();
				      break;
			      default:
				      return;
				      break;
		      }
		      return;
}


void OpenProjectDialog::on_projectDirSelectButton_clicked( )
{
	QString path = config().get_property("Project", "directory", getenv("HOME")).toString();
	
	QDir rootDir(path);
	rootDir.cdUp();
	
	QString newPath = QFileDialog::getExistingDirectory(this,
			tr("Choose an existing or create a new Project Directory"), rootDir.canonicalPath());
			
	if (newPath.isEmpty() || newPath.isNull()) {
		return;
	}
	
	QDir dir;
	
	if (dir.exists(newPath)) {
// 		QMessageBox::information( interface, tr("Traverso - Information"), tr("Using existing Project directory: %1\n").arg(newPath), "OK", 0 );
	} else if (!dir.mkpath(newPath)) {
		QMessageBox::warning( this, tr("Traverso - Warning"), tr("Unable to create Project directory! \n") +
				tr("Please check permission for this directory: %1").arg(newPath) );
		return;
	} else {
		QMessageBox::information( this, tr("Traverso - Information"), tr("Created new Project directory for you here: %1\n").arg(newPath), "OK", 0 );
	}
	
	QDir newdir(newPath);
	
	config().set_property("Project", "directory", newdir.canonicalPath());
	
	m_watcher->addPath(newdir.canonicalPath());

	
	update_projects_list();
}

void OpenProjectDialog::project_dir_rename_detected(const QString & dirname)
{
	update_projects_list();
	
	if (pm().renaming_directory_in_progress()) {
		return;
	}
	
	if (m_dirchangeDetected) {
		return;
	}
	
	m_dirchangeDetected = true;
	
	QMessageBox::critical( Interface::instance(), 
			tr("Traverso - Important"), 
			tr("A Project directory changed outside of Traverso. \n\n"
			   "This is NOT supported! Please undo this change now!\n\n"
			   "If you want to rename a Project, use the Project Manager instead!"),
			QMessageBox::Ok);

	m_dirchangeDetected = false;
}

//eof
