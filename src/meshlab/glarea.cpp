/****************************************************************************
* MeshLab                                                           o o     *
* An extendible mesh processor                                    o     o   *
*                                                                _   O  _   *
* Copyright(C) 2005, 2006                                          \/)\/    *
* Visual Computing Lab                                            /\/|      *
* ISTI - Italian National Research Council                           |      *
*                                                                    \      *
* All rights reserved.                                                      *
*                                                                           *
* This program is free software; you can redistribute it and/or modify      *
* it under the terms of the GNU General Public License as published by      *
* the Free Software Foundation; either version 2 of the License, or         *
* (at your option) any later version.                                       *
*                                                                           *
* This program is distributed in the hope that it will be useful,           *
* but WITHOUT ANY WARRANTY; without even the implied warranty of            *
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the             *
* GNU General Public License (http://www.gnu.org/licenses/gpl.txt)          *
* for more details.                                                         *
*                                                                           *
****************************************************************************/

#include "../common/interfaces.h"
#include "glarea.h"
#include "mainwindow.h"

#include <wrap/gl/picking.h>
#include <wrap/qt/trackball.h>
#include <wrap/qt/col_qt_convert.h>

using namespace std;
using namespace vcg;

GLArea::GLArea(MultiViewer_Container *mvcont, RichParameterSet *current)
: Viewer(mvcont)
{
    this->updateCustomSettingValues(*current);
  log=mvcont->LogPtr();
	animMode=AnimNone;
	iRenderer=0; //Shader support
	iEdit=0;
	currentEditor=0;
	suspendedEditor=false;
	lastModelEdited = 0;
	cfps=0;
	lastTime=0;
	hasToPick=false;
	hasToGetPickPos=false;
	hasToUpdateTexture=false;
	helpVisible=false;
	takeSnapTile=false;
	activeDefaultTrackball=true;
	infoAreaVisible = true;
	trackBallVisible = true;
	currentShader = NULL;
	lastFilterRef = NULL;
	//lastEditRef = NULL;
	setAttribute(Qt::WA_DeleteOnClose,true);
	fov = 60;
	clipRatioFar = 1;
	clipRatioNear = 1;
	nearPlane = .2f;
	farPlane = 5.f;
	pointSize = 2.0f;
	
	updateLayerSetVisibilities();

    connect(meshDoc, SIGNAL(currentMeshChanged(int)), this, SLOT(updateLayer()));
	connect(meshDoc, SIGNAL(layerSetChanged()), this, SLOT(updateLayerSetVisibilities()));
	/*getting the meshlab MainWindow from parent, which is QWorkspace.
	*note as soon as the GLArea is added as Window to the QWorkspace the parent of GLArea is a QWidget,
	*which takes care about the window frame (its parent is the QWorkspace again).
	*/
	MainWindow* mainwindow = dynamic_cast<MainWindow*>(mvcont->parentWidget()->parentWidget());
	//connecting the MainWindow Slots to GLArea signal (simple passthrough)
	if(mainwindow != NULL){
		connect(this,SIGNAL(updateMainWindowMenus()),mainwindow,SLOT(updateMenus()));
		connect(mainwindow,SIGNAL(dispatchCustomSettings(RichParameterSet&)),this,SLOT(updateCustomSettingValues(RichParameterSet&)));
	}else{
		qDebug("The parent of the GLArea parent is not a pointer to the meshlab MainWindow.");
	}

}

GLArea::~GLArea()
{
	// warn any iRender plugin that we're deleting glarea
	if (iRenderer)
		iRenderer->Finalize(currentShader, *meshDoc, this);
}


/*
	This member returns the information of the Mesh in terms of VC,VQ,FC,FQ,WT
	where:
	VC = VertColor,VQ = VertQuality,FC = FaceColor,FQ = FaceQuality,WT = WedgTexCoord
*/
QString GLArea::GetMeshInfoString()
{
	QString info;
	if(mm()->hasDataMask(MeshModel::MM_VERTQUALITY) ) {info.append("VQ ");}
	if(mm()->hasDataMask(MeshModel::MM_VERTCOLOR) )   {info.append("VC ");}
	if(mm()->hasDataMask(MeshModel::MM_VERTRADIUS) )  {info.append("VR ");}
	if(mm()->hasDataMask(MeshModel::MM_VERTTEXCOORD)) {info.append("VT ");}
	if(mm()->hasDataMask(MeshModel::MM_VERTCURV) )    {info.append("VK ");}
	if(mm()->hasDataMask(MeshModel::MM_VERTCURVDIR) ) {info.append("VD ");}
	if(mm()->hasDataMask(MeshModel::MM_FACECOLOR) )   {info.append("FC ");}
	if(mm()->hasDataMask(MeshModel::MM_FACEQUALITY) ) {info.append("FQ ");}
	if(mm()->hasDataMask(MeshModel::MM_WEDGTEXCOORD)) {info.append("WT ");}
	if(mm()->hasDataMask(MeshModel::MM_CAMERA) )      {info.append("MC ");}

	return info;
}

QSize GLArea::minimumSizeHint() const {return QSize(400,300);}
QSize GLArea::sizeHint() const				{return QSize(400,300);}


void GLArea::initializeGL()
{
	glShadeModel(GL_SMOOTH);
  glPixelStorei(GL_PACK_ROW_LENGTH, 0);
	glPixelStorei(GL_PACK_ALIGNMENT, 1);
	glEnable(GL_DEPTH_TEST);
	glEnable(GL_NORMALIZE);
	static float diffuseColor[]={1.0,1.0,1.0,1.0};
	glEnable(GL_LIGHT0);
	glDisable(GL_LIGHT1);
	glLightfv(GL_LIGHT1,GL_DIFFUSE,diffuseColor);
	trackball.center=Point3f(0, 0, 0);
	trackball.radius= 1;

	trackball_light.center=Point3f(0, 0, 0);
	trackball_light.radius= 1;
  GLenum err = glewInit();
	if (err != GLEW_OK ) {
    assert(0);
  }
}

void GLArea::pasteTile()
{
	glPushAttrib(GL_ENABLE_BIT);
	QImage tileBuffer=grabFrameBuffer(true).mirrored(false,true);

	if (snapBuffer.isNull())
		snapBuffer = QImage(tileBuffer.width() * ss.resolution, tileBuffer.height() * ss.resolution, tileBuffer.format());

	uchar *snapPtr = snapBuffer.bits() + (tileBuffer.bytesPerLine() * tileCol) + ((totalCols * tileRow) * tileBuffer.numBytes());
	uchar *tilePtr = tileBuffer.bits();

	for (int y=0; y < tileBuffer.height(); y++)
	{
		memcpy((void*) snapPtr, (void*) tilePtr, tileBuffer.bytesPerLine());
		snapPtr+=tileBuffer.bytesPerLine() * totalCols;
		tilePtr+=tileBuffer.bytesPerLine();
	}

	tileCol++;

	if (tileCol >= totalCols)
	{
		tileCol=0;
		tileRow++;

		if (tileRow >= totalRows)
		{
			QString outfile=QString("%1/%2%3.png")
        .arg(ss.outdir)
        .arg(ss.basename)
        .arg(ss.counter++,2,10,QChar('0'));
			bool ret = (snapBuffer.mirrored(false,true)).save(outfile,"PNG");
      if (ret) log->Logf(GLLogStream::SYSTEM, "Snapshot saved to %s",outfile.toLocal8Bit().constData());
          else log->Logf(GLLogStream::WARNING,"Error saving %s",outfile.toLocal8Bit().constData());

			takeSnapTile=false;
			snapBuffer=QImage();
		}
	}
	update();
	glPopAttrib();
}



void GLArea::drawGradient()
	{
		glMatrixMode(GL_PROJECTION);
		glPushMatrix();
		glLoadIdentity();
		glOrtho(-1,1,-1,1,-1,1);
		glMatrixMode(GL_MODELVIEW);
		glPushMatrix();
		glLoadIdentity();
		glPushAttrib(GL_ENABLE_BIT);
		glDisable(GL_DEPTH_TEST);
		glDisable(GL_LIGHTING);
		glDisable(GL_TEXTURE_2D);

		glBegin(GL_TRIANGLE_STRIP);
            glColor(glas.backgroundTopColor);  	glVertex2f(-1, 1);
            glColor(glas.backgroundBotColor);	glVertex2f(-1,-1);
            glColor(glas.backgroundTopColor);		glVertex2f( 1, 1);
            glColor(glas.backgroundBotColor);	glVertex2f( 1,-1);
		glEnd();

		glPopAttrib();
		glPopMatrix(); // restore modelview
		glMatrixMode(GL_PROJECTION);
		glPopMatrix();
		glMatrixMode(GL_MODELVIEW);
	}

void GLArea::drawLight()
{
// ============== LIGHT TRACKBALL ==============
	// Apply the trackball for the light direction
	glPushMatrix();
	trackball_light.GetView();
	trackball_light.Apply(!(isDefaultTrackBall()));

	static float lightPosF[]={0.0,0.0,1.0,0.0};
	glLightfv(GL_LIGHT0,GL_POSITION,lightPosF);
	static float lightPosB[]={0.0,0.0,-1.0,0.0};
	glLightfv(GL_LIGHT1,GL_POSITION,lightPosB);

  if (!(isDefaultTrackBall()))
	{
    glPushAttrib(GL_ENABLE_BIT | GL_CURRENT_BIT);
		glColor3f(1,1,0);
    glDisable(GL_LIGHTING);
	  const unsigned int lineNum=3;
		glBegin(GL_LINES);
    for(unsigned int i=0;i<=lineNum;++i)
      for(unsigned int j=0;j<=lineNum;++j) {
        glVertex3f(-1.0f+i*2.0/lineNum,-1.0f+j*2.0/lineNum,-2);
        glVertex3f(-1.0f+i*2.0/lineNum,-1.0f+j*2.0/lineNum, 2);
      }
		glEnd();
    glPopAttrib();
	}
	glPopMatrix();

}

void GLArea::paintGL()
{
  QTime time;
  time.start();
	initTexture();
  glClearColor(1.0,1.0,1.0,0.0);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);


	setView();  // Set Modelview and Projection matrix
  if((!takeSnapTile) || (takeSnapTile && !ss.transparentBackground) )
    drawGradient();  // draws the background

  drawLight();

	glPushMatrix();

	// Finally apply the Trackball for the model
	trackball.GetView();
  trackball.Apply(false);
	glPushMatrix();

	//glScale(d);
  //	glTranslate(-FullBBox.Center());
  setLightModel();

	// Set proper colorMode
	if(rm.colorMode != GLW::CMNone)
	{
		glEnable(GL_COLOR_MATERIAL);
		glColorMaterial(GL_FRONT_AND_BACK,GL_AMBIENT_AND_DIFFUSE);
	}
	else glColor(Color4b::LightGray);

	if(rm.backFaceCull) glEnable(GL_CULL_FACE);
	else glDisable(GL_CULL_FACE);

	if(!meshDoc->busy)
	{
		glPushAttrib(GL_ALL_ATTRIB_BITS);

		if (iRenderer) iRenderer->Render(currentShader, *meshDoc, rm, this);
		else
		{

			foreach(MeshModel * mp, meshDoc->meshList)
				{
					//Mesh visibility is read from the viewer visibility map, not from the mesh 
					if(visibilityMap[mp->id]) mp->Render(rm.drawMode,rm.colorMode,rm.textureMode);
				}
		}
		if(iEdit) iEdit->Decorate(*mm(),this);

		// Draw the selection
    if(rm.selectedFace)  mm()->RenderSelectedFace();
    if(rm.selectedVert)  mm()->RenderSelectedVert();
    QAction * p;
		foreach(p , iDecoratorsList)
				{
					MeshDecorateInterface * decorInterface = qobject_cast<MeshDecorateInterface *>(p->parent());
					decorInterface->Decorate(p,*mm(),this,qFont);
				}

		glPopAttrib();
	} ///end if busy

	glPopMatrix(); // We restore the state to immediately after the trackball (and before the bbox scaling/translating)

	if(trackBallVisible && !takeSnapTile && !(iEdit && !suspendedEditor))
			trackball.DrawPostApply();

	// The picking of the surface position has to be done in object space,
	// so after trackball transformation (and before the matrix associated to each mesh);
	if(hasToPick && hasToGetPickPos)
	{
		Point3f pp;
    hasToPick=false;
    if(Pick<Point3f>(pointToPick[0],pointToPick[1],pp))
		{
					emit transmitSurfacePos(nameToGetPickPos, pp);
					hasToGetPickPos=false;
		}
	}
	glPopMatrix(); // We restore the state to immediately before the trackball

	// Double click move picked point to center
	// It has to be done in the before trackball space (we MOVE the trackball itself...)
	if(hasToPick && !hasToGetPickPos)
  {
		Point3f pp;
    hasToPick=false;
    if(Pick<Point3f>(pointToPick[0],pointToPick[1],pp))
		{
		      trackball.Translate(-pp);
          trackball.Scale(1.25f);
          QCursor::setPos(mapToGlobal(QPoint(width()/2+2,height()/2+2)));
		}
  }

	// ...and take a snapshot
	if (takeSnapTile) pasteTile();

	// Draw the log area background
	// on the bottom of the glArea
	if(infoAreaVisible)
	{
		displayInfo();
		updateFps(time.elapsed());
	}

	//Draw highlight if it is the current viewer
	if(mvc->currentId==id)
		displayViewerHighlight();

	// Finally display HELP if requested
	if (isHelpVisible()) displayHelp();

  int error = glGetError();
	if(error) {
    log->Logf(GLLogStream::WARNING,"There are gl errors");
	}

	//check if viewers are linked
	MainWindow *window = (MainWindow*) QApplication::activeWindow();
	if(window && window->linkViewersAct->isChecked())
			mvc->updateTrackballInViewers();
}

void GLArea::displayInfo()
{
	// Enter in 2D screen Mode again
	glMatrixMode(GL_PROJECTION);
	glPushMatrix();
	glLoadIdentity();
	glOrtho(-1,1,-1,1,-1,1);
	glMatrixMode(GL_MODELVIEW);
	glPushMatrix();
	glLoadIdentity();
	glPushAttrib(GL_ENABLE_BIT);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_LIGHTING);
	glDisable(GL_TEXTURE_2D);
	glEnable(GL_BLEND);

	qFont.setStyleStrategy(QFont::NoAntialias);
	qFont.setFamily("Helvetica");
	qFont.setPixelSize(12);
  Color4b logAreaColorInactive = glas.logAreaColor;
  logAreaColorInactive[0]/=2;
  logAreaColorInactive[1]/=2;
  logAreaColorInactive[2]/=2;
  glBlendFunc(GL_ONE,GL_SRC_ALPHA);
    glas.logAreaColor[3]=128;
  if(mvc->currentId==id) glColor(glas.logAreaColor);
                   else  glColor(logAreaColorInactive);
	int lineNum =4;
	float lineSpacing = qFont.pixelSize()*1.5f;
	float barHeight = -1 + 2.0*(lineSpacing*(lineNum+.25))/float(curSiz.height());


	glBegin(GL_QUADS);
    glVertex2f(-1.f,barHeight);     glVertex2f(-1.f,-1.f);
    glVertex2f( 1.f,-1.f);          glVertex2f( 1.f,barHeight);
	glEnd();

  glColor4f(1,1,1,1);

  float leftCol = 20;
  float rightCol=curSiz.width()*0.5;
 	float startPos = curSiz.height()-(5+lineSpacing*(lineNum));

  //	renderText(20,startPos+ 1*lineSpacing,tr("LOG MESSAGES"),qFont);
	//TODO TEMPORANEO BISOGNA SCRIVERE SOLO LE INFO RELATIVE ALLO STATO DELLA GLAREA CORRENTE
	//log.glDraw(this,-1,3,lineSpacing,qFont);

	if(meshDoc->size()==1)
	{
    renderText(rightCol,startPos+ 1*lineSpacing,tr("Vertices: %1").arg(mm()->cm.vn),qFont);
    renderText(rightCol,startPos+ 2*lineSpacing,tr("Faces: %1").arg(mm()->cm.fn),qFont);
	}
	else
	{
      renderText(rightCol,startPos+ 1*lineSpacing,tr("<%1>").arg(mm()->shortName()),qFont);
      renderText(rightCol,startPos+ 2*lineSpacing,tr("Vertices: %1 (%2)").arg(mm()->cm.vn).arg(meshDoc->vn()),qFont);
      renderText(rightCol,startPos+ 3*lineSpacing,tr("Faces: %1 (%2)").arg(mm()->cm.fn).arg(meshDoc->fn()),qFont);
	}
  if(rm.selectedFace || rm.selectedVert || mm()->cm.sfn>0 || mm()->cm.svn>0 )
      renderText(rightCol,startPos+ 4*lineSpacing,tr("Selection: v:%1 f:%2").arg(mm()->cm.svn).arg(mm()->cm.sfn),qFont);

  renderText(leftCol,startPos+ 4*lineSpacing,GetMeshInfoString(),qFont);

  if(fov>5) renderText(leftCol,startPos+1*lineSpacing,QString("FOV: ")+QString::number((int)fov,10),qFont);
       else renderText(leftCol,startPos+1*lineSpacing,QString("FOV: Ortho"),qFont);
	if ((cfps>0) && (cfps<500))
      renderText(leftCol,startPos+2*lineSpacing,QString("FPS: %1").arg(cfps,7,'f',1),qFont);
	if ((clipRatioNear!=1) || (clipRatioFar!=1))
      renderText(leftCol,startPos+3*lineSpacing,QString("Clipping: N:%1 F:%2").arg(clipRatioNear,7,'f',1).arg(clipRatioFar,7,'f',1),qFont);


	// Closing 2D
	glPopAttrib();
	glPopMatrix(); // restore modelview
	glMatrixMode(GL_PROJECTION);
	glPopMatrix();
	glMatrixMode(GL_MODELVIEW);

}

void GLArea::displayViewerHighlight()
{
	// Enter in 2D screen Mode again
	glPushAttrib(GL_LINE_BIT);

	glMatrixMode(GL_PROJECTION);
	glPushMatrix();
	glLoadIdentity();
	glOrtho(-1,1,-1,1,-1,1);
	glMatrixMode(GL_MODELVIEW);
	glPushMatrix();
	glLoadIdentity();
	glPushAttrib(GL_ENABLE_BIT);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_LIGHTING);
	glDisable(GL_TEXTURE_2D);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glColor4f(1.0f,1.0f,1.0f,0.3f);
  for(int width =5; width>0 ; width -= 2)
      {
      glLineWidth(width);
      glBegin(GL_LINE_LOOP);
        glVertex2f(-1.f,1.f);     glVertex2f( 1.f,1.f); glVertex2f( 1.f,-1.f);    glVertex2f(-1.f,-1.f);
      glEnd();
    }
  // Closing 2D
	glPopAttrib();
	glPopMatrix(); // restore modelview
	glMatrixMode(GL_PROJECTION);
	glPopMatrix();
	glMatrixMode(GL_MODELVIEW);
	glPopAttrib();

}


void GLArea::displayHelp()
{
	glMatrixMode(GL_PROJECTION);
	glPushMatrix();
	glLoadIdentity();
	glOrtho(-1,1,-1,1,-1,1);
	glMatrixMode(GL_MODELVIEW);
	glPushMatrix();
	glLoadIdentity();
	glPushAttrib(GL_ENABLE_BIT);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_LIGHTING);
	glDisable(GL_TEXTURE_2D);
	glEnable(GL_BLEND);

	glPushAttrib(GL_ENABLE_BIT | GL_COLOR_BUFFER_BIT | GL_CURRENT_BIT );
	glDisable(GL_TEXTURE_2D);
	glDisable(GL_LIGHTING);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	glBegin(GL_TRIANGLE_STRIP);
		glColor4f(.5f,.8f,1.f,.6f); glVertex2f(-1, 1);
		glColor4f(.0f,.0f,.0f,.0f); glVertex2f(-1,-1);
		glColor4f(.5f,.8f,1.f,.6f); glVertex2f(-.5f,1);
		glColor4f(.0f,.0f,.0f,.0f); glVertex2f(-.5f,-1);
	glEnd();


  float fontSpacingV = (curSiz.height()*.01f)+3;
	float hPosition = curSiz.width()*.1f;
	glColor(Color4b::White);
	//qFont.setBold(true);
	renderText(2+hPosition-(qFont.pointSize()*9),1.5*fontSpacingV,QString("MeshLab Quick Help"),qFont);qFont.setBold(false);
  renderText(2,3*fontSpacingV,QString("Drag:"),qFont);								renderText(hPosition,3*fontSpacingV,QString("Rotate"),qFont);
	renderText(2,4.5*fontSpacingV,QString("Ctrl-Drag:"),qFont);					renderText(hPosition,4.5*fontSpacingV,QString("Pan"),qFont);
	renderText(2,6*fontSpacingV,QString("Shift-Drag:"),qFont);					renderText(hPosition,6*fontSpacingV,QString("Zoom"),qFont);
	renderText(2,7.5*fontSpacingV,QString("Alt-Drag:"),qFont);					renderText(hPosition,7.5*fontSpacingV,QString("Z-Panning"),qFont);
	renderText(2,9*fontSpacingV,QString("Ctrl-Shift-Drag:"),qFont);			renderText(hPosition,9*fontSpacingV,QString("Rotate light"),qFont);
	renderText(2,10.5*fontSpacingV,QString("Wheel:"),qFont);						renderText(hPosition,10.5*fontSpacingV,QString("Zoom"),qFont);
	renderText(2,12*fontSpacingV,QString("Shift-Wheel:"),qFont);				renderText(hPosition,12*fontSpacingV,QString("Change perspective"),qFont);
	renderText(2,13.5*fontSpacingV,QString("Ctrl-Wheel:"),qFont);				renderText(hPosition,13.5*fontSpacingV,QString("Move near clipping plane"),qFont);
	renderText(2,15*fontSpacingV,QString("Ctrl-Shift-Wheel:"),qFont);		renderText(hPosition,15*fontSpacingV,QString("Move far clipping plane"),qFont);
	renderText(2,16.5*fontSpacingV,QString("Double Click:"),qFont);			renderText(hPosition,16.5*fontSpacingV,QString("Center on mouse"),qFont);
	renderText(2,18*fontSpacingV,QString("Alt+enter:"),qFont);						renderText(hPosition,18*fontSpacingV,QString("Enter/Exit fullscreen mode"),qFont);
	glPopAttrib();
	// Closing 2D
	glPopAttrib();
	glPopMatrix(); // restore modelview
	glMatrixMode(GL_PROJECTION);
	glPopMatrix();
	glMatrixMode(GL_MODELVIEW);

}

void GLArea::saveSnapshot()
{
	totalCols=totalRows=ss.resolution;
	tileRow=tileCol=0;

	takeSnapTile=true;
	update();
}

void GLArea::updateLayer()
{
    //if we have an edit tool open, notify it that the current layer has changed
	if(iEdit)
	{
		assert(lastModelEdited);  //if there is an editor last model edited should always be set when start edit is called
		iEdit->LayerChanged(*meshDoc, *lastModelEdited, this);

		//now update the last model edited
		//TODO this is not the best design....   iEdit should maybe keep track of the model on its own
		lastModelEdited = meshDoc->mm();
	}
}

void GLArea::setCurrentEditAction(QAction *editAction)
{
	assert(editAction);
	currentEditor = editAction;

	iEdit = actionToMeshEditMap.value(currentEditor);
	assert(iEdit);
	lastModelEdited = meshDoc->mm();
	if (!iEdit->StartEdit(*meshDoc, this))
		//iEdit->EndEdit(*(meshDoc->mm()), this);
		endEdit();
	else
    log->Logf(GLLogStream::SYSTEM,"Started Mode %s", qPrintable(currentEditor->text()));
}


void GLArea::closeEvent(QCloseEvent *event)
{
	bool close = true;
	if(isWindowModified())
	{
		 QMessageBox::StandardButton ret=QMessageBox::question(
                this,  tr("MeshLab"), tr("File '%1' modified.\n\nClose without saving?").arg(getFileName()),
								QMessageBox::Yes|QMessageBox::No,
								QMessageBox::No);
		if(ret==QMessageBox::No)
		{
			close = false;	// don't close please!
			event->ignore();
			return;
		}
	}
	if(getCurrentEditAction()) endEdit();
  emit glareaClosed();
	event->accept();
}

void GLArea::keyReleaseEvent ( QKeyEvent * e )
{
	e->ignore();
	if(iEdit && !suspendedEditor)  iEdit->keyReleaseEvent(e,*mm(),this);
	else{
      if(e->key()==Qt::Key_Control) trackball.ButtonUp(QT2VCG(Qt::NoButton, Qt::ControlModifier ) );
      if(e->key()==Qt::Key_Shift) trackball.ButtonUp(QT2VCG(Qt::NoButton, Qt::ShiftModifier ) );
      if(e->key()==Qt::Key_Alt) trackball.ButtonUp(QT2VCG(Qt::NoButton, Qt::AltModifier ) );
  }
}

void GLArea::keyPressEvent ( QKeyEvent * e )
{
	e->ignore();
	if(iEdit && !suspendedEditor)  iEdit->keyPressEvent(e,*mm(),this);
	else{
      if(e->key()==Qt::Key_Control) trackball.ButtonDown(QT2VCG(Qt::NoButton, Qt::ControlModifier ) );
      if(e->key()==Qt::Key_Shift) trackball.ButtonDown(QT2VCG(Qt::NoButton, Qt::ShiftModifier ) );
      if(e->key()==Qt::Key_Alt) trackball.ButtonDown(QT2VCG(Qt::NoButton, Qt::AltModifier ) );
  }
}

void GLArea::mousePressEvent(QMouseEvent*e)
{
  e->accept();
	setFocus();

  if( (iEdit && !suspendedEditor) )
		  iEdit->mousePressEvent(e,*mm(),this);
  else {
	    if ((e->modifiers() & Qt::ShiftModifier) && (e->modifiers() & Qt::ControlModifier) &&
          (e->button()==Qt::LeftButton) )
            activeDefaultTrackball=false;
	      else activeDefaultTrackball=true;

	    if (isDefaultTrackBall())
			{
					if(QApplication::keyboardModifiers () & Qt::Key_Control) trackball.ButtonDown(QT2VCG(Qt::NoButton, Qt::ControlModifier ) );
																															else trackball.ButtonUp  (QT2VCG(Qt::NoButton, Qt::ControlModifier ) );
					if(QApplication::keyboardModifiers () & Qt::Key_Shift) trackball.ButtonDown(QT2VCG(Qt::NoButton, Qt::ShiftModifier ) );
																														else trackball.ButtonUp  (QT2VCG(Qt::NoButton, Qt::ShiftModifier ) );
					if(QApplication::keyboardModifiers () & Qt::Key_Alt) trackball.ButtonDown(QT2VCG(Qt::NoButton, Qt::AltModifier ) );
																													else trackball.ButtonUp  (QT2VCG(Qt::NoButton, Qt::AltModifier ) );

          trackball.MouseDown(e->x(),height()-e->y(), QT2VCG(e->button(), e->modifiers() ) );
			}
	    else trackball_light.MouseDown(e->x(),height()-e->y(), QT2VCG(e->button(), Qt::NoModifier ) );
  }
	update();
}

void GLArea::mouseMoveEvent(QMouseEvent*e)
{
	if( (iEdit && !suspendedEditor) )
		iEdit->mouseMoveEvent(e,*mm(),this);
	else {
		if (isDefaultTrackBall())
		{
			trackball.MouseMove(e->x(),height()-e->y());
			setCursorTrack(trackball.current_mode);
		}
		else trackball_light.MouseMove(e->x(),height()-e->y());
		update();
	}

}

// When mouse is released we set the correct mouse cursor
void GLArea::mouseReleaseEvent(QMouseEvent*e)
{
  //clearFocus();
	activeDefaultTrackball=true;
    if( (iEdit && !suspendedEditor) )
			iEdit->mouseReleaseEvent(e,*mm(),this);
    else {
          if (isDefaultTrackBall()) trackball.MouseUp(e->x(),height()-e->y(), QT2VCG(e->button(), e->modifiers() ) );
	        else trackball_light.MouseUp(e->x(),height()-e->y(), QT2VCG(e->button(),e->modifiers()) );
		      setCursorTrack(trackball.current_mode);
        }

	update();
}

//Processing of tablet events, interesting only for painting plugins
void GLArea::tabletEvent(QTabletEvent*e)
{
	if(iEdit && !suspendedEditor) iEdit->tabletEvent(e,*mm(),this);
	else e->ignore();
}

void GLArea::wheelEvent(QWheelEvent*e)
{
	setFocus();
	const int WHEEL_STEP = 120;
	float notch = e->delta()/ float(WHEEL_STEP);
  switch(e->modifiers())
  {
    case Qt::ShiftModifier + Qt::ControlModifier	: clipRatioFar  = math::Clamp( clipRatioFar*powf(1.2f, notch),0.01f,50.0f); break;
    case Qt::ControlModifier											: clipRatioNear = math::Clamp(clipRatioNear*powf(1.2f, notch),0.01f,50.0f); break;
    case Qt::AltModifier													: pointSize = math::Clamp(pointSize*powf(1.2f, notch),0.01f,150.0f);
			foreach(MeshModel * mp, meshDoc->meshList)
				mp->glw.SetHintParamf(GLW::HNPPointSize,pointSize);
			break;
    case Qt::ShiftModifier												: fov = math::Clamp(fov*powf(1.2f,notch),5.0f,90.0f); break;
    default:
      trackball.MouseWheel( e->delta()/ float(WHEEL_STEP));
      break;
	}
	updateGL();
}


void GLArea::mouseDoubleClickEvent ( QMouseEvent * e )
{
  hasToPick=true;
  pointToPick=Point2i(e->x(),height()-e->y());
	updateGL();
}

void GLArea::focusInEvent ( QFocusEvent * e )
{
	QWidget::focusInEvent(e);
	emit currentViewerChanged(id);
}

void GLArea::setCursorTrack(vcg::TrackMode *tm)
{
 //static QMap<QString,QCursor> curMap;
 if(curMap.isEmpty())
 {
  curMap[QString("")]=QCursor(Qt::ArrowCursor);
  curMap["SphereMode"]=QCursor(QPixmap(":/images/cursors/plain_trackball.png"),1,1);
  curMap["PanMode"]=QCursor(QPixmap(":/images/cursors/plain_pan.png"),1,1);
  curMap["ScaleMode"]=QCursor(QPixmap(":/images/cursors/plain_zoom.png"),1,1);
 }
 if(tm) setCursor(curMap[tm->Name()]);
 else setCursor(curMap[""]);

}

void GLArea::setDrawMode(vcg::GLW::DrawMode mode)
{
	rm.drawMode = mode;
	updateGL();
}


void GLArea::setColorMode(vcg::GLW::ColorMode mode)
{
	rm.colorMode = mode;
	updateGL();
}
void GLArea::updateTexture()
{
	hasToUpdateTexture = true;
}

// Texture loading done during the first paint.
void GLArea::initTexture()
{
	if(hasToUpdateTexture)
	{
        foreach (MeshModel *mp,meshDoc->meshList)
            mp->glw.TMId.clear();

		qDebug("Beware: deleting the texutres could lead to problems for shared textures.");
		hasToUpdateTexture = false;
	}

    foreach (MeshModel *mp, meshDoc->meshList)
    {
        if(!mp->cm.textures.empty() && mp->glw.TMId.empty()){
            glEnable(GL_TEXTURE_2D);
            GLint MaxTextureSize;
            glGetIntegerv(GL_MAX_TEXTURE_SIZE,&MaxTextureSize);

            for(unsigned int i =0; i< mp->cm.textures.size();++i){
                QImage img, imgScaled, imgGL;

                bool res = img.load(mp->cm.textures[i].c_str());
                if(!res)
				{
                    // Note that sometimes (in collada) the texture names could have been encoded with a url-like style (e.g. replacing spaces with '%20') so making some other attempt could be harmless
                    QString ConvertedName = QString(mp->cm.textures[i].c_str()).replace(QString("%20"), QString(" "));
                    res = img.load(ConvertedName);
                    if(!res) qDebug("Failure of loading texture %s",mp->cm.textures[i].c_str());
                    else qDebug("Warning, texture loading was successful only after replacing %%20 with spaces;\n Loaded texture %s instead of %s",qPrintable(ConvertedName),mp->cm.textures[i].c_str());
				}
                // image has to be scaled to a 2^n size. We choose the first 2^N <= picture size.
                int bestW=pow(2.0,floor(::log(double(img.width() ))/::log(2.0)));
                int bestH=pow(2.0,floor(::log(double(img.height()))/::log(2.0)));
                while(bestW>MaxTextureSize) bestW /=2;
                while(bestH>MaxTextureSize) bestH /=2;

                log->Log(GLLogStream::SYSTEM,"Loading textures");
                log->Logf(GLLogStream::SYSTEM,"	Texture[ %3i ] =  '%s' ( %6i x %6i ) -> ( %6i x %6i )",	i,mp->cm.textures[i].c_str(), img.width(), img.height(),bestW,bestH);
                imgScaled=img.scaled(bestW,bestH,Qt::IgnoreAspectRatio,Qt::SmoothTransformation);
                imgGL=convertToGLFormat(imgScaled);
                mp->glw.TMId.push_back(0);
                glGenTextures( 1, (GLuint*)&(mp->glw.TMId.back()) );
                glBindTexture( GL_TEXTURE_2D, mp->glw.TMId.back() );
                glTexImage2D( GL_TEXTURE_2D, 0, 3, imgGL.width(), imgGL.height(), 0, GL_RGBA, GL_UNSIGNED_BYTE, imgGL.bits() );

                glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR );
                gluBuild2DMipmaps(GL_TEXTURE_2D, 3, imgGL.width(), imgGL.height(), GL_RGBA, GL_UNSIGNED_BYTE, imgGL.bits() );

                if(glas.textureMagFilter == 0 ) 	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
                else	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
                if(glas.textureMinFilter == 0 ) 	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
                else	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR );

                glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
                qDebug("      	will be loaded as GL texture id %i  ( %i x %i )",mp->glw.TMId.back() ,imgGL.width(), imgGL.height());
            }
        }
        glDisable(GL_TEXTURE_2D);
    }
}

void GLArea::setTextureMode(vcg::GLW::TextureMode mode)
{
	rm.textureMode = mode;
	updateGL();
}

void GLArea::setLight(bool state)
{
	rm.lighting = state;
	updateGL();
}

void GLArea::setLightMode(bool state,LightingModel lmode)
{
	switch(lmode)
	{
	  case LDOUBLE:		rm.doubleSideLighting = state;	break;
	  case LFANCY:		rm.fancyLighting = state; break;
	}
	updateGL();
}

void GLArea::setBackFaceCulling(bool enabled)
{
	rm.backFaceCull = enabled;
	updateGL();
}

void GLArea::setSelectFaceRendering(bool enabled)
{
  rm.selectedFace = enabled;
  updateGL();
}
void GLArea::setSelectVertRendering(bool enabled)
{
  rm.selectedVert = enabled;
  updateGL();
}

void GLArea::setLightModel()
{
    if (rm.lighting)
	{
		glEnable(GL_LIGHTING);

		if (rm.doubleSideLighting)
			glEnable(GL_LIGHT1);
		else
			glDisable(GL_LIGHT1);

        glLightfv(GL_LIGHT0, GL_AMBIENT, Color4f::Construct(glas.baseLightAmbientColor).V());
        glLightfv(GL_LIGHT0, GL_DIFFUSE, Color4f::Construct(glas.baseLightDiffuseColor).V());
        glLightfv(GL_LIGHT0, GL_SPECULAR,Color4f::Construct(glas.baseLightSpecularColor).V());

        glLightfv(GL_LIGHT1, GL_AMBIENT, Color4f::Construct(glas.baseLightAmbientColor).V());
        glLightfv(GL_LIGHT1, GL_DIFFUSE, Color4f::Construct(glas.baseLightDiffuseColor).V());
        glLightfv(GL_LIGHT1, GL_SPECULAR,Color4f::Construct(glas.baseLightSpecularColor).V());
        if(rm.fancyLighting)
        {
            glLightfv(GL_LIGHT0, GL_DIFFUSE, Color4f::Construct(glas.fancyFLightDiffuseColor).V());
            glLightfv(GL_LIGHT1, GL_DIFFUSE, Color4f::Construct(glas.fancyBLightDiffuseColor).V());
		}
	}
    else glDisable(GL_LIGHTING);
}

void GLArea::setView()
{
    glViewport(0,0, this->width(),this->height());
    curSiz.setWidth(this->width());
    curSiz.setHeight(this->height());

    GLfloat fAspect = (GLfloat)curSiz.width()/ curSiz.height();
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();

	// This parameter is the one that controls:
	// HOW LARGE IS THE TRACKBALL ICON ON THE SCREEN.
	float viewRatio = 1.75f;
	float cameraDist = viewRatio / tanf(vcg::math::ToRad(fov*.5f));

 if(fov==5)
   cameraDist = 1000; // small hack for orthographic projection where camera distance is rather meaningless...
	nearPlane = cameraDist - 2.f*clipRatioNear;
	farPlane =  cameraDist + 10.f*clipRatioFar;
	if(nearPlane<=cameraDist*.1f) nearPlane=cameraDist*.1f;

	if (!takeSnapTile)
	{
		if(fov==5)	glOrtho( -viewRatio*fAspect, viewRatio*fAspect, -viewRatio, viewRatio, cameraDist - 2.f*clipRatioNear, cameraDist+2.f*clipRatioFar);
		else    		gluPerspective(fov, fAspect, nearPlane, farPlane);
	}
	else	setTiledView(fov, viewRatio, fAspect, nearPlane, farPlane, cameraDist);

	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
	gluLookAt(0, 0, cameraDist,0, 0, 0, 0, 1, 0);
}

void GLArea::setTiledView(GLdouble fovY, float viewRatio, float fAspect, GLdouble zNear, GLdouble zFar,  float cameraDist)
{
	if(fovY<=5)
	{
		GLdouble fLeft   = -viewRatio*fAspect;
		GLdouble fRight  =  viewRatio*fAspect;
		GLdouble fBottom = -viewRatio;
		GLdouble fTop    =  viewRatio;

		GLdouble tDimX = fabs(fRight-fLeft) / totalCols;
		GLdouble tDimY = fabs(fTop-fBottom) / totalRows;


		glOrtho(fLeft   + tDimX * tileCol, fLeft   + tDimX * (tileCol+1),     /* left, right */
						fBottom + tDimY * tileRow, fBottom + tDimY * (tileRow+1),     /* bottom, top */
							 cameraDist - 2.f*clipRatioNear, cameraDist+2.f*clipRatioFar);
	}
	else
	{
		GLdouble fTop    = zNear * tan(math::ToRad(fovY/2.0));
		GLdouble fRight  = fTop * fAspect;
		GLdouble fBottom = -fTop;
		GLdouble fLeft   = -fRight;

		// tile Dimension
		GLdouble tDimX = fabs(fRight-fLeft) / totalCols;
		GLdouble tDimY = fabs(fTop-fBottom) / totalRows;

		glFrustum(fLeft   + tDimX * tileCol, fLeft   + tDimX * (tileCol+1),
							fBottom + tDimY * tileRow, fBottom + tDimY * (tileRow+1), zNear, zFar);
	}
}

void GLArea::updateFps(float deltaTime)
{
 	static float fpsVector[10];
	static int j=0;
	float averageFps=0;
  if (deltaTime>0) {
    fpsVector[j]=deltaTime;
	  j=(j+1) % 10;
  }
	for (int i=0;i<10;i++) averageFps+=fpsVector[i];
	cfps=1000.0f/(averageFps/10);
  lastTime=deltaTime;
}

void GLArea::resetTrackBall()
{
	trackball.Reset();
	float newScale= 3.0f/meshDoc->bbox().Diag();
	trackball.track.sca = newScale;
	trackball.track.tra =  -meshDoc->bbox().Center();
	updateGL();
}

void GLArea::hideEvent(QHideEvent * /*event*/)
{
	trackball.current_button=0;
}

void GLArea::sendViewPos(QString name)
{
  #ifndef VCG_USE_EIGEN
	Point3f pos=  trackball.track.InverseMatrix() *Inverse(trackball.camera.model) *Point3f(0,0,0);
  #else
  Point3f pos=  Eigen::Transform3f(trackball.track.InverseMatrix()) * Eigen::Transform3f(Inverse(trackball.camera.model)).translation();
  #endif
	emit transmitViewPos(name, pos);
}

void GLArea::sendSurfacePos(QString name)
{
  nameToGetPickPos = name;
	hasToGetPickPos=true;
}

void GLArea::sendViewDir(QString name)
{
	Point3f dir= getViewDir();
	emit transmitViewDir(name,dir);
}

void GLArea::sendCameraPos(QString name)
{
		Point3f pos=meshDoc->mm()->cm.shot.GetViewPoint();
	emit transmitViewDir(name, pos);
}


Point3f GLArea::getViewDir()
{
	vcg::Matrix44f rotM;
	trackball.track.rot.ToMatrix(rotM);
	vcg::Invert(rotM);
	return rotM*vcg::Point3f(0,0,1);
}

void GLArea::updateCustomSettingValues( RichParameterSet& rps )
{
    glas.updateGlobalParameterSet(rps);
    this->update();
}

void GLArea::initGlobalParameterSet( RichParameterSet * defaultGlobalParamSet)
{
    GLAreaSetting::initGlobalParameterSet(defaultGlobalParamSet);
}

//Don't alter the state of the other elements in the visibility map
void GLArea::updateLayerSetVisibilities()
{
	//Align visibilityMap state with meshList state
	QMapIterator<int, bool> i(visibilityMap);
	while (i.hasNext()) {
		i.next();
		bool found =false;
		foreach(MeshModel * mp, meshDoc->meshList)
		{
			if(mp->id == i.key())
			{
				found = true;
				break;
			}
		}
		if(!found)
			visibilityMap.remove(i.key());
	}

	foreach(MeshModel * mp, meshDoc->meshList)
	{
		//Insert the new pair in the map; 
		if(!visibilityMap.contains(mp->id))
			visibilityMap.insert(mp->id,mp->visible);
	}
}

void GLArea::updateLayerSetVisibility(int meshId, bool visibility)
{
	visibilityMap.insert(meshId,visibility);
}

// --------------- Methods involving shots -------------------------------------

float GLArea::getCameraDistance()
{
	// This parameter is the one that controls:
	// HOW LARGE IS THE TRACKBALL ICON ON THE SCREEN.
	float viewRatio = 1.75f;
	float cameraDist = viewRatio / tanf(vcg::math::ToRad(fov*.5f));

	return cameraDist;
}

void GLArea::initializeShot(Shot &shot) 
{
	//Da vedere
	shot.Intrinsics.PixelSizeMm[0]=0.036916077;
    shot.Intrinsics.PixelSizeMm[1]=0.036916077;

    shot.Intrinsics.DistorCenterPx[0]=width()/2;
    shot.Intrinsics.DistorCenterPx[1]=height()/2;
    shot.Intrinsics.CenterPx[0]=width()/2;
    shot.Intrinsics.CenterPx[1]=height()/2;
    shot.Intrinsics.ViewportPx[0]=width();
    shot.Intrinsics.ViewportPx[1]=height();

	double viewportYMm = shot.Intrinsics.PixelSizeMm[1]*shot.Intrinsics.ViewportPx[1];
	float defaultFov=60.0;
	shot.Intrinsics.FocalMm = viewportYMm/(2*tanf(vcg::math::ToRad(defaultFov/2))); //27.846098mm

    shot.Extrinsics.SetIdentity();
}

bool GLArea::viewFromFile() 
{
	Shot shot;
	
	QString filename = QFileDialog::getOpenFileName(this, tr("Load Project"), "./", tr("Xml Files (*.xml)"));
	
	QFile qf(filename);
	QFileInfo qfInfo(filename);

	if( !qf.open(QIODevice::ReadOnly ) )
		return false;

	QString project_path = qfInfo.absoluteFilePath();

	QDomDocument doc("XmlDocument");    //It represents the XML document
	if(!doc.setContent( &qf ))	
		return false;

	QString type = doc.doctype().name();

	//TextAlign file project
	if(type == "RegProjectML")
		loadShotFromTextAlignFile(shot, doc);
	//View State file
	else if(type == "ViewState")
		loadViewFromViewStateFile(shot, doc);

	qDebug("End file reading");
	qf.close();

	return true;
}

void GLArea::loadShotFromTextAlignFile(Shot &shot, QDomDocument &doc) 
{
	QDomElement root = doc.documentElement();
	QDomNode node;

	node = root.firstChild();

	//Devices
	while(!node.isNull()){
		if(QString::compare(node.nodeName(),"Device")==0)
		{
			QString type = node.attributes().namedItem("type").nodeValue();
			if (type== "GlImageWidget")
			{
				//Aligned Image
				if(QString::compare(node.attributes().namedItem("aligned").nodeValue(),"1")==0){
					QDomNode nodeb = node.firstChild();
					QDomNamedNodeMap attr = nodeb.attributes();
					vcg::Point3d tra;
					tra[0] = attr.namedItem("SimTra").nodeValue().section(' ',0,0).toDouble();
					tra[1] = attr.namedItem("SimTra").nodeValue().section(' ',1,1).toDouble();
					tra[2] = attr.namedItem("SimTra").nodeValue().section(' ',2,2).toDouble();
					shot.Extrinsics.SetTra(-tra);

					vcg::Matrix44d rot;
					QStringList values =  attr.namedItem("SimRot").nodeValue().split(" ", QString::SkipEmptyParts);
					for(int y = 0; y < 4; y++)
						for(int x = 0; x < 4; x++)
							rot[y][x] = values[x + 4*y].toDouble();
					shot.Extrinsics.SetRot(rot);

					vcg::Camera<double> &cam = shot.Intrinsics;
					cam.FocalMm = attr.namedItem("Focal").nodeValue().toDouble();
					cam.ViewportPx.X() = attr.namedItem("Viewport").nodeValue().section(' ',0,0).toInt();
					cam.ViewportPx.Y() = attr.namedItem("Viewport").nodeValue().section(' ',1,1).toInt();
					cam.CenterPx[0] = attr.namedItem("Center").nodeValue().section(' ',0,0).toInt();
					cam.CenterPx[1] = attr.namedItem("Center").nodeValue().section(' ',1,1).toInt();
					cam.PixelSizeMm[0] = attr.namedItem("ScaleF").nodeValue().section(' ',0,0).toDouble();
					cam.PixelSizeMm[1] = attr.namedItem("ScaleF").nodeValue().section(' ',1,1).toDouble();
					cam.k[0] = attr.namedItem("LensDist").nodeValue().section(' ',0,0).toDouble();
					cam.k[1] = attr.namedItem("LensDist").nodeValue().section(' ',1,1).toDouble();

					// scale correction
					float scorr = attr.namedItem("ScaleCorr").nodeValue().toDouble();
					if(scorr != 0.0) {
						cam.PixelSizeMm[0] *= scorr;
						cam.PixelSizeMm[1] *= scorr;
					}

				}
			}
		}
		node = node.nextSibling();
	}

	//Adjust params for Meshlab settings

	//resize viewport
	int w = shot.Intrinsics.ViewportPx[0];
	int h = shot.Intrinsics.ViewportPx[1];

	shot.Intrinsics.DistorCenterPx[0]=w/2;
	shot.Intrinsics.DistorCenterPx[1]=h/2;
	shot.Intrinsics.CenterPx[0]=w/2;
	shot.Intrinsics.CenterPx[1]=h/2;
	shot.Intrinsics.ViewportPx[0]=w;
	shot.Intrinsics.ViewportPx[1]=h;

	//Compute new scale
	double viewportYMm=shot.Intrinsics.PixelSizeMm[1]*shot.Intrinsics.ViewportPx[1];
	fov = 2*(vcg::math::ToDeg(atanf(viewportYMm/(2*shot.Intrinsics.FocalMm))));

	float cameraDist = getCameraDistance();

	Matrix44f rotFrom;
	shot.Extrinsics.Rot().ToMatrix(rotFrom);

	Point3f p1 = rotFrom*(vcg::Point3f::Construct(shot.Extrinsics.Tra()));

	Point3f p2 = (Point3f(0,0,cameraDist));

	trackball.track.sca =abs(p2.Z()/p1.Z());

	loadShot(QPair<Shot, float> (shot,trackball.track.sca));

}

void GLArea::loadViewFromViewStateFile(Shot &shot, QDomDocument &doc)
{

	QDomElement root = doc.documentElement();
	QDomNode node = root.firstChild();

	while(!node.isNull()){
		if(QString::compare(node.nodeName(),"CamParam")==0)
		{
			QDomNamedNodeMap attr = node.attributes();
			vcg::Point3d tra;
			tra[0] = attr.namedItem("SimTra").nodeValue().section(' ',0,0).toDouble();
			tra[1] = attr.namedItem("SimTra").nodeValue().section(' ',1,1).toDouble();
			tra[2] = attr.namedItem("SimTra").nodeValue().section(' ',2,2).toDouble();
			shot.Extrinsics.SetTra(-tra);

			vcg::Matrix44d rot;
			QStringList values =  attr.namedItem("SimRot").nodeValue().split(" ", QString::SkipEmptyParts);
			for(int y = 0; y < 4; y++)
				for(int x = 0; x < 4; x++)
					rot[y][x] = values[x + 4*y].toDouble();
			shot.Extrinsics.SetRot(rot);

			vcg::Camera<double> &cam = shot.Intrinsics;
			cam.FocalMm = attr.namedItem("Focal").nodeValue().toDouble();
			cam.ViewportPx.X() = attr.namedItem("Viewport").nodeValue().section(' ',0,0).toInt();
			cam.ViewportPx.Y() = attr.namedItem("Viewport").nodeValue().section(' ',1,1).toInt();
			cam.CenterPx[0] = attr.namedItem("Center").nodeValue().section(' ',0,0).toInt();
			cam.CenterPx[1] = attr.namedItem("Center").nodeValue().section(' ',1,1).toInt();
			cam.PixelSizeMm[0] = attr.namedItem("ScaleF").nodeValue().section(' ',0,0).toDouble();
			cam.PixelSizeMm[1] = attr.namedItem("ScaleF").nodeValue().section(' ',1,1).toDouble();
			cam.k[0] = attr.namedItem("LensDist").nodeValue().section(' ',0,0).toDouble();
			cam.k[1] = attr.namedItem("LensDist").nodeValue().section(' ',1,1).toDouble();

			// scale correction
			float scorr = attr.namedItem("ScaleCorr").nodeValue().toDouble();
			if(scorr != 0.0) {
				cam.PixelSizeMm[0] *= scorr;
				cam.PixelSizeMm[1] *= scorr;
			}
		}
		else if (QString::compare(node.nodeName(),"ViewSettings")==0)
		{
			QDomNamedNodeMap attr = node.attributes();
			trackball.track.sca = attr.namedItem("TrackScale").nodeValue().section(' ',0,0).toFloat();
			nearPlane = attr.namedItem("NearPlane").nodeValue().section(' ',0,0).toFloat();
			farPlane = attr.namedItem("FarPlane").nodeValue().section(' ',0,0).toFloat();
		}
		else if (QString::compare(node.nodeName(),"Render")==0)
		{
			QDomNamedNodeMap attr = node.attributes();
			rm.drawMode = (vcg::GLW::DrawMode) (attr.namedItem("DrawMode").nodeValue().section(' ',0,0).toInt());
			rm.colorMode = (vcg::GLW::ColorMode) (attr.namedItem("ColorMode").nodeValue().section(' ',0,0).toInt());
			rm.textureMode = (vcg::GLW::TextureMode) (attr.namedItem("TextureMode").nodeValue().section(' ',0,0).toInt());
			rm.lighting = (attr.namedItem("Lighting").nodeValue().section(' ',0,0).toInt() != 0);
			rm.backFaceCull = (attr.namedItem("BackFaceCull").nodeValue().section(' ',0,0).toInt() != 0);
			rm.doubleSideLighting = (attr.namedItem("DoubleSideLighting").nodeValue().section(' ',0,0).toInt() != 0);
			rm.fancyLighting = (attr.namedItem("FancyLighting").nodeValue().section(' ',0,0).toInt() != 0);
			rm.selectedFace = (attr.namedItem("SelectedFace").nodeValue().section(' ',0,0).toInt() != 0);
			rm.selectedVert = (attr.namedItem("SelectedVert").nodeValue().section(' ',0,0).toInt() != 0);
		}
		node = node.nextSibling();
	}

	loadShot(QPair<Shot, float> (shot,trackball.track.sca));
}

void GLArea::viewToClipboard()
{
	QClipboard *clipboard = QApplication::clipboard();
	Shot shot = shotFromTrackball().first;

	QDomDocument doc("ViewState");
	QDomElement root = doc.createElement("project");
	doc.appendChild( root );
	QDomElement shotElem = doc.createElement( "CamParam" );

	vcg::Point3d tra = -(shot.Extrinsics.Tra());
	QString str = QString("%1 %2 %3 1").arg(tra[0]).arg(tra[1]).arg(tra[2]);
	shotElem.setAttribute("SimTra", str);
	vcg::Matrix44d rot = shot.Extrinsics.Rot();
	str = QString("%1 %2 %3 %4 %5 %6 %7 %8 %9 %10 %11 %12 %13 %14 %15 %16 ")
		.arg(rot[0][0]).arg(rot[0][1])
		.arg(rot[0][2]).arg(rot[0][3])
		.arg(rot[1][0]).arg(rot[1][1])
		.arg(rot[1][2]).arg(rot[1][3])
		.arg(rot[2][0]).arg(rot[2][1])
		.arg(rot[2][2]).arg(rot[2][3])
		.arg(rot[3][0]).arg(rot[3][1])
		.arg(rot[3][2]).arg(rot[3][3]);
	shotElem.setAttribute( "SimRot", str);

	vcg::Camera<double> &cam = shot.Intrinsics;

	shotElem.setAttribute( "Focal", cam.FocalMm);

	str = QString("%1 %2").arg(cam.k[0]).arg(cam.k[1]);
	shotElem.setAttribute( "LensDist", str);

	str = QString("%1 %2").arg(cam.PixelSizeMm[0]).arg(cam.PixelSizeMm[1]);
	shotElem.setAttribute( "ScaleF", str);

	str = QString("%1 %2").arg(cam.ViewportPx[0]).arg(cam.ViewportPx[1]);
	shotElem.setAttribute( "Viewport", str);

	str = QString("%1 %2").arg((int)(cam.DistorCenterPx[0])).arg((int)(cam.DistorCenterPx[1]));
	shotElem.setAttribute( "Center", str);

	str = QString("%1").arg((double) 1);
	shotElem.setAttribute( "ScaleCorr", str);

	root.appendChild(shotElem);

	QDomElement settingsElem = doc.createElement( "ViewSettings" );
	settingsElem.setAttribute( "TrackScale", trackball.track.sca);
	settingsElem.setAttribute( "NearPlane", nearPlane);
	settingsElem.setAttribute( "FarPlane", farPlane);
	root.appendChild(settingsElem);


	QDomElement renderElem = doc.createElement( "Render");
	renderElem.setAttribute("DrawMode",rm.drawMode);
	renderElem.setAttribute("ColorMode",rm.colorMode);
	renderElem.setAttribute("TextureMode",rm.textureMode);
	renderElem.setAttribute("Lighting",rm.lighting);
	renderElem.setAttribute("BackFaceCull",rm.backFaceCull);
	renderElem.setAttribute("DoubleSideLighting",rm.doubleSideLighting);
	renderElem.setAttribute("FancyLighting",rm.fancyLighting);
	renderElem.setAttribute("SelectedFace",rm.selectedFace);
	renderElem.setAttribute("SelectedVert",rm.selectedVert);
	root.appendChild(renderElem);

	clipboard->setText(doc.toString()); //.remove(QChar('\n')));
}

void GLArea::viewFromClipboard()
{
	QClipboard *clipboard = QApplication::clipboard();
	QString shotString = clipboard->text();

	Shot shot;

	QDomDocument doc("StringDoc");  
	doc.setContent(shotString);

	loadViewFromViewStateFile(shot, doc);
}

QPair<vcg::Shot<double>,float> GLArea::shotFromTrackball()
{
	Shot shot;
	initializeShot(shot);

	double viewportYMm=shot.Intrinsics.PixelSizeMm[1]*shot.Intrinsics.ViewportPx[1];
	shot.Intrinsics.FocalMm = viewportYMm/(2*tanf(vcg::math::ToRad(fov/2)));

	float cameraDist = getCameraDistance();

	//add the translation introduced by gluLookAt() (0,0,cameraDist)---------------------------------------
	//T(gl)*S*R*T(t) => SR(gl+t) => S R (S^(-1)R^(-1)gl + t)
	//Add translation S^(-1) R^(-1)(gl)
	//Shot doesn't introduce scaling
	//---------------------------------------------------------------------
	shot.Extrinsics.SetTra( shot.Extrinsics.Tra() + (Inverse(shot.Extrinsics.Rot())*Point3d(0, 0, cameraDist)));
	
	Shot newShot = track2ShotCPU(shot, &trackball);

	////Expressing scaling as a translation along z
	////k is the ratio between default scale and new scale
	//double oldScale= 3.0f/meshDoc->bbox().Diag();
	//double k= oldScale/trackball.track.sca;

	////Apply this formula
	//// R(t+p) = kR'(t'+p) forall p, R=R', k is a costant
	//// R(t)= kR(t')
	//// t = k*t'
	//Point3d t1 = newShot.Extrinsics.Tra();
	//
	//Matrix44d rapM = Matrix44d().SetScale(k, k, k);
	//Point3d t0 = rapM*t1;

	//newShot.Extrinsics.SetTra(t0);

	return QPair<Shot, float> (newShot,trackball.track.sca);
}

void GLArea::loadShot(const QPair<vcg::Shot<double>,float> &shotAndScale){
	
	Shot shot = shotAndScale.first;
	
	double viewportYMm=shot.Intrinsics.PixelSizeMm[1]*shot.Intrinsics.ViewportPx[1];
	fov = 2*(vcg::math::ToDeg(atanf(viewportYMm/(2*shot.Intrinsics.FocalMm))));

	float cameraDist = getCameraDistance();

	//reset trackball. The point of view must be set only by the shot
	trackball.Reset();
	trackball.track.sca = shotAndScale.second;
	
	/*Point3f point = meshDoc->bbox().Center();
	Point3f p1 = ((trackball.track.Matrix()*(point-trackball.center))- Point3f(0,0,cameraDist));*/
	shot2Track(shot, cameraDist,trackball);

	//Expressing the translation along Z with a scale factor k
	//Point3f p2 = ((trackball.track.Matrix()*(point-trackball.center))- Point3f(0,0,cameraDist));

	////k is the ratio between the distances along z of two correspondent points (before and after the traslazion) 
	////from the point of view
	//float k= abs(p2.Z()/p1.Z());

	//float sca= trackball.track.sca/k;
	//Point3f tra = trackball.track.tra;
	//
	//// Apply this formula:
	//// SR(t+p) -v = k[S'R'(t'+p) -v] forall p, R=R', k is a costant
	//// SR(t) -v = k[S'R(t') -v]
	//// t' = 1/k* S'^-1St + (k-1)/k S'^-1*R^-1v
	//Matrix44f s0 = Matrix44f().SetScale(trackball.track.sca,trackball.track.sca, trackball.track.sca);
	//Matrix44f s1 = Matrix44f().SetScale(sca, sca, sca);
	//Matrix44f r;
	//trackball.track.rot.ToMatrix(r);
	//Matrix44f rapM = Matrix44f().SetScale(1/k, 1/k, 1/k);
	//Matrix44f rapM2 = Matrix44f().SetScale(1-1/k, 1-1/k, 1-1/k);
	//Point3f t1 = rapM*Inverse(s1)*s0*tra + rapM2*Inverse(s1)*Inverse(r)*Point3f(0,0,cameraDist);

	//trackball.track.sca =sca;
	//trackball.track.tra =t1 /*+ tb.track.rot.Inverse().Rotate(glLookAt)*/ ;

	update();
}

void GLArea::createOrthoView(QString dir)
{
	Shot view;
	initializeShot(view);

	fov =5;
	double viewportYMm = view.Intrinsics.PixelSizeMm[1]*view.Intrinsics.ViewportPx[1];
	view.Intrinsics.FocalMm = viewportYMm/(2*tanf(vcg::math::ToRad(fov/2))); //27.846098 equivalente a circa 60 gradi

	trackball.Reset();
	float newScale= 3.0f/meshDoc->bbox().Diag();
	trackball.track.sca = newScale;
	trackball.track.tra =  -meshDoc->bbox().Center();

	vcg::Matrix44d rot;

	if(dir == tr("Top"))
		rot.SetRotateDeg(90,Point3<double>(1,0,0));
	else if(dir == tr("Bottom"))
		rot.SetRotateDeg(90,Point3<double>(-1,0,0));
	else if(dir == tr("Left"))
		rot.SetRotateDeg(90,Point3<double>(0,1,0));
	else if(dir == tr("Right"))
		rot.SetRotateDeg(90,Point3<double>(0,-1,0));
	else if(dir == tr("Front"))
		rot.SetRotateDeg(0,Point3<double>(0,1,0));
	else if(dir == tr("Back"))
		rot.SetRotateDeg(180,Point3<double>(0,1,0));

	view.Extrinsics.SetRot(rot);

	float cameraDist = getCameraDistance();

	//add the translation introduced by gluLookAt() (0,0,cameraDist)---------------------------------------
	//T(gl)*S*R*T(t) => SR(gl+t) => S R (S^(-1)R^(-1)gl + t)
	//Add translation S^(-1) R^(-1)(gl)
	//Shot doesn't introduce scaling
	//---------------------------------------------------------------------
	view.Extrinsics.SetTra( view.Extrinsics.Tra() + (Inverse(view.Extrinsics.Rot())*Point3d(0, 0, cameraDist)));

	Shot shot = track2ShotCPU(view, &trackball);
	
	QPair<Shot,float> shotAndScale = QPair<Shot,float> (shot, trackball.track.sca);
	loadShot(shotAndScale);
}


