// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors

// results_widget.cpp — Magnetics postprocessor results display
// Renders |B| density plot as colored triangles and A equipotential contours.
// Color map and rendering approach ported from FemmviewView.cpp.

#include "results_widget.h"
#include "complex_format.h"
#include "results_doc.h"
#include "py_state.h"
#include "script_callbacks.h"
#include "view_colors.h"
#include "magnetics_handler.h"

#include <QPainter>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QInputDialog>
#include <QFormLayout>
#include <QVBoxLayout>
#include <QLineEdit>
#include "py_line_edit.h"
#include <QDialogButtonBox>
#include <QSlider>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <cmath>
#include <algorithm>

#ifndef PI
#define PI 3.141592653589793238462643383279
#endif

// Colors now come from the global viewColors() singleton
QColor ResultsWidget::densityColor(int level)
{
    return viewColors().densityColor(level);
}

// ================================================================
// Constructor
// ================================================================

void ResultsWidget::setMode(int mode)
{
    if (m_doc) {
        // Clear contour when leaving contour mode
        if (m_mode == 1 && mode != 1)
            m_doc->contour.clear();
        // Clear block selections when leaving area mode
        if (m_mode == 2 && mode != 2)
            for (auto &lbl : m_doc->labellist) lbl.IsSelected = false;
    }
    m_mode = mode;
    update();
}

ResultsWidget::ResultsWidget(ResultsDoc *doc, QWidget *parent)
    : QWidget(parent)
    , m_doc(doc)
    , m_handler(new MagneticsHandler(this))
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
    m_phaseLabel = new QLabel("0\xc2\xb0", this);  // "0°"
    m_phaseLabel->setFixedSize(40, 20);
    m_phaseLabel->setAlignment(Qt::AlignCenter);
    m_phaseLabel->hide();

    connect(m_phaseSlider, &QSlider::valueChanged, [this](int val) {
        m_vectorPhase = val;
        m_phaseLabel->setText(QString("%1\xc2\xb0").arg(val));
        update();
    });

    m_playBtn = new QPushButton("\xe2\x96\xb6", this);  // "▶"
    m_playBtn->setFixedSize(24, 20);
    m_playBtn->setToolTip(tr("Animate phase sweep"));
    m_playBtn->hide();

    m_playTimer = new QTimer(this);
    m_playTimer->setInterval(14);  // ~70 fps, 5s for full 360° → 1°/tick
    connect(m_playTimer, &QTimer::timeout, [this]() {
        int val = m_phaseSlider->value() + 1;
        if (val > 360) val = 0;
        m_phaseSlider->setValue(val);
    });
    connect(m_playBtn, &QPushButton::clicked, [this]() {
        if (m_playTimer->isActive()) {
            m_playTimer->stop();
            m_playBtn->setText("\xe2\x96\xb6");  // ▶
        } else {
            m_playTimer->start();
            m_playBtn->setText("\xe2\x96\xa0");  // ■ (stop)
        }
    });
}

ResultsWidget::~ResultsWidget()
{
    // Remove from registry before deleting so mi_loadsolution knows
    // the doc has been deleted and won't double-free it.
    if (m_doc) doc_registry_remove(m_doc);
    delete m_doc;
    m_doc = nullptr;
}

// ================================================================
// Coordinate transforms
// ================================================================

QPointF ResultsWidget::toScreen(double xd, double yd) const
{
    double sx = m_mag * (xd - m_ox);
    double sy = (height() - 1) - m_mag * (yd - m_oy);
    return QPointF(sx, sy);
}

void ResultsWidget::toDrawing(int xs, int ys, double &xd, double &yd) const
{
    xd = xs / m_mag + m_ox;
    yd = (height() - 1 - ys) / m_mag + m_oy;
}

void ResultsWidget::snapToGrid(double &xd, double &yd) const
{
    if (m_snapToGrid && m_gridSize > 0) {
        xd = m_gridSize * floor(xd / m_gridSize + 0.5);
        yd = m_gridSize * floor(yd / m_gridSize + 0.5);
    }
}

// ================================================================
// Zoom / pan
// ================================================================

void ResultsWidget::zoomIn()
{
    double cx = width() / 2.0 / m_mag + m_ox;
    double cy = height() / 2.0 / m_mag + m_oy;
    m_mag *= 1.5;
    m_ox = cx - width() / 2.0 / m_mag;
    m_oy = cy - height() / 2.0 / m_mag;
    update();
}

void ResultsWidget::zoomOut()
{
    double cx = width() / 2.0 / m_mag + m_ox;
    double cy = height() / 2.0 / m_mag + m_oy;
    m_mag /= 1.5;
    if (m_mag < 0.01) m_mag = 0.01;
    m_ox = cx - width() / 2.0 / m_mag;
    m_oy = cy - height() / 2.0 / m_mag;
    update();
}

void ResultsWidget::zoomFit()
{
    if (!m_doc || m_doc->meshnode.empty()) return;

    double xmin = m_doc->meshnode[0].x, xmax = xmin;
    double ymin = m_doc->meshnode[0].y, ymax = ymin;
    for (auto &nd : m_doc->meshnode) {
        if (nd.x < xmin) xmin = nd.x;
        if (nd.x > xmax) xmax = nd.x;
        if (nd.y < ymin) ymin = nd.y;
        if (nd.y > ymax) ymax = nd.y;
    }

    double dx = xmax - xmin, dy = ymax - ymin;
    if (dx < 1e-12) dx = 1;
    if (dy < 1e-12) dy = 1;
    double margin = 0.05;
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

void ResultsWidget::zoomTo(double x0, double y0, double x1, double y1)
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

void ResultsWidget::zoomWindow()
{
    m_zoomWndFlag = true;
    emit statusMessage("Zoom: click and drag to define window");
}

void ResultsWidget::panLeft()  { m_ox -= 0.25 * width() / m_mag; update(); }
void ResultsWidget::panRight() { m_ox += 0.25 * width() / m_mag; update(); }
void ResultsWidget::panUp()    { m_oy += 0.25 * height() / m_mag; update(); }
void ResultsWidget::panDown()  { m_oy -= 0.25 * height() / m_mag; update(); }

// ================================================================
// Paint
// ================================================================

void ResultsWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), viewColors().background());

    if (!m_doc || m_doc->meshelem.empty()) return;

    p.setRenderHint(QPainter::Antialiasing, false);  // speed for triangles

    // Density and vector plots are either/or
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
// Density plot — fill each triangle with color based on |B|
// ================================================================

void ResultsWidget::drawDensityPlot(QPainter &p)
{
    int flag = m_densityPlotType;
    if (flag <= 0) return;

    double bl = m_doc->PlotBounds[flag - 1][0];
    double bh = m_doc->PlotBounds[flag - 1][1];
    if (bh - bl < 1e-30) bh = bl + 1;

    p.setPen(Qt::NoPen);

    for (auto &el : m_doc->meshelem) {
        double bn[3];  // field value at each vertex

        // Read precomputed nodal values based on plot type.
        // Smooth: use nodal (b1[i], b2[i], h1[i], h2[i], J[i])
        // Flat: use element (B1, B2) for B-based quantities.
        if (m_doc->Frequency != 0) {
            // AC: 1=|B|, 2=Re|B|, 3=Im|B|, 4=|H|, 5=Re|H|, 6=Im|H|,
            //     7=|J|, 8=Re|J|, 9=Im|J|, 10=log|B|
            for (int i = 0; i < 3; i++) {
                CComplex b1 = m_doc->Smooth ? el.b1[i] : el.B1;
                CComplex b2 = m_doc->Smooth ? el.b2[i] : el.B2;
                CComplex h1 = el.h1[i], h2 = el.h2[i];
                CComplex J = el.J[i];

                switch (flag) {
                case 1:  bn[i] = sqrt(b1.re*b1.re + b1.im*b1.im + b2.re*b2.re + b2.im*b2.im); break;
                case 2:  bn[i] = sqrt(b1.re*b1.re + b2.re*b2.re); break;
                case 3:  bn[i] = sqrt(b1.im*b1.im + b2.im*b2.im); break;
                case 4:  bn[i] = sqrt(h1.re*h1.re + h1.im*h1.im + h2.re*h2.re + h2.im*h2.im); break;
                case 5:  bn[i] = sqrt(h1.re*h1.re + h2.re*h2.re); break;
                case 6:  bn[i] = sqrt(h1.im*h1.im + h2.im*h2.im); break;
                case 7:  bn[i] = abs(J) * 1e-6; break;
                case 8:  bn[i] = fabs(J.re) * 1e-6; break;
                case 9:  bn[i] = fabs(J.im) * 1e-6; break;
                case 10: {
                    bn[i] = sqrt(b1.re*b1.re + b1.im*b1.im + b2.re*b2.re + b2.im*b2.im);
                    bn[i] = (bn[i] > 0) ? log10(bn[i]) : -12.0;
                    if (bn[i] < -12.0) bn[i] = -12.0;
                    break;
                }
                default: bn[i] = 0; break;
                }
            }
        } else {
            // DC: 1=|B|, 2=|H|, 3=|J|, 4=log|B|
            for (int i = 0; i < 3; i++) {
                CComplex b1 = m_doc->Smooth ? el.b1[i] : el.B1;
                CComplex b2 = m_doc->Smooth ? el.b2[i] : el.B2;

                switch (flag) {
                case 1:  bn[i] = sqrt(b1.re*b1.re + b2.re*b2.re); break;
                case 2:  bn[i] = sqrt(el.h1[i].re*el.h1[i].re + el.h2[i].re*el.h2[i].re); break;
                case 3:  bn[i] = abs(el.J[i]) * 1e-6; break;
                case 4: {
                    bn[i] = sqrt(b1.re*b1.re + b2.re*b2.re);
                    bn[i] = (bn[i] > 0) ? log10(bn[i]) : -12.0;
                    if (bn[i] < -12.0) bn[i] = -12.0;
                    break;
                }
                default: bn[i] = 0; break;
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
            QPointF sp = toScreen(m_doc->meshnode[el.p[i]].x, m_doc->meshnode[el.p[i]].y);
            c[n++] = {sp.x(), sp.y(), sn[i]};

            // Walk edge from vertex i to vertex j, insert crossing points
            int j = (i + 1) % 3;
            QPointF sp0 = toScreen(m_doc->meshnode[el.p[i]].x, m_doc->meshnode[el.p[i]].y);
            QPointF sp1 = toScreen(m_doc->meshnode[el.p[j]].x, m_doc->meshnode[el.p[j]].y);

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
// Contour lines — equipotential lines of A (vector potential)
// ================================================================

void ResultsWidget::drawContours(QPainter &p)
{
    double aLow = m_doc->A_low;
    double aHigh = m_doc->A_high;
    if (aHigh - aLow < 1e-30) return;

    int nc = m_numContours;
    int edges[3][2] = {{0,1}, {1,2}, {2,0}};

    // Field-line (A-contour) crossing on edge (i0,i1) at value `level`.
    //
    // Planar: A is linear within the element, so the crossing is linear in A.
    //
    // Axisymmetric: the stored nodal A is already the flux 2*pi*r*A_phi
    // (FemmviewDoc.cpp:1780), which FEMM represents with a 6-node quadratic
    // element -- the same one results_doc.cpp uses to compute B, and that OF
    // uses for point readouts because a plain A-based interpolation "can't
    // represent constant flux density very well" (FemmviewDoc.cpp:1793). We use
    // that quadratic for the flux lines: build the edge mid-side value, then
    // solve the quadratic through (v0, amid, v1). (OF's own flux-line drawing
    // never adopted this -- its DoContours still uses an older A/r-linear
    // correction, which wobbles in low-field regions like air.) The mask pass
    // below stays linear; the mask is a 0..1 scalar, not an axisymmetric flux.
    const bool isAxi = (m_doc->ProblemType == 1);

    auto edgeCrossing = [&](const double *a, const double *x, const double *y,
                            int i0, int i1, double level) -> QPointF {
        double v0 = a[i0], v1 = a[i1];
        double s;
        if (isAxi) {
            double R0 = x[i0], R1 = x[i1];           // node radii
            double amid;
            if (R0 < 1e-6 && R1 < 1e-6) amid = (v0 + v1) / 2.0;
            else amid = (R1 * (3.0 * v0 + v1) + R0 * (v0 + 3.0 * v1)) /
                        (4.0 * (R0 + R1));
            // Flux varies quadratically through (v0, amid, v1) along the edge:
            // F(s) = v0*N0 + amid*Nm + v1*N1. Solve F(s) = level.
            double qa = 2.0 * v0 - 4.0 * amid + 2.0 * v1;
            double qb = -3.0 * v0 + 4.0 * amid - v1;
            double qc = v0 - level;
            // Numerically stable quadratic roots (Numerical Recipes form). On a
            // constant-r edge -- one running purely along z -- amid is the
            // linear midpoint, so qa is zero in exact arithmetic but a tiny
            // round-off crumb in floating point. The naive (-qb +/- sqrt)/(2*qa)
            // then catastrophically cancels and yields a garbage crossing that
            // quantizes to a few values; worse, the two elements sharing the
            // edge cancel differently, so their segments don't meet. The
            // factored form never cancels and degrades smoothly to the exact
            // linear crossing as qa -> 0.
            double disc = qb * qb - 4.0 * qa * qc;
            if (disc < 0) disc = 0;
            double sq = sqrt(disc);
            double qq = -0.5 * (qb + (qb >= 0.0 ? sq : -sq));
            // The two roots are qq/qa and qc/qq; one is the bracketed crossing
            // in [0,1]. Guard the divisions for the degenerate qa->0 / qq->0.
            double s1 = (qa != 0.0) ? (qq / qa) : 2.0;   // 2.0 == "out of range"
            double s2 = (qq != 0.0) ? (qc / qq) : 2.0;
            s = (s1 >= 0.0 && s1 <= 1.0) ? s1 : s2;
        } else {
            s = (level - v0) / (v1 - v0);
        }
        if (s < 0.0) s = 0.0; else if (s > 1.0) s = 1.0;
        return toScreen(x[i0] + s * (x[i1] - x[i0]),
                        y[i0] + s * (y[i1] - y[i0]));
    };

    // Helper lambda: draw contours for a given A component
    auto drawPass = [&](bool useIm, const QColor &color) {
        p.setPen(QPen(color, 1));
        for (auto &el : m_doc->meshelem) {
            double a[3], x[3], y[3];
            for (int i = 0; i < 3; i++) {
                a[i] = useIm ? m_doc->meshnode[el.p[i]].A.im
                             : m_doc->meshnode[el.p[i]].A.re;
                x[i] = m_doc->meshnode[el.p[i]].x;
                y[i] = m_doc->meshnode[el.p[i]].y;
            }
            // Levels 0..nc-1 (the A_Low contour through the top one below
            // A_High), matching OF's DoContours: i in [0, NumContours). Starting
            // at 0 includes the lowest line -- important when the user has set
            // an explicit A_Low/A_High range, where that line is not degenerate.
            for (int k = 0; k < nc; k++) {
                double level = aLow + k * (aHigh - aLow) / nc;
                QPointF crossings[3];
                int nCross = 0;
                for (int e = 0; e < 3 && nCross < 2; e++) {
                    int i0 = edges[e][0], i1 = edges[e][1];
                    double v0 = a[i0], v1 = a[i1];
                    if ((v0 <= level && level < v1) || (v1 <= level && level < v0))
                        crossings[nCross++] = edgeCrossing(a, x, y, i0, i1, level);
                }
                if (nCross == 2)
                    p.drawLine(crossings[0], crossings[1]);
            }
        }
    };

    if (m_showAr) drawPass(false, viewColors().realFluxLine());
    if (m_showAi) drawPass(true, viewColors().imagFluxLine());

    // Stress tensor mask contours (0-1 range, orange)
    if (m_showMask && m_doc->bHasMask) {
        int ncMask = 10;  // 10 contour levels for the mask
        p.setPen(QPen(QColor(255, 140, 0), 1));  // orange
        for (auto &el : m_doc->meshelem) {
            double mv[3], x[3], y[3];
            for (int i = 0; i < 3; i++) {
                mv[i] = m_doc->meshnode[el.p[i]].msk * ncMask;
                x[i] = m_doc->meshnode[el.p[i]].x;
                y[i] = m_doc->meshnode[el.p[i]].y;
            }
            for (int contour = 0; contour < ncMask; contour++) {  // 0..ncMask-1, like the A contours
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

void ResultsWidget::drawMesh(QPainter &p)
{
    p.setPen(QPen(viewColors().mesh(), 0));
    for (auto &el : m_doc->meshelem) {
        QPointF pts[3];
        for (int i = 0; i < 3; i++)
            pts[i] = toScreen(m_doc->meshnode[el.p[i]].x, m_doc->meshnode[el.p[i]].y);
        p.drawLine(pts[0], pts[1]);
        p.drawLine(pts[1], pts[2]);
        p.drawLine(pts[2], pts[0]);
    }
}

// ================================================================
// Geometry overlay — segments, arcs, nodes drawn on top of results
// Matches FemmviewView.cpp rendering order
// ================================================================

void ResultsWidget::drawBlockNames(QPainter &p)
{
    QFont font = p.font();
    font.setPointSize(8);
    p.setFont(font);
    QColor nameClr = viewColors().name();
    QColor blkClr = viewColors().blockLabel();

    for (auto &lbl : m_doc->labellist) {
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

        // Magnetization direction arrow for PM materials
        if (lbl.BlockType >= 0 && lbl.BlockType < (int)m_doc->blockproplist.size() &&
            lbl.MagDirFctn.empty() &&
            m_doc->blockproplist[lbl.BlockType].H_c != 0) {
            double theta = lbl.MagDir * PI / 180.0;
            double ct = cos(theta), st = sin(theta);
            QPointF p1(sp.x() - 10*ct, sp.y() + 10*st);
            QPointF p2(sp.x() + 10*ct, sp.y() - 10*st);
            p.setPen(QPen(c, 1));
            p.drawLine(p1, p2);
            double ax = 6.0 / sqrt(2.0);
            QPointF ah1(p2.x() - ax*(ct - st), p2.y() + ax*(st + ct));
            QPointF ah2(p2.x() - ax*(ct + st), p2.y() + ax*(st - ct));
            p.drawLine(p2, ah1);
            p.drawLine(p2, ah2);
        }

        // Block type name
        if (lbl.BlockType >= 0 && lbl.BlockType < (int)m_doc->blockproplist.size()) {
            p.setPen(nameClr);
            p.drawText(QPointF(sp.x() + 5, sp.y() - 2),
                QString::fromStdString(m_doc->blockproplist[lbl.BlockType].BlockName));
        }
    }
}

void ResultsWidget::drawGeometry(QPainter &p)
{
    if (!m_doc) return;

    QColor kLineColor = viewColors().line();
    QColor kNodeColor = viewColors().node();

    // Draw segments
    p.setPen(QPen(kLineColor, 1));
    for (auto &seg : m_doc->linelist) {
        if (seg.Hidden) continue;
        if (seg.n0 < 0 || seg.n0 >= (int)m_doc->nodelist.size()) continue;
        if (seg.n1 < 0 || seg.n1 >= (int)m_doc->nodelist.size()) continue;
        QPointF p0 = toScreen(m_doc->nodelist[seg.n0].x, m_doc->nodelist[seg.n0].y);
        QPointF p1 = toScreen(m_doc->nodelist[seg.n1].x, m_doc->nodelist[seg.n1].y);
        p.drawLine(p0, p1);
    }

    // Draw arc segments
    for (auto &arc : m_doc->arclist) {
        if (arc.Hidden) continue;
        if (arc.n0 < 0 || arc.n0 >= (int)m_doc->nodelist.size()) continue;
        if (arc.n1 < 0 || arc.n1 >= (int)m_doc->nodelist.size()) continue;

        CComplex center;
        double R;
        m_doc->GetCircle(arc, center, R);

        CComplex a0(m_doc->nodelist[arc.n0].x, m_doc->nodelist[arc.n0].y);

        double mySideLength = arc.mySideLength;
        if (mySideLength <= 0) mySideLength = arc.MaxSideLength;
        if (mySideLength <= 0) mySideLength = 5.0;
        int k = (int)ceil(arc.ArcLength / mySideLength);
        if (k < 1) k = 1;
        double dt = arc.ArcLength * PI / (180.0 * k);

        p.setPen(QPen(kLineColor, 1));
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
        for (auto &n : m_doc->nodelist) {
            QPointF sp = toScreen(n.x, n.y);
            p.drawRect(QRectF(sp.x() - 2, sp.y() - 2, 4, 4));
        }
    }
}

// ================================================================
// Color legend
// ================================================================

void ResultsWidget::drawLegend(QPainter &p)
{
    if (!m_showLegend) return;

    int flag;
    double bl, bh;
    if (m_showVectors) {
        // Vector plot uses B or H bounds directly
        if (m_vectorType == 0) { bl = m_doc->B_low; bh = m_doc->B_high; }
        else { bl = m_doc->H_low; bh = m_doc->H_high; }
        flag = (m_vectorType == 0) ? 1 : (m_doc->Frequency == 0 ? 2 : 4);
    } else {
        if (m_densityPlotType <= 0) return;
        flag = m_densityPlotType;
        bl = m_doc->PlotBounds[flag - 1][0];
        bh = m_doc->PlotBounds[flag - 1][1];
    }

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

    // Title based on plot type
    static const char *dcLabels[] = { "", "|B| [T]", "|H| [A/m]", "|J| [MA/m^2]", "log|B| [T]" };
    static const char *acLabels[] = { "", "|B| [T]", "Re|B| [T]", "Im|B| [T]",
        "|H| [A/m]", "Re|H| [A/m]", "Im|H| [A/m]",
        "|J| [MA/m^2]", "Re|J|", "Im|J|", "log|B| [T]" };

    const char *title = "";
    if (m_doc->Frequency == 0 && flag >= 1 && flag <= 4) title = dcLabels[flag];
    else if (m_doc->Frequency != 0 && flag >= 1 && flag <= 10) title = acLabels[flag];

    p.setPen(Qt::black);
    p.drawText(lx, ly - 4, title);
}

void ResultsWidget::drawContourPath(QPainter &p)
{
    if (!m_doc || m_doc->contour.size() < 1 || m_mode != 1) return;

    p.setPen(QPen(QColor(255, 0, 0), 2));  // red — distinct from contour plot colors
    for (size_t i = 0; i < m_doc->contour.size(); i++) {
        QPointF sp = toScreen(m_doc->contour[i].re, m_doc->contour[i].im);
        // Draw a small cross at each contour point
        p.drawLine(QPointF(sp.x()-4, sp.y()), QPointF(sp.x()+4, sp.y()));
        p.drawLine(QPointF(sp.x(), sp.y()-4), QPointF(sp.x(), sp.y()+4));
        // Draw line to next point
        if (i > 0) {
            QPointF prev = toScreen(m_doc->contour[i-1].re, m_doc->contour[i-1].im);
            p.drawLine(prev, sp);
        }
    }
}

void ResultsWidget::updatePhaseSliderVisibility()
{
    bool show = m_showVectors && m_doc && m_doc->Frequency != 0;
    if (show) {
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
    } else {
        if (m_playTimer->isActive()) {
            m_playTimer->stop();
            m_playBtn->setText("\xe2\x96\xb6");
        }
        m_playBtn->hide();
        m_phaseSlider->hide();
        m_phaseLabel->hide();
    }
}

void ResultsWidget::drawVectorPlot(QPainter &p)
{
    if (!m_doc || m_doc->meshelem.empty()) return;

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
    double bLow = m_doc->B_low, bHigh = m_doc->B_high;
    if (m_vectorType == 1) { bLow = m_doc->H_low; bHigh = m_doc->H_high; }
    if (bHigh <= bLow) bHigh = bLow + 1;

    p.setRenderHint(QPainter::Antialiasing, true);

    for (int ix = 0; ix < Nx; ix++) {
        for (int iy = 0; iy < Ny; iy++) {
            double xd = x0d + (ix + 0.5) * dx;
            double yd = y0d + (iy + 0.5) * dy;

            int elm = m_doc->InTriangle(xd, yd);
            if (elm < 0) continue;

            ResultsDoc::PointVals v;
            if (!m_doc->GetPointValues(xd, yd, elm, v)) continue;

            double fx, fy, mag;
            double cosph = cos(m_vectorPhase * PI / 180.0);
            double sinph = sin(m_vectorPhase * PI / 180.0);
            if (m_vectorType == 0) {
                // B field — instantaneous value at phase angle
                fx = v.B1.re * cosph - v.B1.im * sinph;
                fy = v.B2.re * cosph - v.B2.im * sinph;
            } else {
                // H field
                fx = v.H1.re * cosph - v.H1.im * sinph;
                fy = v.H2.re * cosph - v.H2.im * sinph;
            }
            mag = sqrt(fx * fx + fy * fy);  // instantaneous magnitude

            double fmag = sqrt(fx * fx + fy * fy);
            if (fmag < 1e-20) continue;

            // Normalize direction
            double ux = fx / fmag, uy = fy / fmag;

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
            // Rotate ±30 degrees
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

void ResultsWidget::drawSelectedBlocks(QPainter &p)
{
    if (!m_doc) return;

    // Highlight selected blocks by drawing their triangles with a green tint
    p.setPen(Qt::NoPen);
    QColor sel(0, 255, 0, 140);  // semi-transparent green
    for (auto &elem : m_doc->meshelem) {
        if (elem.lbl < 0 || elem.lbl >= (int)m_doc->labellist.size()) continue;
        if (!m_doc->labellist[elem.lbl].IsSelected) continue;

        QPointF pts[3];
        for (int j = 0; j < 3; j++) {
            auto &nd = m_doc->meshnode[elem.p[j]];
            pts[j] = toScreen(nd.x, nd.y);
        }
        p.setBrush(sel);
        p.drawPolygon(pts, 3);
    }
}

// ================================================================
// Mouse events
// ================================================================

void ResultsWidget::mousePressEvent(QMouseEvent *event)
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

        if (!m_doc) return;
        double xd, yd;
        toDrawing(event->pos().x(), event->pos().y(), xd, yd);
        if (m_mode == 0) snapToGrid(xd, yd);

        if (m_mode == 0) {
            showPointProperties(xd, yd);
        }
        else if (m_mode == 1) {
            // Contour mode: left-click snaps to nearest input node
            // and adds intermediate points along connecting arcs
            if (m_doc->nodelist.empty()) return;
            int nodeIdx = m_doc->ClosestInputNode(xd, yd);
            if (nodeIdx < 0) return;
            CComplex z(m_doc->nodelist[nodeIdx].x, m_doc->nodelist[nodeIdx].y);

            if (m_doc->contour.empty()) {
                m_doc->contour.push_back(z);
            } else {
                CComplex y = m_doc->contour.back();
                if (y.re == z.re && y.im == z.im) return;  // same point

                int prevNode = m_doc->ClosestInputNode(y.re, y.im);
                CComplex x(m_doc->nodelist[prevNode].x, m_doc->nodelist[prevNode].y);

                // Check if previous and new node are ends of a segment or arc
                int arcno = -1;
                bool arcForward = true;
                double d1 = 1.e8;

                if (abs(x - y) < 1.e-08) {
                    // Check segments
                    for (int k = 0; k < (int)m_doc->linelist.size(); k++) {
                        if ((m_doc->linelist[k].n0 == prevNode && m_doc->linelist[k].n1 == nodeIdx) ||
                            (m_doc->linelist[k].n0 == nodeIdx && m_doc->linelist[k].n1 == prevNode)) {
                            double d2 = fabs(m_doc->ShortestDistanceFromSegment(xd, yd, k));
                            if (d2 < d1) { arcno = -1; d1 = d2; }
                        }
                    }
                    // Check arcs
                    for (int k = 0; k < (int)m_doc->arclist.size(); k++) {
                        if (m_doc->arclist[k].n0 == prevNode && m_doc->arclist[k].n1 == nodeIdx) {
                            double d2 = m_doc->ShortestDistanceFromInputArc(CComplex(xd, yd), k);
                            if (d2 < d1) { arcno = k; arcForward = true; d1 = d2; }
                        }
                        if (m_doc->arclist[k].n0 == nodeIdx && m_doc->arclist[k].n1 == prevNode) {
                            double d2 = m_doc->ShortestDistanceFromInputArc(CComplex(xd, yd), k);
                            if (d2 < d1) { arcno = k; arcForward = false; d1 = d2; }
                        }
                    }
                }

                if (arcno >= 0) {
                    // Add intermediate points along the arc
                    CComplex c; double R;
                    m_doc->GetInputCircle(arcno, c, R);
                    int n = (int)ceil(m_doc->arclist[arcno].ArcLength /
                                     m_doc->arclist[arcno].MaxSideLength);
                    if (n < 1) n = 1;
                    double dtta = m_doc->arclist[arcno].ArcLength * PI / (180.0 * n);
                    CComplex rot = exp(CComplex(0, arcForward ? dtta : -dtta));
                    for (int i = 0; i < n; i++) {
                        y = (y - c) * rot + c;
                        m_doc->contour.push_back(y);
                    }
                } else {
                    // Straight line or unconnected — just add the node
                    m_doc->contour.push_back(z);
                }
            }
            emit statusMessage(QString("Contour: %1 points").arg(m_doc->contour.size()));
            update();
        }
        else if (m_mode == 2) {
            // Area mode: toggle block selection
            int k = m_doc->InTriangle(xd, yd);
            if (k >= 0) {
                int lbl = m_doc->meshelem[k].lbl;
                if (lbl >= 0 && lbl < (int)m_doc->labellist.size()) {
                    m_doc->labellist[lbl].ToggleSelect();
                    m_doc->bHasMask = false;  // invalidate mask on selection change
                    update();
                }
            }
        }
    }

    // Right-click in contour mode: add arbitrary point
    if (event->button() == Qt::RightButton && m_mode == 1 && m_doc) {
        double xd, yd;
        toDrawing(event->pos().x(), event->pos().y(), xd, yd);
        snapToGrid(xd, yd);
        CComplex z(xd, yd);
        if (!m_doc->contour.empty() && z == m_doc->contour.back()) return;
        m_doc->contour.push_back(z);
        emit statusMessage(QString("Contour: %1 points").arg(m_doc->contour.size()));
        update();
    }
}

void ResultsWidget::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton || m_mode != 2 || !m_doc) return;
    double xd, yd;
    toDrawing(event->pos().x(), event->pos().y(), xd, yd);
    int k = m_doc->InTriangle(xd, yd);
    if (k < 0) return;
    int lbl = m_doc->meshelem[k].lbl;
    if (lbl < 0 || lbl >= (int)m_doc->labellist.size()) return;
    int grp = m_doc->labellist[lbl].InGroup;
    for (auto &label : m_doc->labellist)
        if (label.InGroup == grp) label.IsSelected = true;
    m_doc->bHasMask = false;
    update();
}

void ResultsWidget::mouseMoveEvent(QMouseEvent *event)
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

void ResultsWidget::mouseReleaseEvent(QMouseEvent *event)
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

void ResultsWidget::wheelEvent(QWheelEvent *event)
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

void ResultsWidget::showPointProperties(double xd, double yd)
{
    if (!m_doc) return;
    ResultsDoc::PointVals u;
    if (!m_doc->GetPointValues(xd, yd, u)) return;

    // Format CComplex for display in native-Python "j" notation.
    auto cstr = [](CComplex z) -> QString { return femmFmtComplex(z, 6); };
    bool isAxi = (m_doc->ProblemType == 1);
    bool isAC  = (m_doc->Frequency != 0);
    double Bmag = sqrt(u.B1.re*u.B1.re + u.B2.re*u.B2.re
                     + u.B1.im*u.B1.im + u.B2.im*u.B2.im);

    QString text;
    if (!isAC) {
        double Hmag = sqrt(u.H1.re*u.H1.re + u.H2.re*u.H2.re);
        if (!isAxi) {
            text += QString("Point: x=%1, y=%2\n").arg(xd, 0, 'g').arg(yd, 0, 'g');
            text += QString("A   = %1 Wb/m\n").arg(u.A.re, 0, 'g');
        } else {
            text += QString("Point: r=%1, z=%2\n").arg(xd, 0, 'g').arg(yd, 0, 'g');
            text += QString("Flux= %1 Wb\n").arg(u.A.re, 0, 'g');
        }
        text += QString("|B| = %1 T\n").arg(Bmag, 0, 'g');
        text += QString("%1  = %2 T\n").arg(isAxi?"Br":"Bx").arg(u.B1.re, 0, 'g');
        text += QString("%1  = %2 T\n").arg(isAxi?"Bz":"By").arg(u.B2.re, 0, 'g');
        text += QString("|H| = %1 A/m\n").arg(Hmag, 0, 'g');
        text += QString("%1  = %2 A/m\n").arg(isAxi?"Hr":"Hx").arg(u.H1.re, 0, 'g');
        text += QString("%1  = %2 A/m\n").arg(isAxi?"Hz":"Hy").arg(u.H2.re, 0, 'g');
        if (u.Hc == 0) {
            text += QString("mu_%1= %2 (rel)\n").arg(isAxi?"r":"x").arg(u.mu1.re, 0, 'g');
            text += QString("mu_%1 = %2 (rel)\n").arg(isAxi?"z":"y").arg(u.mu2.re, 0, 'g');
            if (u.mu12.re != 0)
                text += QString("mu_%1= %2 (rel)\n").arg(isAxi?"rz":"xy").arg(u.mu12.re, 0, 'g');
        } else {
            double bh = abs(u.B1*u.H1 + u.B2*u.H2);
            text += QString("B.H = %1 J/m\u00b3 (%2 MGOe)\n").arg(bh, 0, 'g').arg(1.2566370614359173e-6*bh*100., 0, 'g');
        }
        text += QString("E   = %1 J/m\u00b3\n").arg(u.E, 0, 'g');
        text += QString("J   = %1 MA/m\u00b2\n").arg(u.Js.re, 0, 'g');
        if (u.ff > 0)
            text += QString("Winding Fill = %1%\n").arg(u.ff*100., 0, 'f', 2);
    } else {
        double Hmag = sqrt(u.H1.re*u.H1.re + u.H2.re*u.H2.re
                         + u.H1.im*u.H1.im + u.H2.im*u.H2.im);
        if (!isAxi) {
            text += QString("Point: x=%1, y=%2\n").arg(xd, 0, 'g').arg(yd, 0, 'g');
            text += QString("A   = %1 Wb/m\n").arg(cstr(u.A));
        } else {
            text += QString("Point: r=%1, z=%2\n").arg(xd, 0, 'g').arg(yd, 0, 'g');
            text += QString("Flux= %1 Wb\n").arg(cstr(u.A));
        }
        text += QString("|B| = %1 T\n").arg(Bmag, 0, 'g');
        text += QString("%1  = %2 T\n").arg(isAxi?"Br":"Bx").arg(cstr(u.B1));
        text += QString("%1  = %2 T\n").arg(isAxi?"Bz":"By").arg(cstr(u.B2));
        text += QString("|H| = %1 A/m\n").arg(Hmag, 0, 'g');
        text += QString("%1  = %2 A/m\n").arg(isAxi?"Hr":"Hx").arg(cstr(u.H1));
        text += QString("%1  = %2 A/m\n").arg(isAxi?"Hz":"Hy").arg(cstr(u.H2));
        text += QString("mu_%1 = %2 (rel)\n").arg(isAxi?"r":"x").arg(cstr(u.mu1));
        text += QString("mu_%1 = %2 (rel)\n").arg(isAxi?"z":"y").arg(cstr(u.mu2));
        if (u.mu12.re != 0 || u.mu12.im != 0)
            text += QString("mu_%1= %2 (rel)\n").arg(isAxi?"rz":"xy").arg(cstr(u.mu12));
        CComplex Jtot = u.Je + u.Js;
        text += QString("J= %1 MA/m\u00b2\n").arg(cstr(Jtot));
        if (u.ff > 0)
            text += QString("Winding Fill = %1%\n").arg(u.ff*100., 0, 'f', 2);
    }

    emit statusMessage(QString("|B| = %1 T").arg(Bmag, 0, 'g', 4));
    emit outputReady(text);
}

bool ResultsWidget::event(QEvent *ev)
{
    // Intercept Tab before Qt's focus system consumes it
    if (ev->type() == QEvent::KeyPress) {
        auto *ke = static_cast<QKeyEvent*>(ev);
        if (ke->key() == Qt::Key_Tab) {
            keyPressEvent(ke);
            return true;
        }
    }
    return QWidget::event(ev);
}

void ResultsWidget::keyPressEvent(QKeyEvent *event)
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
        if (m_mode == 1 && m_doc) {
            m_doc->contour.clear();
            emit statusMessage("Contour cleared");
            update();
        }
        if (m_mode == 2 && m_doc) {
            for (auto &lbl : m_doc->labellist) lbl.IsSelected = false;
            update();
        }
        break;

    case Qt::Key_Delete:
        if (m_mode == 1 && m_doc && !m_doc->contour.empty()) {
            m_doc->contour.pop_back();
            emit statusMessage(QString("Contour: %1 points").arg(m_doc->contour.size()));
            update();
        }
        break;

    case Qt::Key_Tab:
        if (m_mode <= 1 && m_doc) {
            // Enter point by coordinates — default to current mouse position
            bool isAxi = (m_doc->ProblemType == 1);
            QDialog dlg(this);
            dlg.setWindowTitle(m_mode == 0 ? tr("Enter Point") : tr("Enter Contour Point"));
            auto *form = new QFormLayout;
            auto *xEdit = new PyLineEdit;
            auto *yEdit = new PyLineEdit;
            xEdit->setText(QString::number(m_mx, 'g', 10));
            yEdit->setText(QString::number(m_my, 'g', 10));
            xEdit->selectAll();
            form->addRow(isAxi ? tr("R:") : tr("X:"), xEdit);
            form->addRow(isAxi ? tr("Z:") : tr("Y:"), yEdit);
            auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
            QObject::connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
            QObject::connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
            auto *layout = new QVBoxLayout(&dlg);
            layout->addLayout(form);
            layout->addWidget(buttons);
            xEdit->setFocus();
            if (dlg.exec() == QDialog::Accepted) {
                double x = xEdit->evaluateAsDouble(m_mx);
                double y = yEdit->evaluateAsDouble(m_my);
                if (m_mode == 0)
                    showPointProperties(x, y);
                else {
                    m_doc->contour.push_back(CComplex(x, y));
                    emit statusMessage(QString("Contour: %1 points").arg(m_doc->contour.size()));
                    update();
                }
            }
        }
        break;

    default: QWidget::keyPressEvent(event);
    }
}

// ================================================================
// Postprocessor dialogs — delegated to MagneticsHandler
// ================================================================

void ResultsWidget::showPrefsDialog(QWidget *parent) { m_handler->showPrefsDialog(parent); }
void ResultsWidget::showIntegrateDialog(QWidget *parent, OutputDialog *out) { m_handler->showIntegrateDialog(parent, out); }
void ResultsWidget::showXYPlotDialog(QWidget *parent) { m_handler->showXYPlotDialog(parent); }
void ResultsWidget::showCircuitDialog(QWidget *parent) { m_handler->showCircuitDialog(parent); }
void ResultsWidget::showContourPlotDialog(QWidget *parent) { m_handler->showContourPlotDialog(parent); }
void ResultsWidget::showDensityPlotDialog(QWidget *parent) { m_handler->showDensityPlotDialog(parent); }
void ResultsWidget::showVectorPlotDialog(QWidget *parent) { m_handler->showVectorPlotDialog(parent); }
void ResultsWidget::showProblemInfo(QWidget *parent) { m_handler->showProblemInfo(parent); }
void ResultsWidget::showBHCurves(QWidget *parent) { m_handler->showBHCurves(parent); }

// (handler implementations are in magnetics_handler.cpp)
