// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors

// editor_widget.cpp — FEMM geometry editor widget
// Interaction model ported from femm42src/femm/FemmeView.cpp:
//   - Left click: create entity (node, segment endpoint, block label, arc endpoint)
//   - Right click: toggle-select nearest entity matching current EditAction
//   - Space: open properties for selected entities
//   - Middle drag: pan
//   - Wheel: zoom toward cursor

#include "editor_widget.h"
#include "coord_display.h"
#include "zoom_fit.h"
#include "document.h"
#include "mag_document.h"
#include "elec_document.h"
#include "py_state.h"
#include "script_callbacks.h"
#include "prop_dialogs.h"
#include "arc_dialog.h"
#include "view_colors.h"

#include <QPainter>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QInputDialog>
#include <QMessageBox>
#include "mouse_release_gate.h"
#include <QKeyEvent>
#include <QFormLayout>
#include <QVBoxLayout>
#include <QLineEdit>
#include "py_line_edit.h"
#include <QDialogButtonBox>
#include <QDialog>
#include <cmath>

#ifndef PI
#define PI 3.141592653589793238462643383279
#endif

// Colors are now pulled from viewColors() global singleton

// ================================================================
// Constructor
// ================================================================

EditorWidget::EditorWidget(DocumentBase *doc, QWidget *parent)
    : QWidget(parent)
    , m_doc(doc)
{
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setMinimumSize(200, 200);
}

EditorWidget::~EditorWidget()
{
    // EditorWidget owns its document. Delete it when the tab is closed.
    // The Lua binding pointer (s_doc in lua_*_editor.cpp) is updated
    // automatically: Qt emits subWindowActivated for the next tab BEFORE
    // this destructor runs (direct-connection signal), so s_doc already
    // points at the replacement document by the time we delete the old one.
    doc_registry_remove(m_doc);
    delete m_doc;
    m_doc = nullptr;
}

// ================================================================
// Coordinate transforms
// ================================================================

QPointF EditorWidget::toScreen(double xd, double yd) const
{
    double sx = m_mag * (xd - m_ox);
    double sy = (height() - 1) - m_mag * (yd - m_oy);
    return QPointF(sx, sy);
}

void EditorWidget::toDrawing(int xs, int ys, double &xd, double &yd) const
{
    xd = xs / m_mag + m_ox;
    yd = (height() - 1 - ys) / m_mag + m_oy;
}

void EditorWidget::applySnap(double &x, double &y) const
{
    if (m_snapToGrid && m_gridSize > 0) {
        x = m_gridSize * round(x / m_gridSize);
        y = m_gridSize * round(y / m_gridSize);
    }
}

// ================================================================
// Edit action
// ================================================================

void EditorWidget::setEditAction(int action)
{
    if (m_doc) m_doc->UnselectAll();
    m_editAction = action;
    m_firstPoint = -1;
    emit editActionChanged(action);
    update();
}

void EditorWidget::deleteSelected()
{
    if (!m_doc) return;
    m_doc->pushUndo();
    m_doc->DeleteSelectedSegments();
    m_doc->DeleteSelectedArcSegments();
    m_doc->DeleteSelectedNodes();
    m_doc->DeleteSelectedBlockLabels();
    update();
}

// ================================================================
// Zoom / pan
// ================================================================

void EditorWidget::zoomIn()
{
    double cx = width() / 2.0 / m_mag + m_ox;
    double cy = height() / 2.0 / m_mag + m_oy;
    m_mag *= 1.5;
    m_ox = cx - width() / 2.0 / m_mag;
    m_oy = cy - height() / 2.0 / m_mag;
    update();
}

void EditorWidget::zoomOut()
{
    double cx = width() / 2.0 / m_mag + m_ox;
    double cy = height() / 2.0 / m_mag + m_oy;
    m_mag /= 1.5;
    if (m_mag < 0.01) m_mag = 0.01;
    m_ox = cx - width() / 2.0 / m_mag;
    m_oy = cy - height() / 2.0 / m_mag;
    update();
}

void EditorWidget::zoomFit()
{
    if (!m_doc || m_doc->nodelist.empty()) {
        m_ox = -width() / 2.0 / m_mag;
        m_oy = -height() / 2.0 / m_mag;
        update();
        return;
    }

    double xmin, ymin, xmax, ymax;
    if (!m_doc->getBoundingBox(xmin, ymin, xmax, ymax)) return;

    double dx = xmax - xmin, dy = ymax - ymin;
    if (dx < 1e-12) dx = 1.0;
    if (dy < 1e-12) dy = 1.0;

    double margin = kZoomFitMargin;   // shared; see zoom_fit.h
    xmin -= dx * margin; xmax += dx * margin;
    ymin -= dy * margin; ymax += dy * margin;
    dx = xmax - xmin; dy = ymax - ymin;

    double mx = (width() - 1.0) / dx;
    double my = (height() - 1.0) / dy;
    m_mag = (mx < my) ? mx : my;
    m_ox = xmin;
    m_oy = ymin;
    update();
}

void EditorWidget::zoomTo(double x0, double y0, double x1, double y1)
{
    if (x0 > x1) std::swap(x0, x1);
    if (y0 > y1) std::swap(y0, y1);
    double dx = x1 - x0, dy = y1 - y0;
    if (dx < 1e-12 || dy < 1e-12) return;
    double mx = width() / dx;
    double my = height() / dy;
    m_mag = std::min(mx, my);
    m_ox = x0;
    m_oy = y0;
    update();
}

void EditorWidget::viewBounds(double &x0, double &y0, double &x1, double &y1) const
{
    toDrawing(0, height() - 1, x0, y0);
    toDrawing(width() - 1, 0, x1, y1);
}

void EditorWidget::zoomWindow()
{
    m_zoomWndFlag = 1;
    emit statusMessage("Zoom: click and drag to define window");
}

void EditorWidget::selectWindow()
{
    m_selectWndFlag = 1;
    emit statusMessage("Select window: drag to select area");
}

void EditorWidget::selectCircle()
{
    m_selectCircFlag = 1;
    emit statusMessage("Select circle: click center, drag to set radius");
}

void EditorWidget::panLeft()
{
    m_ox -= 0.25 * width() / m_mag;
    update();
}
void EditorWidget::panRight()
{
    m_ox += 0.25 * width() / m_mag;
    update();
}
void EditorWidget::panUp()
{
    m_oy += 0.25 * height() / m_mag;
    update();
}
void EditorWidget::panDown()
{
    m_oy -= 0.25 * height() / m_mag;
    update();
}

// ================================================================
// Paint
// ================================================================

void EditorWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.fillRect(rect(), viewColors().background());

    if (!m_doc) return;

    if (m_showGrid) drawGrid(p);
    hideMeshIfGeometryChanged();
    if (m_showMesh) drawMesh(p);
    drawSegments(p);
    drawArcSegments(p);
    drawNodes(p);
    drawBlockLabels(p);

    // Rubber band for zoom/select window
    if (m_zoomWndFlag == 2 || m_selectWndFlag == 2) {
        QPoint curScreen = mapFromGlobal(QCursor::pos());
        QRect rb = QRect(m_rubberStart, curScreen).normalized();
        p.setPen(QPen(Qt::blue, 1, Qt::DashLine));
        p.setBrush(Qt::NoBrush);
        p.drawRect(rb);
    }

    // Rubber band for select circle
    if (m_selectCircFlag == 2) {
        QPoint curScreen = mapFromGlobal(QCursor::pos());
        int dx = curScreen.x() - m_rubberStart.x();
        int dy = curScreen.y() - m_rubberStart.y();
        int r = (int)std::sqrt((double)(dx * dx + dy * dy));
        p.setPen(QPen(Qt::blue, 1, Qt::DashLine));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(m_rubberStart, r, r);
    }
}

void EditorWidget::drawGrid(QPainter &p)
{
    if (m_gridSize <= 0) return;

    // Like original FEMM: draw dots at grid intersections.
    // If the grid would be too dense (< 8 pixels apart), don't draw.
    double pixelSpacing = m_gridSize * m_mag;
    if (pixelSpacing < 8) return;

    double x0d, y0d, x1d, y1d;
    toDrawing(0, height() - 1, x0d, y0d);
    toDrawing(width() - 1, 0, x1d, y1d);

    double gx = m_gridSize * floor(x0d / m_gridSize);
    double gy = m_gridSize * floor(y0d / m_gridSize);

    // Safety limit
    int nx = (int)((x1d - gx) / m_gridSize) + 1;
    int ny = (int)((y1d - gy) / m_gridSize) + 1;
    if ((long)nx * ny > 250000) return;

    p.setPen(QPen(viewColors().grid(), 2, Qt::SolidLine, Qt::RoundCap));
    for (double x = gx; x <= x1d; x += m_gridSize) {
        for (double y = gy; y <= y1d; y += m_gridSize) {
            QPointF sp = toScreen(x, y);
            p.drawPoint(sp);
        }
    }
}

void EditorWidget::drawNodes(QPainter &p)
{
    // Nodes are small squares (4x4 pixels), matching original FEMM
    for (auto &n : m_doc->nodelist) {
        QPointF sp = toScreen(n.x, n.y);
        QColor c = n.IsSelected ? viewColors().selected() : viewColors().node();
        p.setPen(QPen(c, 1));
        p.setBrush(Qt::NoBrush);
        p.drawRect(QRectF(sp.x() - 2, sp.y() - 2, 4, 4));
    }
}

void EditorWidget::drawSegments(QPainter &p)
{
    for (auto &seg : m_doc->linelist) {
        if (seg.n0 < 0 || seg.n0 >= (int)m_doc->nodelist.size()) continue;
        if (seg.n1 < 0 || seg.n1 >= (int)m_doc->nodelist.size()) continue;

        QPointF p0 = toScreen(m_doc->nodelist[seg.n0].x, m_doc->nodelist[seg.n0].y);
        QPointF p1 = toScreen(m_doc->nodelist[seg.n1].x, m_doc->nodelist[seg.n1].y);

        p.setPen(QPen(seg.IsSelected ? viewColors().selected() : viewColors().line(), 1));
        p.drawLine(p0, p1);
    }
}

void EditorWidget::drawArcSegments(QPainter &p)
{
    for (auto &arc : m_doc->arclist) {
        if (arc.n0 < 0 || arc.n0 >= (int)m_doc->nodelist.size()) continue;
        if (arc.n1 < 0 || arc.n1 >= (int)m_doc->nodelist.size()) continue;

        CComplex center;
        double R;
        m_doc->GetCircle(arc, center, R);

        CComplex a0 = m_doc->nodelist[arc.n0].CC();

        double startAngle = atan2((a0 - center).im, (a0 - center).re);

        // Step through arc using the same rotation approach as original FEMM
        double mySideLength = arc.mySideLength;
        if (mySideLength <= 0) mySideLength = arc.MaxSideLength;
        if (mySideLength <= 0) mySideLength = 5.0;
        int k = (int)ceil(arc.ArcLength / mySideLength);
        if (k < 1) k = 1;
        double dt = arc.ArcLength * PI / (180.0 * k);

        p.setPen(QPen(arc.IsSelected ? viewColors().selected() : viewColors().line(), 1));

        CComplex pt = a0;
        QPointF prev = toScreen(pt.re, pt.im);
        CComplex rot(cos(dt), sin(dt));
        for (int i = 0; i < k; i++) {
            pt = (pt - center) * rot + center;
            QPointF cur = toScreen(pt.re, pt.im);
            p.drawLine(prev, cur);
            prev = cur;
        }
    }
}

void EditorWidget::drawBlockLabels(QPainter &p)
{
    QFont font = p.font();
    font.setPointSize(8);
    p.setFont(font);

    for (auto &blk : m_doc->blocklist) {
        QPointF sp = toScreen(blk.x, blk.y);
        QColor c = blk.IsSelected ? viewColors().selected() : viewColors().blockLabel();

        // Small square (4x4 pixels) like original
        p.setPen(QPen(c, 1));
        p.setBrush(Qt::NoBrush);
        p.drawRect(QRectF(sp.x() - 2, sp.y() - 2, 4, 4));

        // Area constraint circle
        if (blk.MaxArea > 0) {
            double side = sqrt(blk.MaxArea / PI);
            int r = (int)ceil(side * m_mag);
            if (r > 2 && r < 1000) {
                p.setPen(QPen(c, 1, Qt::DotLine));
                p.drawEllipse(sp, r, r);
            }
        }

        // Magnetization direction arrow for PM materials
        if (blk.MagDirFctn.empty() &&
            m_doc->materialCoercivity(blk.BlockType) != 0) {
            double theta = blk.MagDir * PI / 180.0;
            double ct = cos(theta), st = sin(theta);
            QPointF p1(sp.x() - 10*ct, sp.y() + 10*st);
            QPointF p2(sp.x() + 10*ct, sp.y() - 10*st);
            p.setPen(QPen(c, 1));
            p.drawLine(p1, p2);
            // Arrowhead (6-pixel arms at 45 degrees)
            double ax = 6.0 / sqrt(2.0);
            QPointF ah1(p2.x() - ax*(ct - st), p2.y() + ax*(st + ct));
            QPointF ah2(p2.x() - ax*(ct + st), p2.y() + ax*(st - ct));
            p.drawLine(p2, ah1);
            p.drawLine(p2, ah2);
        }

        // Label text
        if (m_showNames) {
            p.setPen(viewColors().name());
            // Material name; a label with no material assigned shows "<None>"
            // (matching OF) so it can't be mistaken for a bare node.
            QString matText = (blk.BlockType.empty() || blk.BlockType == "<None>")
                              ? QStringLiteral("<None>")
                              : QString::fromStdString(blk.BlockType);
            p.drawText(QPointF(sp.x() + 5, sp.y() - 2), matText);
            int yoff = 12;
            // Circuit info: [CircuitName:Turns]
            if (!blk.InCircuit.empty() && blk.InCircuit != "<None>") {
                QString circText = QString("[%1:%2]")
                    .arg(QString::fromStdString(blk.InCircuit))
                    .arg(blk.Turns);
                p.drawText(QPointF(sp.x() + 5, sp.y() - 2 + yoff), circText);
            }
        }
    }
}

// ================================================================
// Mouse events — matching original FEMM behavior
// ================================================================

void EditorWidget::mousePressEvent(QMouseEvent *event)
{
    if (!m_doc) return;

    toDrawing(event->pos().x(), event->pos().y(), m_mx, m_my);

    // ---- Middle button: pan ----
    if (event->button() == Qt::MiddleButton) {
        m_panning = true;
        m_panStart = event->pos();
        m_panOxStart = m_ox;
        m_panOyStart = m_oy;
        setCursor(Qt::ClosedHandCursor);
        return;
    }

    // ---- Left button: create entity or start zoom/select window ----
    if (event->button() == Qt::LeftButton) {
        double cx = m_mx, cy = m_my;
        applySnap(cx, cy);

        // Zoom window mode
        if (m_zoomWndFlag == 1) {
            m_zoomWndFlag = 2;
            m_wndX0 = m_mx; m_wndY0 = m_my;
            m_rubberStart = event->pos();
            return;
        }
        if (m_zoomWndFlag == 2) return;

        // Select window mode
        if (m_selectWndFlag == 1) {
            m_selectWndFlag = 2;
            m_wndX0 = m_mx; m_wndY0 = m_my;
            m_rubberStart = event->pos();
            return;
        }
        if (m_selectWndFlag == 2) return;

        // Select circle mode
        if (m_selectCircFlag == 1) {
            m_selectCircFlag = 2;
            m_wndX0 = m_mx; m_wndY0 = m_my;
            m_rubberStart = event->pos();
            return;
        }
        if (m_selectCircFlag == 2) return;

        // EditAction 0: Add node
        if (m_editAction == 0) {
            m_doc->pushUndo();
            m_doc->AddNode(cx, cy, 1.0 / m_mag);
            update();
        }

        // EditAction 1: Add segment (two-click)
        if (m_editAction == 1) {
            if (m_firstPoint < 0) {
                if (m_doc->nodelist.empty()) return;
                m_firstPoint = m_doc->ClosestNode(cx, cy);
                m_doc->nodelist[m_firstPoint].ToggleSelect();
                update();
            } else {
                m_doc->pushUndo();
                m_doc->nodelist[m_firstPoint].ToggleSelect();
                int n1 = m_doc->ClosestNode(cx, cy);
                m_doc->AddSegment(m_firstPoint, n1);
                m_firstPoint = -1;
                update();
            }
        }

        // EditAction 2: Add block label
        if (m_editAction == 2) {
            m_doc->pushUndo();
            m_doc->AddBlockLabel(cx, cy, 1.0 / m_mag);
            update();
        }

        // EditAction 3: Add arc segment (two-click + dialog)
        if (m_editAction == 3) {
            if (m_firstPoint < 0) {
                if (m_doc->nodelist.empty()) return;
                m_firstPoint = m_doc->ClosestNode(cx, cy);
                m_doc->nodelist[m_firstPoint].ToggleSelect();
                update();
            } else {
                int n1 = m_doc->ClosestNode(cx, cy);
                int n0 = m_firstPoint;
                m_doc->nodelist[n0].ToggleSelect();
                m_firstPoint = -1;
                update();
                if (n1 >= 0 && n1 != n0) {
                    // After the button-up, never here: mouse_release_gate.cpp.
                    femmAfterMouseRelease(this, [this, n0, n1]() {
                        if (!m_doc || n0 >= (int)m_doc->nodelist.size() ||
                            n1 >= (int)m_doc->nodelist.size()) return;
                        // Collect boundary names from document
                        std::vector<std::string> bdryNames;
                        for (int i = 0; i < m_doc->numBoundaries(); i++)
                            bdryNames.push_back(m_doc->boundaryName(i));

                        ArcDialog dlg(bdryNames, m_arcAngle, m_arcMaxSeg,
                                      m_arcBdryIdx, this);
                        if (dlg.exec() == QDialog::Accepted) {
                            m_arcAngle = dlg.arcAngle();
                            m_arcMaxSeg = dlg.maxSeg();
                            m_arcBdryIdx = dlg.boundaryIndex();

                            m_doc->pushUndo();
                            FArcSegment asegm;
                            asegm.n0 = n0;
                            asegm.n1 = n1;
                            asegm.ArcLength = m_arcAngle;
                            asegm.MaxSideLength = m_arcMaxSeg;
                            if (m_arcBdryIdx > 0 && m_arcBdryIdx - 1 < (int)bdryNames.size())
                                asegm.BoundaryMarker = bdryNames[m_arcBdryIdx - 1];
                            m_doc->AddArcSegment(asegm);
                            update();
                        }
                    });
                }
            }
        }

        // EditAction 4: Group mode — left click does nothing special
    }

    // ---- Right button: toggle-select nearest entity ----
    if (event->button() == Qt::RightButton) {
        // Cancel any in-progress two-click operation
        if (m_firstPoint >= 0) {
            m_doc->nodelist[m_firstPoint].IsSelected = false;
            m_firstPoint = -1;
            update();
            return;
        }

        if (m_editAction == 0) {
            // Select nearest node
            int j = m_doc->ClosestNode(m_mx, m_my);
            if (j >= 0) m_doc->nodelist[j].ToggleSelect();
        }
        else if (m_editAction == 1) {
            // Select nearest segment
            int j = m_doc->ClosestSegment(m_mx, m_my);
            if (j >= 0) m_doc->linelist[j].ToggleSelect();
        }
        else if (m_editAction == 2) {
            // Select nearest block label
            int j = m_doc->ClosestBlockLabel(m_mx, m_my);
            if (j >= 0) m_doc->blocklist[j].ToggleSelect();
        }
        else if (m_editAction == 3) {
            // Select nearest arc segment
            int j = m_doc->ClosestArcSegment(m_mx, m_my);
            if (j >= 0) m_doc->arclist[j].ToggleSelect();
        }
        else if (m_editAction == 4) {
            // Group mode: find closest entity of any type, toggle its group
            int clnode = m_doc->ClosestNode(m_mx, m_my);
            int clseg = m_doc->ClosestSegment(m_mx, m_my);
            int claseg = m_doc->ClosestArcSegment(m_mx, m_my);
            int cllbl = m_doc->ClosestBlockLabel(m_mx, m_my);

            double dnode = (clnode >= 0) ? m_doc->nodelist[clnode].GetDistance(m_mx, m_my) : 1e30;
            double dseg = (clseg >= 0) ? m_doc->ShortestDistance(m_mx, m_my, clseg) : 1e30;
            double daseg = (claseg >= 0) ? m_doc->ShortestDistanceFromArc(CComplex(m_mx, m_my), m_doc->arclist[claseg]) : 1e30;
            double dlbl = (cllbl >= 0) ? m_doc->blocklist[cllbl].GetDistance(m_mx, m_my) : 1e30;

            int grp = -1;
            double best = 1e30;
            if (dnode < best) { best = dnode; grp = m_doc->nodelist[clnode].InGroup; }
            if (dseg < best) { best = dseg; grp = m_doc->linelist[clseg].InGroup; }
            if (daseg < best) { best = daseg; grp = m_doc->arclist[claseg].InGroup; }
            if (dlbl < best) { best = dlbl; grp = m_doc->blocklist[cllbl].InGroup; }

            if (grp >= 1) {
                for (auto &n : m_doc->nodelist)
                    if (n.InGroup == grp) n.ToggleSelect();
                for (auto &s : m_doc->linelist)
                    if (s.InGroup == grp) s.ToggleSelect();
                for (auto &a : m_doc->arclist)
                    if (a.InGroup == grp) a.ToggleSelect();
                for (auto &b : m_doc->blocklist)
                    if (b.InGroup == grp) b.ToggleSelect();
            }
        }
        update();
    }
}

void EditorWidget::mouseMoveEvent(QMouseEvent *event)
{
    toDrawing(event->pos().x(), event->pos().y(), m_mx, m_my);

    // Show snapped coordinates in the status bar
    double dx = m_mx, dy = m_my;
    applySnap(dx, dy);
    emit coordinatesChanged(dx, dy);

    if (m_panning) {
        double dx = (event->pos().x() - m_panStart.x()) / m_mag;
        double dy = -(event->pos().y() - m_panStart.y()) / m_mag;
        m_ox = m_panOxStart - dx;
        m_oy = m_panOyStart - dy;
        update();
    }

    // Rubber band update for zoom/select window/circle
    if (m_zoomWndFlag == 2 || m_selectWndFlag == 2 || m_selectCircFlag == 2)
        update();
}

void EditorWidget::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (!m_doc || event->button() != Qt::RightButton) {
        QWidget::mouseDoubleClickEvent(event);
        return;
    }

    double xd, yd;
    toDrawing(event->pos().x(), event->pos().y(), xd, yd);
    QString info;

    if (m_editAction == 0) {
        int i = m_doc->ClosestNode(xd, yd);
        if (i >= 0) {
            auto &n = m_doc->nodelist[i];
            info = QString("Closest node:  (%1,%2)\n").arg(n.x).arg(n.y);
            info += QString("Nodal Property:  %1\n")
                .arg(n.BoundaryMarker.empty() ? "<None>" : QString::fromStdString(n.BoundaryMarker));
            info += QString("In Group:  %1").arg(n.InGroup);
            n.ToggleSelect();
        }
    }
    else if (m_editAction == 1) {
        int i = m_doc->ClosestSegment(xd, yd);
        if (i >= 0) {
            auto &s = m_doc->linelist[i];
            auto &n0 = m_doc->nodelist[s.n0];
            auto &n1 = m_doc->nodelist[s.n1];
            double len = std::sqrt((n1.x - n0.x) * (n1.x - n0.x) +
                                   (n1.y - n0.y) * (n1.y - n0.y));
            info = QString("Length of closest segment:  %1\n").arg(len);
            info += QString("Boundary Property:  %1\n")
                .arg(s.BoundaryMarker.empty() ? "<None>" : QString::fromStdString(s.BoundaryMarker));
            info += QString("Grid spacing:  %1\n")
                .arg(s.MaxSideLength < 0 ? "<Auto>" : QString::number(s.MaxSideLength));
            info += QString("In Group:  %1").arg(s.InGroup);
            if (s.Hidden) info += "\nHidden in postprocessor";
            s.ToggleSelect();
        }
    }
    else if (m_editAction == 2) {
        int i = m_doc->ClosestBlockLabel(xd, yd);
        if (i >= 0) {
            auto &b = m_doc->blocklist[i];
            double meshSize = (b.MaxArea > 0) ?
                floor(2e7 * sqrt(b.MaxArea / PI) + 0.5) / 1e7 : 0;
            info = QString("Closest block label:  (%1,%2)\n").arg(b.x).arg(b.y);
            info += QString("Block Material:  %1\n")
                .arg(b.BlockType.empty() ? "<None>" : QString::fromStdString(b.BlockType));
            info += QString("In Circuit:  %1\n")
                .arg(b.InCircuit.empty() ? "<None>" : QString::fromStdString(b.InCircuit));
            info += QString("Mesh size:  %1\n")
                .arg(meshSize == 0 ? "<Auto>" : QString::number(meshSize));
            info += QString("In Group:  %1").arg(b.InGroup);
            if (b.MagDir != 0) info += QString("\nMag Direction:  %1 deg").arg(b.MagDir);
            b.ToggleSelect();
        }
    }
    else if (m_editAction == 3) {
        int i = m_doc->ClosestArcSegment(xd, yd);
        if (i >= 0) {
            auto &a = m_doc->arclist[i];
            CComplex c; double R;
            m_doc->GetCircle(a, c, R);
            info = QString("Angle spanned by closest arc:  %1 deg\n").arg(a.ArcLength);
            info += QString("Radius of associated circle:  %1\n").arg(R);
            info += QString("Center of associated circle:  (%1,%2)\n").arg(c.re).arg(c.im);
            info += QString("Boundary Property:  %1\n")
                .arg(a.BoundaryMarker.empty() ? "<None>" : QString::fromStdString(a.BoundaryMarker));
            info += QString("Grid spacing:  %1 deg\n").arg(a.MaxSideLength);
            info += QString("In Group:  %1").arg(a.InGroup);
            if (a.Hidden) info += "\nHidden in postprocessor";
            a.ToggleSelect();
        }
    }

    if (!info.isEmpty()) {
        update();
        // After the button-up, never here: mouse_release_gate.cpp.
        femmAfterMouseRelease(this, [this, info]() {
            QMessageBox::information(this, tr("Properties"), info);
        });
    }
}

void EditorWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_panning && event->button() == Qt::MiddleButton) {
        m_panning = false;
        setCursor(Qt::ArrowCursor);
        return;
    }

    if (event->button() != Qt::LeftButton) return;

    // Zoom window complete
    if (m_zoomWndFlag == 2) {
        m_zoomWndFlag = 0;
        double x0 = m_wndX0, y0 = m_wndY0;
        double x1 = m_mx, y1 = m_my;
        if (x0 > x1) std::swap(x0, x1);
        if (y0 > y1) std::swap(y0, y1);
        if (x1 - x0 < 1e-12 || y1 - y0 < 1e-12) { update(); return; }

        double magx = width() / (x1 - x0);
        double magy = height() / (y1 - y0);
        m_mag = std::min(magx, magy);
        m_ox = x0;
        m_oy = y0;
        update();
        return;
    }

    // Select window complete
    if (m_selectWndFlag == 2) {
        m_selectWndFlag = 0;
        double x0 = m_wndX0, y0 = m_wndY0;
        double x1 = m_mx, y1 = m_my;
        if (x0 > x1) std::swap(x0, x1);
        if (y0 > y1) std::swap(y0, y1);

        if (m_editAction == 0 || m_editAction == 4) {
            for (auto &n : m_doc->nodelist)
                if (n.x >= x0 && n.x <= x1 && n.y >= y0 && n.y <= y1)
                    n.IsSelected = true;
        }
        if (m_editAction == 2 || m_editAction == 4) {
            for (auto &b : m_doc->blocklist)
                if (b.x >= x0 && b.x <= x1 && b.y >= y0 && b.y <= y1)
                    b.IsSelected = true;
        }
        if (m_editAction == 1 || m_editAction == 4) {
            for (auto &s : m_doc->linelist) {
                auto &n0 = m_doc->nodelist[s.n0];
                auto &n1 = m_doc->nodelist[s.n1];
                if (n0.x >= x0 && n0.x <= x1 && n0.y >= y0 && n0.y <= y1 &&
                    n1.x >= x0 && n1.x <= x1 && n1.y >= y0 && n1.y <= y1)
                    s.IsSelected = true;
            }
        }
        if (m_editAction == 3 || m_editAction == 4) {
            for (auto &a : m_doc->arclist) {
                auto &n0 = m_doc->nodelist[a.n0];
                auto &n1 = m_doc->nodelist[a.n1];
                if (n0.x >= x0 && n0.x <= x1 && n0.y >= y0 && n0.y <= y1 &&
                    n1.x >= x0 && n1.x <= x1 && n1.y >= y0 && n1.y <= y1)
                    a.IsSelected = true;
            }
        }
        update();
        return;
    }

    // Select circle complete
    if (m_selectCircFlag == 2) {
        m_selectCircFlag = 0;
        double cx = m_wndX0, cy = m_wndY0;
        double dx = m_mx - cx, dy = m_my - cy;
        double R = std::sqrt(dx * dx + dy * dy);

        auto inCirc = [&](double x, double y) {
            double ex = x - cx, ey = y - cy;
            return std::sqrt(ex * ex + ey * ey) <= R;
        };

        if (m_editAction == 0 || m_editAction == 4)
            for (auto &n : m_doc->nodelist)
                if (inCirc(n.x, n.y)) n.IsSelected = true;

        if (m_editAction == 2 || m_editAction == 4)
            for (auto &b : m_doc->blocklist)
                if (inCirc(b.x, b.y)) b.IsSelected = true;

        if (m_editAction == 1 || m_editAction == 4)
            for (auto &s : m_doc->linelist) {
                auto &n0 = m_doc->nodelist[s.n0];
                auto &n1 = m_doc->nodelist[s.n1];
                if (inCirc(n0.x, n0.y) && inCirc(n1.x, n1.y))
                    s.IsSelected = true;
            }

        if (m_editAction == 3 || m_editAction == 4)
            for (auto &a : m_doc->arclist) {
                auto &n0 = m_doc->nodelist[a.n0];
                auto &n1 = m_doc->nodelist[a.n1];
                if (inCirc(n0.x, n0.y) && inCirc(n1.x, n1.y))
                    a.IsSelected = true;
            }

        update();
        return;
    }
}

void EditorWidget::wheelEvent(QWheelEvent *event)
{
    // Ignore tiny scroll deltas (spurious trackpad events during two-finger tap)
    if (std::abs(event->angleDelta().y()) < 15) return;

    double xd, yd;
    QPoint pos = event->position().toPoint();
    toDrawing(pos.x(), pos.y(), xd, yd);

    double factor = (event->angleDelta().y() > 0) ? 1.25 : (1.0 / 1.25);
    m_mag *= factor;
    if (m_mag < 0.01) m_mag = 0.01;

    m_ox = xd - pos.x() / m_mag;
    m_oy = yd - (height() - 1 - pos.y()) / m_mag;
    update();
}

// ================================================================
// Keyboard — matching original FEMM
// ================================================================

void EditorWidget::keyPressEvent(QKeyEvent *event)
{
    switch (event->key()) {
    case Qt::Key_Space:
        openSelected();
        break;

    case Qt::Key_Backspace:
        // Same as Delete. A Mac laptop keyboard has no forward-delete (the key
        // labelled "delete" IS Backspace), so Edit|Delete Selected's shortcut --
        // QKeySequence::Delete, which macOS resolves to the forward-delete key --
        // cannot be typed there at all.
        //
        // Deliberately handled here rather than as a second shortcut on that
        // action: shortcuts are resolved before the focused widget sees the key,
        // and the Python console dock lives in this same window, where Backspace
        // edits the input line (py_console.cpp:341). A window-level Backspace
        // shortcut would eat it. As a key event it only fires when the drawing
        // has focus, which is the same condition under which Escape works.
        deleteSelected();
        break;

    case Qt::Key_Escape:
        m_firstPoint = -1;
        m_zoomWndFlag = 0;
        m_selectWndFlag = 0;
        m_selectCircFlag = 0;
        if (m_doc) m_doc->UnselectAll();
        update();
        break;

    case Qt::Key_Left:  panLeft(); break;
    case Qt::Key_Right: panRight(); break;
    case Qt::Key_Up:    panUp(); break;
    case Qt::Key_Down:  panDown(); break;
    case Qt::Key_PageUp:   zoomIn(); break;
    case Qt::Key_PageDown: zoomOut(); break;
    case Qt::Key_Home:     zoomFit(); break;

    case Qt::Key_Tab:
        if (m_doc && (m_editAction == 0 || m_editAction == 2)) {
            // Enter point by coordinates. The pair is labelled and interpreted
            // per the document's display mode: (x,y), (r,z) axisymmetric, or
            // radius/degrees in polar.
            const int coords = m_doc->Coords;
            double c1, c2;
            QString l1, l2;
            coorddisp::toDisplay(coords, m_mx, m_my, c1, c2);
            coorddisp::entryLabels(coords, m_doc->ProblemType, l1, l2);
            QDialog dlg(this);
            dlg.setWindowTitle(m_editAction == 0 ? tr("Enter Node Point") : tr("Enter Block Label Point"));
            auto *form = new QFormLayout;
            auto *xEdit = new PyLineEdit;
            auto *yEdit = new PyLineEdit;
            xEdit->setText(QString::number(c1, 'g', 10));
            yEdit->setText(QString::number(c2, 'g', 10));
            xEdit->selectAll();
            form->addRow(l1, xEdit);
            form->addRow(l2, yEdit);
            auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
            QObject::connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
            QObject::connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
            auto *layout = new QVBoxLayout(&dlg);
            layout->addLayout(form);
            layout->addWidget(buttons);
            xEdit->setFocus();
            if (dlg.exec() == QDialog::Accepted) {
                double x, y;
                coorddisp::fromDisplay(coords, xEdit->evaluateAsDouble(c1),
                                       yEdit->evaluateAsDouble(c2), x, y);
                m_doc->pushUndo();
                if (m_editAction == 0)
                    m_doc->AddNode(x, y, 1.0 / m_mag);
                else
                    m_doc->AddBlockLabel(x, y, 1.0 / m_mag);
                update();
            }
        }
        break;

    case Qt::Key_F3:
        // Halve all mesh sizes (refine)
        if (m_doc) {
            for (auto &b : m_doc->blocklist)
                if (b.MaxArea > 0) b.MaxArea /= 4.0;
            update();
            emit statusMessage(tr("Mesh sizes halved"));
        }
        break;

    case Qt::Key_F4:
        // Double all mesh sizes (coarsen)
        if (m_doc) {
            for (auto &b : m_doc->blocklist)
                b.MaxArea *= 4.0;
            update();
            emit statusMessage(tr("Mesh sizes doubled"));
        }
        break;

    default:
        QWidget::keyPressEvent(event);
    }
}

void EditorWidget::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
}

// ================================================================
// ================================================================
// Mesh display
// ================================================================

void EditorWidget::loadMeshFromData(int numNodes, const double *nx, const double *ny,
                                     int numTris, const int *triNodes, const int *triRegion)
{
    m_meshNodes.clear();
    m_meshLines.clear();
    m_greyMeshLines.clear();

    m_meshNodes.resize(numNodes);
    for (int i = 0; i < numNodes; i++)
        m_meshNodes[i] = { nx[i], ny[i] };

    for (int i = 0; i < numTris; i++) {
        int n[3] = { triNodes[i*3], triNodes[i*3+1], triNodes[i*3+2] };
        int region = triRegion[i];
        for (int q = 0; q < 3; q++) {
            int p = (q + 1) % 3;
            if (n[p] > n[q]) {
                MeshEdge edge = { n[p], n[q] };
                if (region != 0)
                    m_meshLines.push_back(edge);
                else
                    m_greyMeshLines.push_back(edge);
            }
        }
    }
    m_showMesh = true;
    if (m_doc) {
        m_meshGeomNodes = m_doc->nodelist.size();
        m_meshGeomLines = m_doc->linelist.size();
        m_meshGeomArcs  = m_doc->arclist.size();
    }
    update();
}

// Stop displaying the mesh once the geometry it was built against changes.
// Checked at paint time so every mutation path is covered -- mouse, menu, undo
// and script alike -- rather than sprinkling calls through a dozen handlers.
//
// The counts are re-baselined here, which makes this a ONE-SHOT hide rather
// than a standing veto: the user can turn the mesh back on from Mesh > Show
// Mesh and it stays on until the next add or delete. Mesh data is kept either
// way; Purge Mesh is still the thing that discards it.
void EditorWidget::hideMeshIfGeometryChanged()
{
    if (!m_doc) return;
    size_t nn = m_doc->nodelist.size();
    size_t nl = m_doc->linelist.size();
    size_t na = m_doc->arclist.size();
    if (nn == m_meshGeomNodes && nl == m_meshGeomLines && na == m_meshGeomArcs)
        return;
    m_meshGeomNodes = nn;
    m_meshGeomLines = nl;
    m_meshGeomArcs  = na;
    m_showMesh = false;
}

void EditorWidget::clearMesh()
{
    m_meshNodes.clear();
    m_meshLines.clear();
    m_greyMeshLines.clear();
    m_showMesh = false;
    update();
}

void EditorWidget::drawMesh(QPainter &p)
{
    if (m_meshNodes.empty()) return;

    // Draw defined regions in mesh color (yellow-green)
    p.setPen(QPen(viewColors().mesh(), 0));
    for (auto &e : m_meshLines) {
        if (e.n0 < 0 || e.n0 >= (int)m_meshNodes.size()) continue;
        if (e.n1 < 0 || e.n1 >= (int)m_meshNodes.size()) continue;
        p.drawLine(toScreen(m_meshNodes[e.n0].x, m_meshNodes[e.n0].y),
                   toScreen(m_meshNodes[e.n1].x, m_meshNodes[e.n1].y));
    }

    // Draw undefined regions in grey
    p.setPen(QPen(QColor(220, 220, 220), 0));
    for (auto &e : m_greyMeshLines) {
        if (e.n0 < 0 || e.n0 >= (int)m_meshNodes.size()) continue;
        if (e.n1 < 0 || e.n1 >= (int)m_meshNodes.size()) continue;
        p.drawLine(toScreen(m_meshNodes[e.n0].x, m_meshNodes[e.n0].y),
                   toScreen(m_meshNodes[e.n1].x, m_meshNodes[e.n1].y));
    }
}

// ================================================================
// Open properties for selected entities (Space bar)
// ================================================================

void EditorWidget::openSelected()
{
    if (!m_doc) return;

    if (m_editAction == 0) {
        // Node properties
        int k = -1, nsel = 0;
        int nBdry = 0, nCond = 0;
        for (int i = 0; i < (int)m_doc->nodelist.size(); i++) {
            if (m_doc->nodelist[i].IsSelected) {
                if (k >= 0) {
                    if (m_doc->nodelist[i].BoundaryMarker != m_doc->nodelist[k].BoundaryMarker) nBdry++;
                    if (m_doc->nodelist[i].InConductor != m_doc->nodelist[k].InConductor) nCond++;
                    if (m_doc->nodelist[i].InGroup != m_doc->nodelist[k].InGroup)
                        m_doc->nodelist[k].InGroup = 0;
                }
                k = i; nsel++;
            }
        }
        if (nsel == 0) return;

        NodePropDialog dlg(m_doc, this);
        dlg.setGroup(m_doc->nodelist[k].InGroup);
        dlg.setLfs(m_doc->nodelist[k].lfs);
        if (nBdry == 0) {
            int idx = 0;
            for (int i = 0; i < m_doc->numPointProps(); i++)
                if (m_doc->pointPropName(i) == m_doc->nodelist[k].BoundaryMarker)
                    idx = i + 1;
            dlg.setSelectedProp(idx);
        }
        if (nCond == 0) {
            int cidx = 0;
            for (int i = 0; i < m_doc->numCircuits(); i++)
                if (m_doc->circuitName(i) == m_doc->nodelist[k].InConductor)
                    cidx = i + 1;
            dlg.setSelectedConductor(cidx);
        }
        if (dlg.exec() == QDialog::Accepted) {
            for (auto &n : m_doc->nodelist) {
                if (n.IsSelected) {
                    if (dlg.selectedProp() == 0) n.BoundaryMarker = "<None>";
                    else n.BoundaryMarker = m_doc->pointPropName(dlg.selectedProp() - 1);
                    n.InGroup = dlg.group();
                    n.lfs = dlg.lfs();
                    if (m_doc->physicsType() != 0) {
                        if (dlg.selectedConductor() == 0) n.InConductor = "<None>";
                        else n.InConductor = m_doc->circuitName(dlg.selectedConductor() - 1);
                    }
                }
            }
        }
    }
    else if (m_editAction == 1) {
        // Segment properties
        int k = -1, nsel = 0;
        int nBdry = 0, nCond = 0;
        double ms = 0;
        bool anyHidden = false;
        for (int i = 0; i < (int)m_doc->linelist.size(); i++) {
            if (m_doc->linelist[i].IsSelected) {
                if (m_doc->linelist[i].MaxSideLength < 0) ms = -1;
                if (ms >= 0) ms += m_doc->linelist[i].MaxSideLength;
                if (m_doc->linelist[i].Hidden) anyHidden = true;
                if (k >= 0) {
                    if (m_doc->linelist[i].BoundaryMarker != m_doc->linelist[k].BoundaryMarker) nBdry++;
                    if (m_doc->linelist[i].InConductor != m_doc->linelist[k].InConductor) nCond++;
                    if (m_doc->linelist[i].InGroup != m_doc->linelist[k].InGroup)
                        m_doc->linelist[k].InGroup = 0;
                }
                k = i; nsel++;
            }
        }
        if (nsel == 0) return;
        if (ms > 0) ms /= nsel;

        SegmentPropDialog dlg(m_doc, this);
        dlg.setGroup(m_doc->linelist[k].InGroup);
        dlg.setHidden(anyHidden);
        dlg.setAutoMesh(ms < 0);
        if (ms >= 0) dlg.setMeshSize(ms);
        if (nBdry == 0) {
            int idx = 0;
            for (int i = 0; i < m_doc->numBoundaries(); i++)
                if (m_doc->boundaryName(i) == m_doc->linelist[k].BoundaryMarker)
                    idx = i + 1;
            dlg.setSelectedProp(idx);
        }
        if (nCond == 0) {
            int cidx = 0;
            for (int i = 0; i < m_doc->numCircuits(); i++)
                if (m_doc->circuitName(i) == m_doc->linelist[k].InConductor)
                    cidx = i + 1;
            dlg.setSelectedConductor(cidx);
        }
        if (dlg.exec() == QDialog::Accepted) {
            for (auto &s : m_doc->linelist) {
                if (s.IsSelected) {
                    if (dlg.autoMesh()) s.MaxSideLength = -1;
                    else if (dlg.meshSize() > 0) s.MaxSideLength = dlg.meshSize();
                    else s.MaxSideLength = -1;
                    if (dlg.selectedProp() == 0) s.BoundaryMarker = "";
                    else s.BoundaryMarker = m_doc->boundaryName(dlg.selectedProp() - 1);
                    s.Hidden = dlg.hidden();
                    s.InGroup = dlg.group();
                    if (m_doc->physicsType() != 0) {
                        if (dlg.selectedConductor() == 0) s.InConductor = "<None>";
                        else s.InConductor = m_doc->circuitName(dlg.selectedConductor() - 1);
                    }
                }
            }
        }
    }
    else if (m_editAction == 2) {
        // Block label properties
        int k = -1, nsel = 0;
        int nMat = 0, nCirc = 0;
        double maxArea = 0;
        for (int i = 0; i < (int)m_doc->blocklist.size(); i++) {
            if (m_doc->blocklist[i].IsSelected) {
                if (m_doc->blocklist[i].MaxArea > maxArea)
                    maxArea = m_doc->blocklist[i].MaxArea;
                if (k >= 0) {
                    if (m_doc->blocklist[i].BlockType != m_doc->blocklist[k].BlockType) nMat++;
                    if (m_doc->blocklist[i].InCircuit != m_doc->blocklist[k].InCircuit) nCirc++;
                    if (m_doc->blocklist[i].InGroup != m_doc->blocklist[k].InGroup)
                        m_doc->blocklist[k].InGroup = 0;
                    if (m_doc->blocklist[i].IsExternal) m_doc->blocklist[k].IsExternal = true;
                    if (m_doc->blocklist[i].IsDefault) m_doc->blocklist[k].IsDefault = true;
                }
                k = i; nsel++;
            }
        }
        if (nsel == 0) return;

        BlockPropDialog dlg(m_doc, this);
        dlg.setProblemType(m_doc->ProblemType);
        dlg.setGroup(m_doc->blocklist[k].InGroup);
        dlg.setExternal(m_doc->blocklist[k].IsExternal);
        dlg.setDefault(m_doc->blocklist[k].IsDefault);

        // Magnetics-specific fields (from last selected, per OF)
        if (m_doc->physicsType() == 0) {
            dlg.setMagDir(m_doc->blocklist[k].MagDir);
            dlg.setMagDirFctn(QString::fromStdString(m_doc->blocklist[k].MagDirFctn));
            dlg.setTurns(m_doc->blocklist[k].Turns);
        }

        double sideLen = floor(2e7 * sqrt(maxArea / PI) + 0.5) / 1e7;
        dlg.setAutoMesh(sideLen == 0);
        if (sideLen > 0) dlg.setMeshSize(sideLen);

        // Set material combo — show common value or <None> if different
        if (nMat == 0) {
            if (m_doc->blocklist[k].BlockType == "<No Mesh>") dlg.setSelectedMaterial(1);
            else {
                int idx = 0;
                for (int i = 0; i < m_doc->numMaterials(); i++)
                    if (m_doc->materialName(i) == m_doc->blocklist[k].BlockType)
                        idx = i + 2;
                dlg.setSelectedMaterial(idx);
            }
        }
        // Set circuit combo
        if (nCirc == 0 && m_doc->physicsType() == 0) {
            int idx = 0;
            for (int i = 0; i < m_doc->numCircuits(); i++)
                if (m_doc->circuitName(i) == m_doc->blocklist[k].InCircuit)
                    idx = i + 1;
            dlg.setSelectedCircuit(idx);
        }

        if (dlg.exec() == QDialog::Accepted) {
            for (auto &b : m_doc->blocklist) {
                if (b.IsSelected) {
                    double ms = dlg.meshSize();
                    b.MaxArea = PI * ms * ms / 4.0;
                    if (dlg.autoMesh()) b.MaxArea = 0;
                    if (m_doc->physicsType() == 0) {
                        b.MagDir = dlg.magDir();
                        b.MagDirFctn = dlg.magDirFctn().toStdString();
                        b.Turns = dlg.turns();
                    }
                    int matIdx = dlg.selectedMaterial();
                    if (matIdx == 0) b.BlockType = "<None>";
                    else if (matIdx == 1) b.BlockType = "<No Mesh>";
                    else b.BlockType = m_doc->materialName(matIdx - 2);
                    if (m_doc->physicsType() == 0) {
                        int circIdx = dlg.selectedCircuit();
                        if (circIdx == 0) b.InCircuit = "<None>";
                        else b.InCircuit = m_doc->circuitName(circIdx - 1);
                    }
                    b.InGroup = dlg.group();
                    b.IsExternal = dlg.isExternal();
                    if (nsel == 1) {
                        if (dlg.isDefault()) {
                            b.IsDefault = true;
                            for (auto &other : m_doc->blocklist)
                                if (&other != &b) other.IsDefault = false;
                        }
                        else b.IsDefault = false;
                    }
                }
            }
        }
    }
    else if (m_editAction == 3) {
        // Arc segment properties
        int k = -1, nsel = 0;
        int nBdry = 0, nCond = 0;
        double ms = 0;
        bool anyHidden = false;
        for (int i = 0; i < (int)m_doc->arclist.size(); i++) {
            if (m_doc->arclist[i].IsSelected) {
                ms += m_doc->arclist[i].MaxSideLength;
                if (m_doc->arclist[i].Hidden) anyHidden = true;
                if (k >= 0) {
                    if (m_doc->arclist[i].BoundaryMarker != m_doc->arclist[k].BoundaryMarker) nBdry++;
                    if (m_doc->arclist[i].InConductor != m_doc->arclist[k].InConductor) nCond++;
                    if (m_doc->arclist[i].InGroup != m_doc->arclist[k].InGroup)
                        m_doc->arclist[k].InGroup = 0;
                }
                k = i; nsel++;
            }
        }
        if (nsel == 0) return;
        ms /= nsel;

        ArcPropDialog dlg(m_doc, this);
        dlg.setGroup(m_doc->arclist[k].InGroup);
        dlg.setHidden(anyHidden);
        dlg.setMaxSegAngle(ms);
        if (nBdry == 0) {
            int idx = 0;
            for (int i = 0; i < m_doc->numBoundaries(); i++)
                if (m_doc->boundaryName(i) == m_doc->arclist[k].BoundaryMarker)
                    idx = i + 1;
            dlg.setSelectedProp(idx);
        }
        if (nCond == 0) {
            int cidx = 0;
            for (int i = 0; i < m_doc->numCircuits(); i++)
                if (m_doc->circuitName(i) == m_doc->arclist[k].InConductor)
                    cidx = i + 1;
            dlg.setSelectedConductor(cidx);
        }
        if (dlg.exec() == QDialog::Accepted) {
            for (auto &a : m_doc->arclist) {
                if (a.IsSelected) {
                    if (dlg.selectedProp() == 0) a.BoundaryMarker = "";
                    else a.BoundaryMarker = m_doc->boundaryName(dlg.selectedProp() - 1);
                    a.MaxSideLength = dlg.maxSegAngle();
                    a.Hidden = dlg.hidden();
                    a.InGroup = dlg.group();
                    if (m_doc->physicsType() != 0) {
                        if (dlg.selectedConductor() == 0) a.InConductor = "<None>";
                        else a.InConductor = m_doc->circuitName(dlg.selectedConductor() - 1);
                    }
                }
            }
        }
    }
    else if (m_editAction == 4) {
        // Group dialog
        GroupDialog dlg(this);
        // Find common group among all selected
        int grp = 0;
        bool first = true;
        for (auto &n : m_doc->nodelist)
            if (n.IsSelected) { if (first) { grp = n.InGroup; first = false; } else if (n.InGroup != grp) grp = 0; }
        for (auto &s : m_doc->linelist)
            if (s.IsSelected) { if (first) { grp = s.InGroup; first = false; } else if (s.InGroup != grp) grp = 0; }
        for (auto &a : m_doc->arclist)
            if (a.IsSelected) { if (first) { grp = a.InGroup; first = false; } else if (a.InGroup != grp) grp = 0; }
        for (auto &b : m_doc->blocklist)
            if (b.IsSelected) { if (first) { grp = b.InGroup; first = false; } else if (b.InGroup != grp) grp = 0; }
        dlg.setGroup(grp);

        if (dlg.exec() == QDialog::Accepted) {
            int g = dlg.group();
            for (auto &n : m_doc->nodelist) if (n.IsSelected) n.InGroup = g;
            for (auto &s : m_doc->linelist) if (s.IsSelected) s.InGroup = g;
            for (auto &a : m_doc->arclist) if (a.IsSelected) a.InGroup = g;
            for (auto &b : m_doc->blocklist) if (b.IsSelected) b.InGroup = g;
        }
    }

    m_doc->UnselectAll();
    update();
}
