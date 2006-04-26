/***************************************************************************
                          imageviewer.cpp  -  An ImageViewer for KStars
                             -------------------
    begin                : Mon Aug 27 2001
    copyright            : (C) 2001 by Thomas Kabelmann
    email                : tk78@gmx.de
 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include <qfont.h>
//Added by qt3to4:
#include <QResizeEvent>
#include <QKeyEvent>
#include <QPaintEvent>
#include <QCloseEvent>
#include <QDesktopWidget>

#include <klocale.h>
#include <kmessagebox.h>
#include <kfiledialog.h>
#include <kstatusbar.h>
#include <kio/netaccess.h>
#include <kaction.h>

#include <kdebug.h>
#include <ktoolbar.h>
#include "imageviewer.h"

#include <kapplication.h>

ImageViewer::ImageViewer (const KUrl *url, const QString &capText, QWidget *parent, const char *name)
	: KMainWindow (parent, name), imageURL (*url), fileIsImage (false),
	  ctrl (false), key_s (false), key_q (false), downloadJob(0)
{
// toolbar can dock only on top-position and can't be minimized
// JH: easier to just disable its mobility
	//toolBar()->setMovingEnabled( false );

	KAction *action = new KAction( i18n("Close Window"), "fileclose",
			KShortcut(Qt::CTRL+Qt::Key_Q), this, SLOT (close()), actionCollection(), 
			"actCloseImViewer" );
	action->plug (toolBar());
	action = new KAction( i18n("Save Image"), "filesave", 
			KShortcut(Qt::CTRL+Qt::Key_S), this, SLOT (saveFileToDisc()), actionCollection(),
			"actSaveImViewer" );
	action->plug (toolBar());

	statusBar()->insertPermanentItem( capText, 0, 1 );
	statusBar()->setItemAlignment( 0, Qt::AlignLeft | Qt::AlignVCenter );
	QFont fnt = statusBar()->font();
	fnt.setPointSize( fnt.pointSize() - 2 );
	statusBar()->setFont( fnt );
	
	if (!imageURL.isValid())		//check URL
		kDebug()<<"URL is malformed"<<endl;
	setWindowTitle (imageURL.fileName()); // the title of the window
	loadImageFromURL();
}

ImageViewer::~ImageViewer() {
// check if download job is running
	checkJob();

	if (!file->remove())		// if the file was not complete downloaded the suffix is  ".part"
	{
		kDebug()<<QString("remove of %1 failed").arg(file->fileName())<<endl;
		file->setFileName (file->fileName() + ".part");		// set new suffix to filename
                kDebug()<<QString("try to remove %1").arg( file->fileName())<<endl;
		if (file->remove())
                    kDebug()<<"file removed\n";
		else
                    kDebug()<<"file not removed\n";
	}
}

void ImageViewer::paintEvent (QPaintEvent */*ev*/)
{
	bitBlt (this, 0, toolBar()->height() + 1, &pix);
}

void ImageViewer::resizeEvent (QResizeEvent */*ev*/)
{
	if ( !downloadJob )  // only if image is loaded
 		//FIXME commented out next line to build as KPixmapIO is gone -- annma 1006-02-20
		//pix = kpix.convertToPixmap ( image.smoothScale ( size().width() , size().height() - toolBar()->height() - statusBar()->height() ) );	// convert QImage to QPixmap (fastest method)

	update();
}

void ImageViewer::closeEvent (QCloseEvent *ev)
{
	if (ev)	// not if closeEvent (0) is called, because segfault
		ev->accept();	// parent-widgets should not get this event, so it will be filtered
	this->~ImageViewer();	// destroy the object, so the object can only allocated with operator new, not as a global/local variable
}


void ImageViewer::keyPressEvent (QKeyEvent *ev)
{
	ev->accept();  //make sure key press events are captured.
	switch (ev->key())
	{
		case Qt::Key_Control : ctrl = true; break;
		case Qt::Key_Q : key_q = true; break;
		case Qt::Key_S : key_s = true; break;
		default : ev->ignore();
	}
	if (ctrl && key_q)
		close();
	if (ctrl && key_s)
	{
		ctrl = false;
		key_s = false;
		saveFileToDisc();
	}
}

void ImageViewer::keyReleaseEvent (QKeyEvent *ev)
{
	ev->accept();
	switch (ev->key())
	{
		case Qt::Key_Control : ctrl = false; break;
		case Qt::Key_Q : key_q = false; break;
		case Qt::Key_S : key_s = false; break;
		default : ev->ignore();
	}
}

void ImageViewer::loadImageFromURL()
{
	file = tempfile.file();
	tempfile.unlink();		// we just need the name and delete the tempfile from disc; if we don't do it, a dialog will be shown
	KUrl saveURL (file->fileName());
	if (!saveURL.isValid())
            kDebug()<<"tempfile-URL is malformed\n";

	downloadJob = KIO::copy (imageURL, saveURL);	// starts the download asynchron
	connect (downloadJob, SIGNAL (result (KJob *)), SLOT (downloadReady (KJob *)));
}

void ImageViewer::downloadReady (KJob *job)
{
// set downloadJob to 0, but don't delete it - the job will automatically deleted !!!
	downloadJob = 0;

	if ( job->error() )
	{
		static_cast<KIO::Job*>(job)->showErrorDialog();
		closeEvent (0);
		return;		// exit this function
	}

	file->close(); // to get the newest informations of the file and not any informations from opening of the file

	if ( file->exists() )
	{
		showImage();
		return;
	}
	closeEvent (0);
}

void ImageViewer::showImage()
{
	if (!image.load (file->fileName()))		// if loading failed
	{
		QString text = i18n ("Loading of the image %1 failed.", imageURL.prettyURL());
		KMessageBox::error (this, text);
		closeEvent (0);
		return;
	}
	fileIsImage = true;	// we loaded the file and know now, that it is an image

	//First, if the image is less wide than the statusBar, we have to scale it up.
	if ( image.width() < statusBar()->width() ) {
		image.scaled ( statusBar()->width() , image.height() * statusBar()->width() / image.width(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation );
	}
	
	QRect deskRect = kapp->desktop()->availableGeometry();
	int w = deskRect.width(); // screen width
	int h = deskRect.height(); // screen height
	int h2 = image.height() + toolBar()->height() + statusBar()->height(); //height required for ImageViewer
	if (image.width() <= w && h2 <= h)
		resize ( image.width(), h2 );

//If the image is larger than screen width and/or screen height, shrink it to fit the screen
//while preserving the original aspect ratio

	else if (image.width() <= w) //only the height is too large
		resize ( int( image.width()*h/h2 ), h );
	else if (image.height() <= h) //only the width is too large
		resize ( w, int( h2*w/image.width() ) );
	else { //uh-oh...both width and height are too large.  which needs to be shrunk least?
		float fx = float(w)/float(image.width());
		float fy = float(h)/float(h2);
    if (fx > fy) //width needs to be shrunk less, so shrink to fit in height
			resize ( int( image.width()*fy ), h );
		else //vice versa
			resize ( w, int( h2*fx ) );
	}

	show();	// hide is default
// pix will be initialized in resizeEvent(), which will automatically called first time
}

void ImageViewer::saveFileToDisc()
{
	KUrl newURL = KFileDialog::getSaveURL(imageURL.fileName());  // save-dialog with default filename
	if (!newURL.isEmpty())
	{
		QFile f (newURL.directory() + "/" +  newURL.fileName());
		if (f.exists())
		{
			int r=KMessageBox::warningContinueCancel(static_cast<QWidget *>(parent()),
									i18n( "A file named \"%1\" already exists. "
											"Overwrite it?" , newURL.fileName()),
									i18n( "Overwrite File?" ),
									i18n( "&Overwrite" ) );
			if(r==KMessageBox::Cancel) return;
			
			f.remove();
		}
		saveFile (newURL);
	}
}

void ImageViewer::saveFile (KUrl &url) {
// synchronous Access to prevent segfaults
	if (!KIO::NetAccess::copy (KUrl (file->fileName()), url, (QWidget*) 0))
	{
		QString text = i18n ("Saving of the image %1 failed.", url.prettyURL());
		KMessageBox::error (this, text);
	}
}

void ImageViewer::close() {
	closeEvent (0);
}

void ImageViewer::checkJob() {
	if ( downloadJob ) {  // if download job is running
		downloadJob->kill( true );  // close job quietly, without emitting a result
		kDebug() << "Download job killed";
	}
}

#include "imageviewer.moc"
