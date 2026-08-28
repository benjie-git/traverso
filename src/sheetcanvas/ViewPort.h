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

*/

#ifndef VIEWPORT_H
#define VIEWPORT_H

#include <QGraphicsView>
#include <QGraphicsItem>
#include <QEnterEvent>
#include "ContextItem.h"
#include "AbstractViewPort.h"

class ViewItem;
class SheetView;
class ContextItem;
class Import;
class AudioTrack;
class HoldCursor;
class QGraphicsTextItem;

class ViewPort : public QGraphicsView, public AbstractViewPort
{
    Q_OBJECT

public :
    ViewPort(QGraphicsScene* scene, QWidget* parent);
    virtual ~ViewPort();

    // Set functions
    void set_canvas_cursor_text(const QString& text, int mseconds=-1) override;
    void set_canvas_cursor_pos(QPointF pos, CursorMoveReason reason) override;
    void set_canvas_cursor_shape(const QString& shape, int alignment=Qt::AlignCenter) override;
    virtual void set_sheetview(SheetView* view) {m_sv = view;}

    inline QPointF map_to_scene(const QPoint& pos) const override {
        return mapToScene(pos);
    }

    inline QPoint map_to_global(const QPoint& point) const override {
        return mapToGlobal(point);
    }

    void grab_mouse() override;
    void release_mouse() override;

    void detect_items_below_cursor() override;



protected:
    virtual bool event(QEvent *event) override;
    virtual void enterEvent ( QEnterEvent * ) override;
    virtual void leaveEvent ( QEvent * ) override;
    virtual void paintEvent( QPaintEvent* e) override;
    virtual void mouseMoveEvent(QMouseEvent* e) override;
    virtual void mousePressEvent ( QMouseEvent * e ) override;
    virtual void mouseReleaseEvent ( QMouseEvent * e ) override;
    virtual void mouseDoubleClickEvent ( QMouseEvent * e ) override;
    virtual void wheelEvent ( QWheelEvent* e ) override;
    virtual void keyPressEvent ( QKeyEvent* e) override;
    virtual void keyReleaseEvent ( QKeyEvent* e) override;
    virtual void dragEnterEvent(QDragEnterEvent *event) override;
    virtual void dragMoveEvent(QDragMoveEvent *event) override;


    void tabletEvent ( QTabletEvent * event ) override;

    SheetView* m_sv;

private:
    QPoint      m_oldMousePos;
};

#endif

//eof
