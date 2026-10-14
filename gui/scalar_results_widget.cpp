// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors

// scalar_results_widget.cpp — Scalar potential postprocessor results display
// Renders density plots (|D|, |E|, V, energy density), equipotential contours,
// and D/E vector arrows for electrostatics (and later heat/current flow).
// Adapted from results_widget.cpp (magnetics) for real-valued scalar potentials.

#include "scalar_results_widget.h"
#include "coord_display.h"
#include "zoom_fit.h"
#include "complex_format.h"
#include "elec_results_doc.h"
#include "heat_results_doc.h"
#include "py_state.h"
#include "script_callbacks.h"
#include "curr_results_doc.h"
#include "postproc_select.h"
#include "view_colors.h"
#include "electrostatics_handler.h"
#include "heat_handler.h"
#include "curr_handler.h"

#include <QPainter>
#include <QMessageBox>
#include "mouse_release_gate.h"
#include <QMouseEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QInputDialog>
#include <QFormLayout>
#include <QVBoxLayout>
#include <QLineEdit>
#include "py_line_edit.h"
#include <QDialogButtonBox>
#include <cmath>
#include <algorithm>

#ifndef PI
#define PI 3.141592653589793238462643383279
#endif

// Colors from the global viewColors() singleton
QColor ScalarResultsWidget::densityColor(int level)
{
    return viewColors().densityColor(level);
}

// ================================================================
// Constructor
// ================================================================

void ScalarResultsWidget::clearAreaSelection()
{
    if (m_view.labellist) for (auto &lbl : *m_view.labellist) lbl.IsSelected = false;
    if (m_view.meshnode)  for (auto &mn : *m_view.meshnode) mn.IsSelected = false;
    if (m_view.nodelist)  for (auto &n : *m_view.nodelist) n.IsSelected = false;
    if (m_view.linelist)  for (auto &s : *m_view.linelist) s.IsSelected = false;
    if (m_view.arclist)   for (auto &a : *m_view.arclist) a.IsSelected = false;
    if (m_view.bHasMask)  *m_view.bHasMask = false;
}

void ScalarResultsWidget::setMode(int mode)
{
    // Clear contour when leaving contour mode
    if (m_mode == 1 && mode != 1 && m_view.contour)
        (*m_view.contour).clear();
    // Clear block/conductor selections when leaving area mode
    if (m_mode == 2 && mode != 2)
        clearAreaSelection();
    m_mode = mode;
    update();
}

ScalarResultsWidget::ScalarResultsWidget(ElecResultsDoc *doc, QWidget *parent)
    : QWidget(parent)
    , m_view(ScalarDocView::fromElec(doc))
    , m_elecDoc(doc)
    , m_physicsType(1)
    , m_handler(new ElectrostaticsHandler(this))
{
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setMinimumSize(200, 200);
}

ScalarResultsWidget::ScalarResultsWidget(HeatResultsDoc *doc, QWidget *parent)
    : QWidget(parent)
    , m_view(ScalarDocView::fromHeat(doc))
    , m_heatDoc(doc)
    , m_physicsType(2)
    , m_handler(new HeatFlowHandler(this, doc))
{
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setMinimumSize(200, 200);
}

ScalarResultsWidget::ScalarResultsWidget(CurrResultsDoc *doc, QWidget *parent)
    : QWidget(parent)
    , m_view(ScalarDocView::fromCurrentFlow(doc))
    , m_currDoc(doc)
    , m_physicsType(3)
    , m_handler(new CurrentFlowHandler(this, doc))
{
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setMinimumSize(200, 200);

    // Phase slider for AC vector plots — overlaid at bottom
    m_phaseSlider = new QSlider(Qt::Horizontal, this);
    m_phaseSlider->setRange(0, 360);
    m_phaseSlider->setValue(0);
    m_phaseSlider->setFixedHeight(20);
    m_phaseSlider->hide();
    m_phaseLabel = new QLabel("0\xc2\xb0", this);  // "0deg"
    m_phaseLabel->setFixedSize(40, 20);
    m_phaseLabel->setAlignment(Qt::AlignCenter);
    m_phaseLabel->hide();

    connect(m_phaseSlider, &QSlider::valueChanged, [this](int val) {
        m_vectorPhase = val;
        m_phaseLabel->setText(QString("%1\xc2\xb0").arg(val));
        update();
    });

    m_playBtn = new QPushButton("\xe2\x96\xb6", this);  // play symbol
    m_playBtn->setFixedSize(24, 20);
    m_playBtn->setToolTip(tr("Animate phase sweep"));
    m_playBtn->hide();

    m_playTimer = new QTimer(this);
    m_playTimer->setInterval(14);  // ~70 fps, 5s for full 360deg
    connect(m_playTimer, &QTimer::timeout, [this]() {
        int val = m_phaseSlider->value() + 1;
        if (val > 360) val = 0;
        m_phaseSlider->setValue(val);
    });
    connect(m_playBtn, &QPushButton::clicked, [this]() {
        if (m_playTimer->isActive()) {
            m_playTimer->stop();
            m_playBtn->setText("\xe2\x96\xb6");  // play
        } else {
            m_playTimer->start();
            m_playBtn->setText("\xe2\x96\xa0");  // stop
        }
    });
}

ScalarResultsWidget::~ScalarResultsWidget()
{
    // Remove from registry before deleting so *i_loadsolution knows
    // the doc has been deleted and won't double-free it.
    // Only one of the three pointers is non-null (determined by physicsType).
    if (m_elecDoc) doc_registry_remove(m_elecDoc);
    if (m_heatDoc) doc_registry_remove(m_heatDoc);
    if (m_currDoc) doc_registry_remove(m_currDoc);
    delete m_elecDoc;
    delete m_heatDoc;
    delete m_currDoc;
    m_elecDoc = nullptr;
    m_heatDoc = nullptr;
    m_currDoc = nullptr;
}

// ================================================================
// Coordinate transforms
// ================================================================

QPointF ScalarResultsWidget::toScreen(double xd, double yd) const
{
    double sx = m_mag * (xd - m_ox);
    double sy = (height() - 1) - m_mag * (yd - m_oy);
    return QPointF(sx, sy);
}

void ScalarResultsWidget::toDrawing(int xs, int ys, double &xd, double &yd) const
{
    xd = xs / m_mag + m_ox;
    yd = (height() - 1 - ys) / m_mag + m_oy;
}

void ScalarResultsWidget::snapToGrid(double &xd, double &yd) const
{
    if (m_snapToGrid && m_gridSize > 0) {
        xd = m_gridSize * floor(xd / m_gridSize + 0.5);
        yd = m_gridSize * floor(yd / m_gridSize + 0.5);
    }
}

// ================================================================
// Zoom / pan
// ================================================================

void ScalarResultsWidget::zoomIn()
{
    double cx = width() / 2.0 / m_mag + m_ox;
    double cy = height() / 2.0 / m_mag + m_oy;
    m_mag *= 1.5;
    m_ox = cx - width() / 2.0 / m_mag;
    m_oy = cy - height() / 2.0 / m_mag;
    update();
}

void ScalarResultsWidget::zoomOut()
{
    double cx = width() / 2.0 / m_mag + m_ox;
    double cy = height() / 2.0 / m_mag + m_oy;
    m_mag /= 1.5;
    if (m_mag < 0.01) m_mag = 0.01;
    m_ox = cx - width() / 2.0 / m_mag;
    m_oy = cy - height() / 2.0 / m_mag;
    update();
}

void ScalarResultsWidget::zoomFit()
{
    if (!m_view.meshnode || (*m_view.meshnode).empty()) return;

    double xmin = (*m_view.meshnode)[0].x, xmax = xmin;
    double ymin = (*m_view.meshnode)[0].y, ymax = ymin;
    for (auto &nd : (*m_view.meshnode)) {
        if (nd.x < xmin) xmin = nd.x;
        if (nd.x > xmax) xmax = nd.x;
        if (nd.y < ymin) ymin = nd.y;
        if (nd.y > ymax) ymax = nd.y;
    }

    double dx = xmax - xmin, dy = ymax - ymin;
    if (dx < 1e-12) dx = 1;
    if (dy < 1e-12) dy = 1;
    double margin = kZoomFitMargin;   // shared; see zoom_fit.h
    xmin -= dx * margin; xmax += dx * margin;
    ymin -= dy * margin; ymax += dy * margin;
    dx = xmax - xmin; dy = ymax - ymin;

    double mx = (width() - 1.0) / dx;
    double my = (height() - 1.0) / dy;
    m_mag = std::min(mx, my);
    m_ox = xmin;
    m_oy = ymin;
    update();
}

void ScalarResultsWidget::zoomTo(double x0, double y0, double x1, double y1)
{
    if (x0 > x1) std::swap(x0, x1);
    if (y0 > y1) std::swap(y0, y1);
    double dx = x1 - x0, dy = y1 - y0;
    if (dx < 1e-12 || dy < 1e-12) return;
    m_mag = std::min(width() / dx, height() / dy);
    m_ox = x0;
    m_oy = y0;
    update();
}

void ScalarResultsWidget::zoomWindow()
{
    m_zoomWndFlag = true;
    emit statusMessage("Zoom: click and drag to define window");
}

void ScalarResultsWidget::panLeft()  { m_ox -= 0.25 * width() / m_mag; update(); }
void ScalarResultsWidget::panRight() { m_ox += 0.25 * width() / m_mag; update(); }
void ScalarResultsWidget::panUp()    { m_oy += 0.25 * height() / m_mag; update(); }
void ScalarResultsWidget::panDown()  { m_oy -= 0.25 * height() / m_mag; update(); }

// ================================================================
// Paint
// ================================================================

void ScalarResultsWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), viewColors().background());

    if (!m_view.meshnode || (*m_view.meshelem).empty()) return;

    p.setRenderHint(QPainter::Antialiasing, false);  // speed for triangles

    if (m_showVectors)
        ; // vector plot drawn after contours (needs antialiasing)
    else if (m_showDensity)
        drawDensityPlot(p);
    if (m_showMesh) drawMesh(p);

    p.setRenderHint(QPainter::Antialiasing, true);
    if (m_showContours) drawContours(p);
    if (m_showVectors) drawVectorPlot(p);

    // Selected blocks highlight
    drawSelectedBlocks(p);

    // Geometry overlay (segments, arcs, nodes) drawn on top
    drawGeometry(p);

    // Block names
    if (m_showNames) drawBlockNames(p);

    // Contour path overlay
    drawContourPath(p);

    // Grid overlay
    if (m_showGrid && m_gridSize > 0) {
        double pixelSpacing = m_gridSize * m_mag;
        if (pixelSpacing >= 8) {
            double x0d, y0d, x1d, y1d;
            toDrawing(0, height() - 1, x0d, y0d);
            toDrawing(width() - 1, 0, x1d, y1d);
            double gx = m_gridSize * floor(x0d / m_gridSize);
            double gy = m_gridSize * floor(y0d / m_gridSize);
            int nx = (int)((x1d - gx) / m_gridSize) + 1;
            int ny = (int)((y1d - gy) / m_gridSize) + 1;
            if ((long)nx * ny <= 250000) {
                p.setPen(QPen(QColor(180, 180, 180), 2, Qt::SolidLine, Qt::RoundCap));
                for (double x = gx; x <= x1d; x += m_gridSize)
                    for (double y = gy; y <= y1d; y += m_gridSize)
                        p.drawPoint(toScreen(x, y));
            }
        }
    }

    if (m_showDensity || m_showVectors) drawLegend(p);

    // Rubber band for zoom window
    if (m_zoomWndDragging) {
        QPoint curScreen = mapFromGlobal(QCursor::pos());
        QRect rb = QRect(m_zoomWndStart, curScreen).normalized();
        p.setPen(QPen(Qt::blue, 1, Qt::DashLine));
        p.setBrush(Qt::NoBrush);
        p.drawRect(rb);
    }

    updatePhaseSliderVisibility();
}

// ================================================================
// Density plot — fill each triangle with color based on field quantity
// Types: 1=|D|, 2=|E|, 3=V, 4=energy density
// ================================================================

void ScalarResultsWidget::drawDensityPlot(QPainter &p)
{
    int flag = m_densityPlotType;
    if (flag <= 0) return;

    // Map density plot type to PlotBounds index (physics-dependent)
    int boundsIdx;
    if (m_physicsType == 3) {
        // Current flow: 13 types (1-13)
        // 1=|V|->PB[0], 2=Re(V)->PB[1], 3=Im(V)->PB[2]
        // 4=|J|->PB[3], 5=Re(J)->PB[4], 6=Im(J)->PB[5]
        // 7=|E|->PB[6], 8=Re(E)->PB[7], 9=Im(E)->PB[8]
        // 10=|Jc|->PB[3], 11=Re(Jc)->PB[4], 12=Im(Jc)->PB[5], 13=|Jd|->PB[3]
        static const int currMap[] = {-1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 3, 4, 5, 3};
        if (flag < 1 || flag > 13) return;
        boundsIdx = currMap[flag];
    } else if (m_physicsType == 2) {
        // Heat: type 1=T->PB[0], 2=|F|->PB[1], 3=|G|->PB[2]
        static const int heatMap[] = {-1, 0, 1, 2};
        if (flag < 1 || flag > 3) return;
        boundsIdx = heatMap[flag];
    } else {
        // Electrostatics: type 1=|D|->PB[1], 2=|E|->PB[2], 3=V->PB[0], 4=energy->PB[3]
        static const int elecMap[] = {-1, 1, 2, 0, 3};
        if (flag < 1 || flag > 4) return;
        boundsIdx = elecMap[flag];
    }

    double bl = m_view.PlotBounds[boundsIdx][0];
    double bh = m_view.PlotBounds[boundsIdx][1];
    if (bh - bl < 1e-30) bh = bl + 1;

    p.setPen(Qt::NoPen);

    for (auto &el : (*m_view.meshelem)) {
        double bn[3];  // field value at each vertex

        for (int i = 0; i < 3; i++) {
            if (m_physicsType == 3) {
                // Current flow: complex quantities from cmeshnode/cmeshelem
                auto &cel = m_currDoc->cmeshelem[&el - &(*m_view.meshelem)[0]];
                switch (flag) {
                case 1: { // |V|
                    CComplex V = m_currDoc->cmeshnode[el.p[i]].V;
                    bn[i] = abs(V);
                    break;
                }
                case 2: { // Re(V)
                    bn[i] = m_currDoc->cmeshnode[el.p[i]].V.re;
                    break;
                }
                case 3: { // Im(V)
                    bn[i] = m_currDoc->cmeshnode[el.p[i]].V.im;
                    break;
                }
                case 4: { // |J|
                    CComplex jx = (*m_view.Smooth) ? cel.jx[i] : cel.Jx;
                    CComplex jy = (*m_view.Smooth) ? cel.jy[i] : cel.Jy;
                    bn[i] = sqrt(abs(jx)*abs(jx) + abs(jy)*abs(jy));
                    break;
                }
                case 5: { // Re(J)
                    CComplex jx = (*m_view.Smooth) ? cel.jx[i] : cel.Jx;
                    CComplex jy = (*m_view.Smooth) ? cel.jy[i] : cel.Jy;
                    bn[i] = sqrt(jx.re*jx.re + jy.re*jy.re);
                    break;
                }
                case 6: { // Im(J)
                    CComplex jx = (*m_view.Smooth) ? cel.jx[i] : cel.Jx;
                    CComplex jy = (*m_view.Smooth) ? cel.jy[i] : cel.Jy;
                    bn[i] = sqrt(jx.im*jx.im + jy.im*jy.im);
                    break;
                }
                case 7: { // |E| = |J/K|
                    CComplex jx = (*m_view.Smooth) ? cel.jx[i] : cel.Jx;
                    CComplex jy = (*m_view.Smooth) ? cel.jy[i] : cel.Jy;
                    CComplex Kx = m_currDoc->blockproplist[cel.blk].Kx;
                    CComplex Ky = m_currDoc->blockproplist[cel.blk].Ky;
                    CComplex Ex = jx / Kx;
                    CComplex Ey = jy / Ky;
                    bn[i] = sqrt(abs(Ex)*abs(Ex) + abs(Ey)*abs(Ey));
                    break;
                }
                case 8: { // Re(E)
                    CComplex jx = (*m_view.Smooth) ? cel.jx[i] : cel.Jx;
                    CComplex jy = (*m_view.Smooth) ? cel.jy[i] : cel.Jy;
                    CComplex Kx = m_currDoc->blockproplist[cel.blk].Kx;
                    CComplex Ky = m_currDoc->blockproplist[cel.blk].Ky;
                    CComplex Ex = jx / Kx;
                    CComplex Ey = jy / Ky;
                    bn[i] = sqrt(Ex.re*Ex.re + Ey.re*Ey.re);
                    break;
                }
                case 9: { // Im(E)
                    CComplex jx = (*m_view.Smooth) ? cel.jx[i] : cel.Jx;
                    CComplex jy = (*m_view.Smooth) ? cel.jy[i] : cel.Jy;
                    CComplex Kx = m_currDoc->blockproplist[cel.blk].Kx;
                    CComplex Ky = m_currDoc->blockproplist[cel.blk].Ky;
                    CComplex Ex = jx / Kx;
                    CComplex Ey = jy / Ky;
                    bn[i] = sqrt(Ex.im*Ex.im + Ey.im*Ey.im);
                    break;
                }
                case 10: { // |Jc| (conduction current = ox*E)
                    CComplex jx = (*m_view.Smooth) ? cel.jx[i] : cel.Jx;
                    CComplex jy = (*m_view.Smooth) ? cel.jy[i] : cel.Jy;
                    CComplex Kx = m_currDoc->blockproplist[cel.blk].Kx;
                    CComplex Ky = m_currDoc->blockproplist[cel.blk].Ky;
                    double ox = m_currDoc->blockproplist[cel.blk].ox;
                    double oy = m_currDoc->blockproplist[cel.blk].oy;
                    CComplex Jcx = ox * (jx / Kx);
                    CComplex Jcy = oy * (jy / Ky);
                    bn[i] = sqrt(abs(Jcx)*abs(Jcx) + abs(Jcy)*abs(Jcy));
                    break;
                }
                case 11: { // Re(Jc)
                    CComplex jx = (*m_view.Smooth) ? cel.jx[i] : cel.Jx;
                    CComplex jy = (*m_view.Smooth) ? cel.jy[i] : cel.Jy;
                    CComplex Kx = m_currDoc->blockproplist[cel.blk].Kx;
                    CComplex Ky = m_currDoc->blockproplist[cel.blk].Ky;
                    double ox = m_currDoc->blockproplist[cel.blk].ox;
                    double oy = m_currDoc->blockproplist[cel.blk].oy;
                    CComplex Jcx = ox * (jx / Kx);
                    CComplex Jcy = oy * (jy / Ky);
                    bn[i] = sqrt(Jcx.re*Jcx.re + Jcy.re*Jcy.re);
                    break;
                }
                case 12: { // Im(Jc)
                    CComplex jx = (*m_view.Smooth) ? cel.jx[i] : cel.Jx;
                    CComplex jy = (*m_view.Smooth) ? cel.jy[i] : cel.Jy;
                    CComplex Kx = m_currDoc->blockproplist[cel.blk].Kx;
                    CComplex Ky = m_currDoc->blockproplist[cel.blk].Ky;
                    double ox = m_currDoc->blockproplist[cel.blk].ox;
                    double oy = m_currDoc->blockproplist[cel.blk].oy;
                    CComplex Jcx = ox * (jx / Kx);
                    CComplex Jcy = oy * (jy / Ky);
                    bn[i] = sqrt(Jcx.im*Jcx.im + Jcy.im*Jcy.im);
                    break;
                }
                case 13: { // |Jd| (displacement current = j*w*e*E)
                    CComplex jx = (*m_view.Smooth) ? cel.jx[i] : cel.Jx;
                    CComplex jy = (*m_view.Smooth) ? cel.jy[i] : cel.Jy;
                    CComplex Kx = m_currDoc->blockproplist[cel.blk].Kx;
                    CComplex Ky = m_currDoc->blockproplist[cel.blk].Ky;
                    double ox = m_currDoc->blockproplist[cel.blk].ox;
                    double oy = m_currDoc->blockproplist[cel.blk].oy;
                    // Jd = J - Jc
                    CComplex Ex = jx / Kx;
                    CComplex Ey = jy / Ky;
                    CComplex Jdx = jx - ox * Ex;
                    CComplex Jdy = jy - oy * Ey;
                    bn[i] = sqrt(abs(Jdx)*abs(Jdx) + abs(Jdy)*abs(Jdy));
                    break;
                }
                }
            } else if (m_physicsType == 2) {
                // Heat flow: type 1=T, 2=|F|, 3=|G|
                switch (flag) {
                case 1: // T (temperature)
                    bn[i] = (*m_view.meshnode)[el.p[i]].V;
                    break;
                case 2: { // |F| (heat flux magnitude)
                    CComplex d = (*m_view.Smooth) ? el.d[i] : el.D;
                    bn[i] = abs(d);
                    break;
                }
                case 3: { // |G| (gradient magnitude = |F|/K)
                    CComplex d = (*m_view.Smooth) ? el.d[i] : el.D;
                    double kx = m_view.blockPropEx(el.blk);
                    double ky = m_view.blockPropEy(el.blk);
                    double Gx = d.re / kx;
                    double Gy = d.im / ky;
                    bn[i] = sqrt(Gx * Gx + Gy * Gy);
                    break;
                }
                }
            } else {
                // Electrostatics: type 1=|D|, 2=|E|, 3=V, 4=energy
                switch (flag) {
                case 1: { // |D|
                    CComplex d = (*m_view.Smooth) ? el.d[i] : el.D;
                    bn[i] = abs(d);
                    break;
                }
                case 2: { // |E|
                    CComplex d = (*m_view.Smooth) ? el.d[i] : el.D;
                    double ex = m_view.blockPropEx(el.blk);
                    double ey = m_view.blockPropEy(el.blk);
                    double Ex = d.re / (eo * ex);
                    double Ey = d.im / (eo * ey);
                    bn[i] = sqrt(Ex * Ex + Ey * Ey);
                    break;
                }
                case 3: // V (voltage)
                    bn[i] = (*m_view.meshnode)[el.p[i]].V;
                    break;
                case 4: { // energy density = D.E / 2
                    CComplex d = (*m_view.Smooth) ? el.d[i] : el.D;
                    double ex = m_view.blockPropEx(el.blk);
                    double ey = m_view.blockPropEy(el.blk);
                    double Ex = d.re / (eo * ex);
                    double Ey = d.im / (eo * ey);
                    bn[i] = 0.5 * (d.re * Ex + d.im * Ey);
                    break;
                }
                }
            }
        }

        // Scale to 0-20 range
        double sn[3];
        for (int i = 0; i < 3; i++)
            sn[i] = 20.0 * (bn[i] - bl) / (bh - bl);

        // Build subtriangle vertex list by inserting edge crossing points
        // Ported from OF PlotFluxDensity: walk each edge, add points where
        // integer color boundaries cross.
        struct SubVert { double sx, sy, sv; }; // screen x, y, scaled value
        SubVert c[64];
        int n = 0;

        for (int i = 0; i < 3 && n < 64; i++) {
            // Add vertex
            QPointF sp = toScreen((*m_view.meshnode)[el.p[i]].x, (*m_view.meshnode)[el.p[i]].y);
            c[n++] = {sp.x(), sp.y(), sn[i]};

            // Walk edge from vertex i to vertex j, insert crossing points
            int j = (i + 1) % 3;
            QPointF sp0 = toScreen((*m_view.meshnode)[el.p[i]].x, (*m_view.meshnode)[el.p[i]].y);
            QPointF sp1 = toScreen((*m_view.meshnode)[el.p[j]].x, (*m_view.meshnode)[el.p[j]].y);

            if (sn[i] < sn[j]) {
                for (double z = ceil(sn[i]); z < sn[j] && n < 64; z += 1.0) {
                    if (z >= 0 && z < 20) {
                        double t = (z - sn[i]) / (sn[j] - sn[i]);
                        c[n++] = {sp0.x() + t * (sp1.x() - sp0.x()),
                                  sp0.y() + t * (sp1.y() - sp0.y()), z};
                    }
                }
            } else {
                for (double z = floor(sn[i]); z > sn[j] && n < 64; z -= 1.0) {
                    if (z >= 0 && z < 20) {
                        double t = (z - sn[i]) / (sn[j] - sn[i]);
                        c[n++] = {sp0.x() + t * (sp1.x() - sp0.x()),
                                  sp0.y() + t * (sp1.y() - sp0.y()), z};
                    }
                }
            }
        }

        // Peel off subtriangles from the polygon by removing the lowest vertex
        while (n > 2) {
            // Find lowest vertex
            int jmin = 0;
            for (int i = 1; i < n; i++)
                if (c[i].sv < c[jmin].sv) jmin = i;

            int iprev = (jmin - 1 + n) % n;
            int inext = (jmin + 1) % n;

            // Color from average of three vertices
            double avg = (c[iprev].sv + c[jmin].sv + c[inext].sv) / 3.0;
            int colorIdx = 19 - (int)std::clamp(avg, 0.0, 19.99);
            if (colorIdx < 0) colorIdx = 0;
            if (colorIdx > 19) colorIdx = 19;

            QPointF tri[3] = {
                {c[iprev].sx, c[iprev].sy},
                {c[jmin].sx, c[jmin].sy},
                {c[inext].sx, c[inext].sy}
            };
            QColor col = m_greyScale ? viewColors().greyColor(colorIdx) : densityColor(colorIdx);
            p.setBrush(col);
            p.drawConvexPolygon(tri, 3);

            // Remove the lowest vertex
            for (int i = jmin; i < n - 1; i++)
                c[i] = c[i + 1];
            n--;
        }
    }
}

// ================================================================
// Contour lines — equipotential lines of V (voltage)
// ================================================================

void ScalarResultsWidget::drawContours(QPainter &p)
{
    double aLow = (*m_view.A_Low);
    double aHigh = (*m_view.A_High);
    if (aHigh - aLow < 1e-30) return;

    int nc = m_numContours;
    int edges[3][2] = {{0,1}, {1,2}, {2,0}};

    p.setPen(QPen(viewColors().realFluxLine(), 1));
    for (auto &el : (*m_view.meshelem)) {
        double a[3], x[3], y[3];
        for (int i = 0; i < 3; i++) {
            a[i] = (*m_view.meshnode)[el.p[i]].V;
            x[i] = (*m_view.meshnode)[el.p[i]].x;
            y[i] = (*m_view.meshnode)[el.p[i]].y;
        }
        double sa[3];
        for (int i = 0; i < 3; i++)
            sa[i] = nc * (a[i] - aLow) / (aHigh - aLow);

        for (int contour = 1; contour < nc; contour++) {
            double level = (double)contour;
            QPointF crossings[3];
            int nCross = 0;
            for (int e = 0; e < 3 && nCross < 2; e++) {
                int i0 = edges[e][0], i1 = edges[e][1];
                double v0 = sa[i0], v1 = sa[i1];
                if ((v0 <= level && level < v1) || (v1 <= level && level < v0)) {
                    double t = (level - v0) / (v1 - v0);
                    double cx = x[i0] + t * (x[i1] - x[i0]);
                    double cy = y[i0] + t * (y[i1] - y[i0]);
                    crossings[nCross++] = toScreen(cx, cy);
                }
            }
            if (nCross == 2)
                p.drawLine(crossings[0], crossings[1]);
        }
    }

    // Stress tensor mask contours (0-1 range, orange)
    if (m_showMask && (*m_view.bHasMask)) {
        int ncMask = 10;
        p.setPen(QPen(QColor(255, 140, 0), 1));
        for (auto &el : (*m_view.meshelem)) {
            double mv[3], x[3], y[3];
            for (int i = 0; i < 3; i++) {
                mv[i] = (*m_view.meshnode)[el.p[i]].msk * ncMask;
                x[i] = (*m_view.meshnode)[el.p[i]].x;
                y[i] = (*m_view.meshnode)[el.p[i]].y;
            }
            for (int contour = 1; contour < ncMask; contour++) {
                double level = (double)contour;
                QPointF crossings[3];
                int nCross = 0;
                for (int e = 0; e < 3 && nCross < 2; e++) {
                    int i0 = edges[e][0], i1 = edges[e][1];
                    double v0 = mv[i0], v1 = mv[i1];
                    if ((v0 <= level && level < v1) || (v1 <= level && level < v0)) {
                        double t = (level - v0) / (v1 - v0);
                        crossings[nCross++] = toScreen(
                            x[i0] + t * (x[i1] - x[i0]),
                            y[i0] + t * (y[i1] - y[i0]));
                    }
                }
                if (nCross == 2) p.drawLine(crossings[0], crossings[1]);
            }
        }
    }
}

// ================================================================
// Mesh wireframe
// ================================================================

void ScalarResultsWidget::drawMesh(QPainter &p)
{
    p.setPen(QPen(viewColors().mesh(), 0));
    for (auto &el : (*m_view.meshelem)) {
        QPointF pts[3];
        for (int i = 0; i < 3; i++)
            pts[i] = toScreen((*m_view.meshnode)[el.p[i]].x, (*m_view.meshnode)[el.p[i]].y);
        p.drawLine(pts[0], pts[1]);
        p.drawLine(pts[1], pts[2]);
        p.drawLine(pts[2], pts[0]);
    }
}

// ================================================================
// Geometry overlay — segments, arcs, nodes drawn on top of results
// ================================================================

void ScalarResultsWidget::drawBlockNames(QPainter &p)
{
    QFont font = p.font();
    font.setPointSize(8);
    p.setFont(font);
    QColor nameClr = viewColors().name();
    QColor blkClr = viewColors().blockLabel();

    for (auto &lbl : (*m_view.labellist)) {
        QPointF sp = toScreen(lbl.x, lbl.y);
        QColor c = lbl.IsSelected ? viewColors().selected() : blkClr;

        // Small square
        p.setPen(QPen(c, 1));
        p.setBrush(Qt::NoBrush);
        p.drawRect(QRectF(sp.x() - 2, sp.y() - 2, 4, 4));

        // Area constraint circle
        if (lbl.MaxArea > 0) {
            double side = sqrt(lbl.MaxArea / PI);
            int r = (int)ceil(side * m_mag);
            if (r > 2 && r < 1000) {
                p.setPen(QPen(c, 1, Qt::DotLine));
                p.drawEllipse(sp, r, r);
            }
        }

        // Block type name
        if (lbl.BlockType >= 0 && lbl.BlockType < m_view.blockPropCount()) {
            p.setPen(nameClr);
            p.drawText(QPointF(sp.x() + 5, sp.y() - 2),
                QString::fromStdString(m_view.blockPropName(lbl.BlockType)));
        }
    }
}

void ScalarResultsWidget::drawGeometry(QPainter &p)
{
    if (!m_view.meshnode) return;

    QColor kLineColor = viewColors().line();
    QColor kNodeColor = viewColors().node();

    // Draw segments (selected ones — e.g. a selected conductor — highlighted)
    QColor kSelColor = viewColors().selected();
    for (auto &seg : (*m_view.linelist)) {
        if (seg.Hidden) continue;
        if (seg.n0 < 0 || seg.n0 >= (int)(*m_view.nodelist).size()) continue;
        if (seg.n1 < 0 || seg.n1 >= (int)(*m_view.nodelist).size()) continue;
        QPointF p0 = toScreen((*m_view.nodelist)[seg.n0].x, (*m_view.nodelist)[seg.n0].y);
        QPointF p1 = toScreen((*m_view.nodelist)[seg.n1].x, (*m_view.nodelist)[seg.n1].y);
        p.setPen(seg.IsSelected ? QPen(kSelColor, 2) : QPen(kLineColor, 1));
        p.drawLine(p0, p1);
    }

    // Draw arc segments
    for (auto &arc : (*m_view.arclist)) {
        if (arc.Hidden) continue;
        if (arc.n0 < 0 || arc.n0 >= (int)(*m_view.nodelist).size()) continue;
        if (arc.n1 < 0 || arc.n1 >= (int)(*m_view.nodelist).size()) continue;

        CComplex center;
        double R;
        m_view.GetCircle(arc, center, R);

        CComplex a0((*m_view.nodelist)[arc.n0].x, (*m_view.nodelist)[arc.n0].y);

        double mySideLength = arc.mySideLength;
        if (mySideLength <= 0) mySideLength = arc.MaxSideLength;
        if (mySideLength <= 0) mySideLength = 5.0;
        int k = (int)ceil(arc.ArcLength / mySideLength);
        if (k < 1) k = 1;
        double dt = arc.ArcLength * PI / (180.0 * k);

        p.setPen(arc.IsSelected ? QPen(kSelColor, 2) : QPen(kLineColor, 1));
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

    // Draw nodes as small squares (gated by m_showPoints)
    if (m_showPoints) {
        p.setPen(QPen(kNodeColor, 1));
        p.setBrush(Qt::NoBrush);
        for (auto &nd : (*m_view.nodelist)) {
            QPointF sp = toScreen(nd.x, nd.y);
            p.drawRect(QRectF(sp.x() - 2, sp.y() - 2, 4, 4));
        }
    }
}

// ================================================================
// Color legend
// ================================================================

void ScalarResultsWidget::drawLegend(QPainter &p)
{
    if (!m_showLegend) return;

    int boundsIdx;
    const char *title = "";

    if (m_showVectors) {
        if (m_physicsType == 3) {
            // Current flow: vector 0=J, 1=E
            if (m_vectorType == 0) { boundsIdx = 3; title = "|J| [A/m^2]"; }
            else { boundsIdx = 6; title = "|E| [V/m]"; }
        } else if (m_physicsType == 2) {
            // Heat: vector 0=F, 1=G
            if (m_vectorType == 0) { boundsIdx = 1; title = "|F| [W/m^2]"; }
            else { boundsIdx = 2; title = "|G| [K/m]"; }
        } else {
            // Electrostatics: vector 0=D, 1=E
            if (m_vectorType == 0) { boundsIdx = 1; title = "|D| [C/m^2]"; }
            else { boundsIdx = 2; title = "|E| [V/m]"; }
        }
    } else {
        if (m_densityPlotType <= 0) return;
        if (m_physicsType == 3) {
            static const char *labels[] = { "",
                "|V| [V]", "Re(V) [V]", "Im(V) [V]",
                "|J| [A/m^2]", "Re(J) [A/m^2]", "Im(J) [A/m^2]",
                "|E| [V/m]", "Re(E) [V/m]", "Im(E) [V/m]",
                "|Jc| [A/m^2]", "Re(Jc) [A/m^2]", "Im(Jc) [A/m^2]", "|Jd| [A/m^2]" };
            static const int boundsMap[] = { -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 3, 4, 5, 3 };
            if (m_densityPlotType < 1 || m_densityPlotType > 13) return;
            boundsIdx = boundsMap[m_densityPlotType];
            title = labels[m_densityPlotType];
        } else if (m_physicsType == 2) {
            static const char *labels[] = { "", "T [K]", "|F| [W/m^2]", "|G| [K/m]" };
            static const int boundsMap[] = { -1, 0, 1, 2 };
            if (m_densityPlotType < 1 || m_densityPlotType > 3) return;
            boundsIdx = boundsMap[m_densityPlotType];
            title = labels[m_densityPlotType];
        } else {
            static const char *labels[] = { "",
                "|D| [C/m^2]", "|E| [V/m]", "V [V]", "Energy Density [J/m^3]" };
            static const int boundsMap[] = { -1, 1, 2, 0, 3 };
            if (m_densityPlotType < 1 || m_densityPlotType > 4) return;
            boundsIdx = boundsMap[m_densityPlotType];
            title = labels[m_densityPlotType];
        }
    }

    double bl = m_view.PlotBounds[boundsIdx][0];
    double bh = m_view.PlotBounds[boundsIdx][1];

    int lx = width() - 80;
    int ly = 20;
    int boxH = 12;
    int boxW = 20;

    QFont font = p.font();
    font.setPointSize(7);
    p.setFont(font);

    // drawRect() fills with the current brush as well as stroking the
    // border, so force NoBrush — otherwise stale brush state (e.g. the
    // last density-plot triangle color) overpaints the fillRect colors.
    p.setPen(Qt::black);
    p.setBrush(Qt::NoBrush);
    for (int i = 0; i < 20; i++) {
        int y = ly + i * boxH;
        QColor col = m_greyScale ? viewColors().greyColor(i) : densityColor(i);
        p.fillRect(lx, y, boxW, boxH, col);
        p.drawRect(lx, y, boxW, boxH);

        if (i % 5 == 0 || i == 19) {
            double val = bh - (bh - bl) * i / 19.0;
            p.drawText(lx + boxW + 4, y + boxH - 1,
                       QString::number(val, 'g', 3));
        }
    }

    p.setPen(Qt::black);
    p.drawText(lx, ly - 4, title);
}

// ================================================================
// Contour path overlay (line integral path)
// ================================================================

void ScalarResultsWidget::drawContourPath(QPainter &p)
{
    if (!m_view.meshnode || (*m_view.contour).size() < 1 || m_mode != 1) return;

    p.setPen(QPen(QColor(255, 0, 0), 2));  // red
    for (size_t i = 0; i < (*m_view.contour).size(); i++) {
        QPointF sp = toScreen((*m_view.contour)[i].re, (*m_view.contour)[i].im);
        // Draw a small cross at each contour point
        p.drawLine(QPointF(sp.x()-4, sp.y()), QPointF(sp.x()+4, sp.y()));
        p.drawLine(QPointF(sp.x(), sp.y()-4), QPointF(sp.x(), sp.y()+4));
        // Draw line to next point
        if (i > 0) {
            QPointF prev = toScreen((*m_view.contour)[i-1].re, (*m_view.contour)[i-1].im);
            p.drawLine(prev, sp);
        }
    }
}

// ================================================================
// Vector plot — D or E field arrows
// ================================================================

void ScalarResultsWidget::drawVectorPlot(QPainter &p)
{
    if (!m_view.meshnode || (*m_view.meshelem).empty()) return;

    // Auto grid density: arrows ~20 pixels apart
    const double arrowSpacingPix = 20.0;
    int Nx = std::max(2, (int)(width() / arrowSpacingPix));
    int Ny = std::max(2, (int)(height() / arrowSpacingPix));

    // Get visible drawing area
    double x0d, y0d, x1d, y1d;
    toDrawing(0, height() - 1, x0d, y0d);
    toDrawing(width() - 1, 0, x1d, y1d);

    double dx = (x1d - x0d) / Nx;
    double dy = (y1d - y0d) / Ny;
    double arrowLen = std::min(dx, dy) * 0.4;
    double arrowPixLen = arrowLen * m_mag;
    if (arrowPixLen < 3) return;

    // Get bounds for color mapping
    double bLow, bHigh;
    if (m_physicsType == 3) {
        // Current flow: vector 0=J (PB[3]), 1=E (PB[6])
        if (m_vectorType == 0) {
            bLow = m_view.PlotBounds[3][0]; bHigh = m_view.PlotBounds[3][1];
        } else {
            bLow = m_view.PlotBounds[6][0]; bHigh = m_view.PlotBounds[6][1];
        }
    } else {
        // Elec/heat: vector 0 uses PlotBounds[1], vector 1 uses PlotBounds[2]
        if (m_vectorType == 0) {
            bLow = m_view.PlotBounds[1][0]; bHigh = m_view.PlotBounds[1][1];
        } else {
            bLow = m_view.PlotBounds[2][0]; bHigh = m_view.PlotBounds[2][1];
        }
    }
    if (bHigh <= bLow) bHigh = bLow + 1;

    p.setRenderHint(QPainter::Antialiasing, true);

    for (int ix = 0; ix < Nx; ix++) {
        for (int iy = 0; iy < Ny; iy++) {
            double xd = x0d + (ix + 0.5) * dx;
            double yd = y0d + (iy + 0.5) * dy;

            int elm = m_view.InTriangle(xd, yd);
            if (elm < 0) continue;

            double fx, fy, mag;

            if (m_physicsType == 3 && m_currDoc) {
                // Current flow: get complex J or E and apply phase
                CPointVals cv;
                if (!m_currDoc->GetPointValues(xd, yd, elm, cv)) continue;

                double cosph = cos(m_vectorPhase * PI / 180.0);
                double sinph = sin(m_vectorPhase * PI / 180.0);
                if (m_vectorType == 0) {
                    // J field — instantaneous value at phase angle
                    fx = cv.Jx.re * cosph - cv.Jx.im * sinph;
                    fy = cv.Jy.re * cosph - cv.Jy.im * sinph;
                } else {
                    // E field
                    fx = cv.Ex.re * cosph - cv.Ex.im * sinph;
                    fy = cv.Ey.re * cosph - cv.Ey.im * sinph;
                }
            } else {
                EPointVals v;
                if (!m_view.GetPointValues(xd, yd, elm, v)) continue;

                if (m_vectorType == 0) {
                    // D field
                    fx = v.D.re;
                    fy = v.D.im;
                } else {
                    // E field
                    fx = v.E.re;
                    fy = v.E.im;
                }
            }
            mag = sqrt(fx * fx + fy * fy);
            if (mag < 1e-30) continue;

            // Normalize direction
            double ux = fx / mag, uy = fy / mag;

            // Color from magnitude (same 20-level map as density plot)
            double scaled = 19.0 * (mag - bLow) / (bHigh - bLow);
            int colorIdx = 19 - (int)std::clamp(scaled, 0.0, 19.0);
            if (colorIdx < 0) colorIdx = 0;
            if (colorIdx > 19) colorIdx = 19;
            QColor col = m_greyScale ? viewColors().greyColor(colorIdx) : densityColor(colorIdx);

            // Draw arrow in screen space
            QPointF center = toScreen(xd, yd);
            double halfLen = arrowPixLen * 0.5;

            // Arrow shaft: from tail to head
            QPointF tail(center.x() - ux * halfLen, center.y() + uy * halfLen);
            QPointF head(center.x() + ux * halfLen, center.y() - uy * halfLen);

            p.setPen(QPen(col, 1.5));
            p.drawLine(tail, head);

            // Arrowhead (two short lines from head)
            double headLen = halfLen * 0.3;
            double ax = -ux * headLen, ay = uy * headLen;  // reverse direction, screen coords
            // Rotate +/-30 degrees
            double cos30 = 0.866, sin30 = 0.5;
            QPointF h1(head.x() + ax * cos30 - ay * sin30,
                       head.y() + ay * cos30 + ax * sin30);
            QPointF h2(head.x() + ax * cos30 + ay * sin30,
                       head.y() + ay * cos30 - ax * sin30);
            p.drawLine(head, h1);
            p.drawLine(head, h2);
        }
    }
}

// ================================================================
// Selected blocks highlight
// ================================================================

void ScalarResultsWidget::drawSelectedBlocks(QPainter &p)
{
    if (!m_view.meshnode) return;

    p.setPen(Qt::NoPen);
    QColor sel(0, 255, 0, 140);  // semi-transparent green
    for (auto &elem : (*m_view.meshelem)) {
        if (elem.lbl < 0 || elem.lbl >= (int)(*m_view.labellist).size()) continue;
        if (!(*m_view.labellist)[elem.lbl].IsSelected) continue;

        QPointF pts[3];
        for (int j = 0; j < 3; j++) {
            auto &nd = (*m_view.meshnode)[elem.p[j]];
            pts[j] = toScreen(nd.x, nd.y);
        }
        p.setBrush(sel);
        p.drawPolygon(pts, 3);
    }
}

// ================================================================
// Mouse events
// ================================================================

void ScalarResultsWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::MiddleButton) {
        m_panning = true;
        m_panStart = event->pos();
        m_panOxStart = m_ox;
        m_panOyStart = m_oy;
        setCursor(Qt::ClosedHandCursor);
    }

    // Left click
    if (event->button() == Qt::LeftButton) {
        // Zoom window mode: start drag
        if (m_zoomWndFlag) {
            m_zoomWndStart = event->pos();
            m_zoomWndDragging = true;
            return;
        }

        if (!m_view.meshnode) return;
        double xd, yd;
        toDrawing(event->pos().x(), event->pos().y(), xd, yd);
        if (m_mode == 0) snapToGrid(xd, yd);

        if (m_mode == 0) {
            showPointProperties(xd, yd);
        }
        else if (m_mode == 1) {
            // Contour mode: left-click snaps to nearest input node
            // and adds intermediate points along connecting arcs
            if ((*m_view.nodelist).empty()) return;
            int nodeIdx = m_view.ClosestInputNode(xd, yd);
            if (nodeIdx < 0) return;
            CComplex z((*m_view.nodelist)[nodeIdx].x, (*m_view.nodelist)[nodeIdx].y);

            if ((*m_view.contour).empty()) {
                (*m_view.contour).push_back(z);
            } else {
                CComplex y = (*m_view.contour).back();
                if (y.re == z.re && y.im == z.im) return;  // same point

                int prevNode = m_view.ClosestInputNode(y.re, y.im);
                CComplex x((*m_view.nodelist)[prevNode].x, (*m_view.nodelist)[prevNode].y);

                // Check if previous and new node are ends of a segment or arc
                int arcno = -1;
                bool arcForward = true;
                double d1 = 1.e8;

                if (abs(x - y) < 1.e-08) {
                    // Check segments
                    for (int k = 0; k < (int)(*m_view.linelist).size(); k++) {
                        if (((*m_view.linelist)[k].n0 == prevNode && (*m_view.linelist)[k].n1 == nodeIdx) ||
                            ((*m_view.linelist)[k].n0 == nodeIdx && (*m_view.linelist)[k].n1 == prevNode)) {
                            double d2 = fabs(m_view.ShortestDistanceFromSegment(xd, yd, k));
                            if (d2 < d1) { arcno = -1; d1 = d2; }
                        }
                    }
                    // Check arcs
                    for (int k = 0; k < (int)(*m_view.arclist).size(); k++) {
                        if ((*m_view.arclist)[k].n0 == prevNode && (*m_view.arclist)[k].n1 == nodeIdx) {
                            double d2 = m_view.ShortestDistanceFromInputArc(CComplex(xd, yd), k);
                            if (d2 < d1) { arcno = k; arcForward = true; d1 = d2; }
                        }
                        if ((*m_view.arclist)[k].n0 == nodeIdx && (*m_view.arclist)[k].n1 == prevNode) {
                            double d2 = m_view.ShortestDistanceFromInputArc(CComplex(xd, yd), k);
                            if (d2 < d1) { arcno = k; arcForward = false; d1 = d2; }
                        }
                    }
                }

                if (arcno >= 0) {
                    // Add intermediate points along the arc
                    CComplex c; double R;
                    m_view.GetInputCircle(arcno, c, R);
                    int n = (int)ceil((*m_view.arclist)[arcno].ArcLength /
                                     (*m_view.arclist)[arcno].MaxSideLength);
                    if (n < 1) n = 1;
                    double dtta = (*m_view.arclist)[arcno].ArcLength * PI / (180.0 * n);
                    CComplex rot = exp(CComplex(0, arcForward ? dtta : -dtta));
                    for (int i = 0; i < n; i++) {
                        y = (y - c) * rot + c;
                        (*m_view.contour).push_back(y);
                    }
                } else {
                    // Straight line or unconnected — just add the node
                    (*m_view.contour).push_back(z);
                }
            }
            emit statusMessage(QString("Contour: %1 points").arg((*m_view.contour).size()));
            update();
        }
        else if (m_mode == 2) {
            // Area mode: toggle block selection
            int k = m_view.InTriangle(xd, yd);
            if (k >= 0) {
                int lbl = (*m_view.meshelem)[k].lbl;
                if (lbl >= 0 && lbl < (int)(*m_view.labellist).size()) {
                    (*m_view.labellist)[lbl].ToggleSelect();
                    (*m_view.bHasMask) = false;  // invalidate mask on selection change
                    update();
                }
            }
        }
    }

    // Right-click in contour mode: add arbitrary point
    if (event->button() == Qt::RightButton && m_mode == 1 && m_view.meshnode) {
        double xd, yd;
        toDrawing(event->pos().x(), event->pos().y(), xd, yd);
        snapToGrid(xd, yd);
        CComplex z(xd, yd);
        if (!(*m_view.contour).empty() && z == (*m_view.contour).back()) return;
        (*m_view.contour).push_back(z);
        emit statusMessage(QString("Contour: %1 points").arg((*m_view.contour).size()));
        update();
    }

    // Right-click in area mode: select the conductor nearest the click. This is
    // how a conductor is selected for a weighted-stress-tensor force/torque
    // integral in electrostatics/current flow (matches OF's block-mode
    // right-click).
    if (event->button() == Qt::RightButton && m_mode == 2 && m_view.meshnode) {
        double xd, yd;
        toDrawing(event->pos().x(), event->pos().y(), xd, yd);
        if (m_view.SelectConductorNear(xd, yd)) {
            if (m_view.bHasMask) *m_view.bHasMask = false;  // invalidate mask
            update();
        }
    }
}

void ScalarResultsWidget::mouseDoubleClickEvent(QMouseEvent *event)
{
    // Right double-click in point mode: report the closest input node, as the
    // preprocessor does for the closest node/segment/label.
    if (event->button() == Qt::RightButton && m_mode == 0 && m_view.nodelist) {
        double xd, yd;
        toDrawing(event->pos().x(), event->pos().y(), xd, yd);
        int i = ppClosestNode(xd, yd, *m_view.nodelist);
        if (i >= 0) {
            auto &n = (*m_view.nodelist)[i];
            const QString msg = QString("Closest input node:\n(%1,%2)").arg(n.x).arg(n.y);
            // After the button-up, never here: mouse_release_gate.cpp.
            femmAfterMouseRelease(this, [this, msg]() {
                QMessageBox::information(this, tr("Closest Input Node"), msg);
            });
        }
        return;
    }

    if (event->button() != Qt::LeftButton || m_mode != 2 || !m_view.meshelem) return;
    double xd, yd;
    toDrawing(event->pos().x(), event->pos().y(), xd, yd);
    int k = m_view.InTriangle(xd, yd);
    if (k < 0) return;
    int lbl = (*m_view.meshelem)[k].lbl;
    if (lbl < 0 || lbl >= (int)(*m_view.labellist).size()) return;
    int grp = (*m_view.labellist)[lbl].InGroup;
    for (auto &label : *m_view.labellist)
        if (label.InGroup == grp) label.IsSelected = true;
    (*m_view.bHasMask) = false;
    update();
}

void ScalarResultsWidget::mouseMoveEvent(QMouseEvent *event)
{
    double xd, yd;
    toDrawing(event->pos().x(), event->pos().y(), xd, yd);
    snapToGrid(xd, yd);
    m_mx = xd; m_my = yd;
    emit coordinatesChanged(xd, yd);

    if (m_panning) {
        double dx = (event->pos().x() - m_panStart.x()) / m_mag;
        double dy = -(event->pos().y() - m_panStart.y()) / m_mag;
        m_ox = m_panOxStart - dx;
        m_oy = m_panOyStart - dy;
        update();
    }

    if (m_zoomWndDragging)
        update();
}

void ScalarResultsWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_panning && event->button() == Qt::MiddleButton) {
        m_panning = false;
        setCursor(Qt::ArrowCursor);
    }
    // Zoom window: complete the drag
    if (m_zoomWndDragging && event->button() == Qt::LeftButton) {
        m_zoomWndDragging = false;
        m_zoomWndFlag = false;
        setCursor(Qt::ArrowCursor);
        double x0d, y0d, x1d, y1d;
        toDrawing(m_zoomWndStart.x(), m_zoomWndStart.y(), x0d, y0d);
        toDrawing(event->pos().x(), event->pos().y(), x1d, y1d);
        if (x0d > x1d) std::swap(x0d, x1d);
        if (y0d > y1d) std::swap(y0d, y1d);
        double dx = x1d - x0d, dy = y1d - y0d;
        if (dx < 1e-12 || dy < 1e-12) return;
        double mx = (width() - 1.0) / dx;
        double my = (height() - 1.0) / dy;
        m_mag = std::min(mx, my);
        m_ox = x0d;
        m_oy = y0d;
        update();
    }
}

void ScalarResultsWidget::wheelEvent(QWheelEvent *event)
{
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

void ScalarResultsWidget::showPointProperties(double xd, double yd)
{
    bool isAxi = (m_view.ProblemType && *m_view.ProblemType == 1);
    QString x1 = isAxi ? "r" : "x", x2 = isAxi ? "z" : "y";
    QString text;

    if (m_physicsType == 3 && m_currDoc) {
        // Current flow — complex values via ToStringAlt format
        CPointVals u;
        if (!m_currDoc->GetPointValues(xd, yd, u)) return;
        // Format CComplex for display in native-Python "j" notation.
        auto cstr = [](CComplex z) -> QString { return femmFmtComplex(z, 6); };
        double Jmag = abs(abs(u.Jx) + CComplex(0,1)*abs(u.Jy));
        double Emag = abs(abs(u.Ex) + CComplex(0,1)*abs(u.Ey));
        double Jcmag = abs(abs(u.Jcx) + CComplex(0,1)*abs(u.Jcy));
        double Jdmag = abs(abs(u.Jdx) + CComplex(0,1)*abs(u.Jdy));
        text += QString("Point: %1=%2, %3=%4\n").arg(x1).arg(xd, 0, 'g').arg(x2).arg(yd, 0, 'g');
        text += QString("V   = %1 V\n").arg(cstr(u.V));
        text += QString("|J| = %1 A/m\u00b2\n").arg(Jmag, 0, 'g');
        text += QString("J%1  = %2 A/m\u00b2\n").arg(x1).arg(cstr(u.Jx));
        text += QString("J%1  = %2 A/m\u00b2\n").arg(x2).arg(cstr(u.Jy));
        text += QString("|E| = %1 V/m\n").arg(Emag, 0, 'g');
        text += QString("E%1  = %2 V/m\n").arg(x1).arg(cstr(u.Ex));
        text += QString("E%1  = %2 V/m\n").arg(x2).arg(cstr(u.Ey));
        text += QString("e%1  = %2 (rel)\n").arg(x1).arg(cstr(u.ex));
        text += QString("e%1  = %2 (rel)\n").arg(x2).arg(cstr(u.ey));
        text += QString("o%1  = %2 S/m\n").arg(x1).arg(u.ox, 0, 'g');
        text += QString("o%1  = %2 S/m\n").arg(x2).arg(u.oy, 0, 'g');
        text += QString("|Jc| = %1 A/m\u00b2\n").arg(Jcmag, 0, 'g');
        text += QString("|Jd| = %1 A/m\u00b2\n").arg(Jdmag, 0, 'g');
        emit statusMessage(QString("V = %1 V, |J| = %2 A/m\u00b2")
            .arg(u.V.re, 0, 'g', 4).arg(Jmag, 0, 'g', 4));
    } else if (m_physicsType == 2) {
        // Heat flow
        EPointVals u;
        if (!m_view.GetPointValues(xd, yd, u)) return;
        text += QString("Point: %1=%2, %3=%4\n").arg(x1).arg(xd, 0, 'g').arg(x2).arg(yd, 0, 'g');
        text += QString("T   = %1 K\n").arg(u.V, 0, 'g');
        text += QString("|F| = %1 W/m\u00b2\n").arg(abs(u.D), 0, 'g');
        text += QString("F%1  = %2 W/m\u00b2\n").arg(x1).arg(u.D.re, 0, 'g');
        text += QString("F%1  = %2 W/m\u00b2\n").arg(x2).arg(u.D.im, 0, 'g');
        text += QString("|G| = %1 K/m\n").arg(abs(u.E), 0, 'g');
        text += QString("G%1  = %2 K/m\n").arg(x1).arg(u.E.re, 0, 'g');
        text += QString("G%1  = %2 K/m\n").arg(x2).arg(u.E.im, 0, 'g');
        text += QString("K%1  = %2 W/(m*K)\n").arg(x1).arg(u.e.re, 0, 'g');
        text += QString("K%1  = %2 W/(m*K)\n").arg(x2).arg(u.e.im, 0, 'g');
        emit statusMessage(QString("T = %1 K, |F| = %2 W/m\u00b2")
            .arg(u.V, 0, 'g', 4).arg(abs(u.D), 0, 'g', 4));
    } else {
        // Electrostatics
        EPointVals u;
        if (!m_view.GetPointValues(xd, yd, u)) return;
        text += QString("Point: %1=%2, %3=%4\n").arg(x1).arg(xd, 0, 'g').arg(x2).arg(yd, 0, 'g');
        text += QString("V   = %1 V\n").arg(u.V, 0, 'g');
        text += QString("|D| = %1 C/m\u00b2\n").arg(abs(u.D), 0, 'g');
        text += QString("D%1  = %2 C/m\u00b2\n").arg(x1).arg(u.D.re, 0, 'g');
        text += QString("D%1  = %2 C/m\u00b2\n").arg(x2).arg(u.D.im, 0, 'g');
        text += QString("|E| = %1 V/m\n").arg(abs(u.E), 0, 'g');
        text += QString("E%1  = %2 V/m\n").arg(x1).arg(u.E.re, 0, 'g');
        text += QString("E%1  = %2 V/m\n").arg(x2).arg(u.E.im, 0, 'g');
        text += QString("e%1  = %2 (rel)\n").arg(x1).arg(u.e.re, 0, 'g');
        text += QString("e%1  = %2 (rel)\n").arg(x2).arg(u.e.im, 0, 'g');
        text += QString("nrg = %1 J/m\u00b3\n").arg(u.nrg, 0, 'g');
        emit statusMessage(QString("V = %1 V, |E| = %2 V/m")
            .arg(u.V, 0, 'g', 4).arg(abs(u.E), 0, 'g', 4));
    }
    emit outputReady(text);
}

bool ScalarResultsWidget::event(QEvent *ev)
{
    if (ev->type() == QEvent::KeyPress) {
        auto *ke = static_cast<QKeyEvent*>(ev);
        if (ke->key() == Qt::Key_Tab) {
            keyPressEvent(ke);
            return true;
        }
    }
    return QWidget::event(ev);
}

void ScalarResultsWidget::keyPressEvent(QKeyEvent *event)
{
    switch (event->key()) {
    case Qt::Key_PageUp:   zoomIn(); break;
    case Qt::Key_PageDown: zoomOut(); break;
    case Qt::Key_Home:     zoomFit(); break;
    case Qt::Key_Left:     panLeft(); break;
    case Qt::Key_Right:    panRight(); break;
    case Qt::Key_Up:       panUp(); break;
    case Qt::Key_Down:     panDown(); break;

    case Qt::Key_Escape:
        if (m_mode == 1 && m_view.meshnode) {
            (*m_view.contour).clear();
            emit statusMessage("Contour cleared");
            update();
        }
        if (m_mode == 2 && m_view.meshnode) {
            clearAreaSelection();
            update();
        }
        break;

    case Qt::Key_Delete:
        if (m_mode == 1 && m_view.meshnode && !(*m_view.contour).empty()) {
            (*m_view.contour).pop_back();
            emit statusMessage(QString("Contour: %1 points").arg((*m_view.contour).size()));
            update();
        }
        break;

    case Qt::Key_Tab:
        if (m_mode <= 1 && m_view.meshnode) {
            // Labelled and interpreted per the document's display mode:
            // (x,y), (r,z) axisymmetric, or radius/degrees in polar.
            const int coords = m_view.Coords ? *m_view.Coords : 0;
            const int ptype = m_view.ProblemType ? *m_view.ProblemType : 0;
            double c1, c2;
            QString l1, l2;
            coorddisp::toDisplay(coords, m_mx, m_my, c1, c2);
            coorddisp::entryLabels(coords, ptype, l1, l2);
            QDialog dlg(this);
            dlg.setWindowTitle(m_mode == 0 ? tr("Enter Point") : tr("Enter Contour Point"));
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
                if (m_mode == 0)
                    showPointProperties(x, y);
                else {
                    (*m_view.contour).push_back(CComplex(x, y));
                    emit statusMessage(QString("Contour: %1 points").arg((*m_view.contour).size()));
                    update();
                }
            }
        }
        break;

    default: QWidget::keyPressEvent(event);
    }
}


// ================================================================
// Phase slider visibility (current flow AC only)
// ================================================================

void ScalarResultsWidget::updatePhaseSliderVisibility()
{
    bool show = m_showVectors && m_currDoc && m_currDoc->Frequency != 0;
    if (show && m_phaseSlider) {
        int bw = m_playBtn->width() + 4;
        int lw = m_phaseLabel->width() + 4;
        int sw = width() - bw - lw - 20;
        if (sw < 50) sw = 50;
        m_playBtn->move(6, height() - 25);
        m_phaseSlider->setGeometry(6 + bw, height() - 25, sw, 20);
        m_phaseLabel->move(6 + bw + sw + 4, height() - 25);
        m_playBtn->show();
        m_phaseSlider->show();
        m_phaseLabel->show();
    } else if (m_phaseSlider) {
        if (m_playTimer && m_playTimer->isActive()) {
            m_playTimer->stop();
            m_playBtn->setText("\xe2\x96\xb6");
        }
        m_playBtn->hide();
        m_phaseSlider->hide();
        m_phaseLabel->hide();
    }
}

// ================================================================
// Postprocessor dialogs — delegated to handler
// ================================================================

void ScalarResultsWidget::showPrefsDialog(QWidget *parent) { m_handler->showPrefsDialog(parent); }
void ScalarResultsWidget::showIntegrateDialog(QWidget *parent, OutputDialog *out) { m_handler->showIntegrateDialog(parent, out); }
void ScalarResultsWidget::showXYPlotDialog(QWidget *parent) { m_handler->showXYPlotDialog(parent); }
void ScalarResultsWidget::showCircuitDialog(QWidget *parent) { m_handler->showCircuitDialog(parent); }
void ScalarResultsWidget::showContourPlotDialog(QWidget *parent) { m_handler->showContourPlotDialog(parent); }
void ScalarResultsWidget::showDensityPlotDialog(QWidget *parent) { m_handler->showDensityPlotDialog(parent); }
void ScalarResultsWidget::showVectorPlotDialog(QWidget *parent) { m_handler->showVectorPlotDialog(parent); }
void ScalarResultsWidget::showProblemInfo(QWidget *parent) { m_handler->showProblemInfo(parent); }
