// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors

// scalar_results_widget.h — Scalar potential postprocessor results display widget
// Renders density plots, equipotential contours, and vectors for scalar
// potential problems (electrostatics, heat flow, current flow).
// Adapted from results_widget.h (magnetics) for real-valued potentials.
#ifndef SCALAR_RESULTS_WIDGET_H
#define SCALAR_RESULTS_WIDGET_H

#include <QWidget>
#include <QColor>
#include <QSlider>
#include "scalar_doc_view.h"
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include "scalar_doc_view.h"

class ElecResultsDoc;
class HeatResultsDoc;
class CurrResultsDoc;
class OutputDialog;
class PostprocessorHandler;

class ScalarResultsWidget : public QWidget
{
    Q_OBJECT

public:
    // Electrostatics constructor (physicsType 1)
    explicit ScalarResultsWidget(ElecResultsDoc *doc, QWidget *parent = nullptr);
    // Heat flow constructor (physicsType 2)
    explicit ScalarResultsWidget(HeatResultsDoc *doc, QWidget *parent = nullptr);
    // Current flow constructor (physicsType 3)
    explicit ScalarResultsWidget(CurrResultsDoc *doc, QWidget *parent = nullptr);
    ~ScalarResultsWidget();

    ElecResultsDoc *document() const { return m_elecDoc; }
    HeatResultsDoc *heatDocument() const { return m_heatDoc; }
    CurrResultsDoc *currDocument() const { return m_currDoc; }
    void setDocument(ElecResultsDoc *doc) { m_elecDoc = doc; m_view = ScalarDocView::fromElec(doc); zoomFit(); }
    void setHeatDocument(HeatResultsDoc *doc) { m_heatDoc = doc; m_view = ScalarDocView::fromHeat(doc); zoomFit(); }
    void setCurrDocument(CurrResultsDoc *doc) { m_currDoc = doc; m_view = ScalarDocView::fromCurrentFlow(doc); zoomFit(); }
    int physicsType() const { return m_physicsType; }

    // Display options
    void setShowDensity(bool on) { m_showDensity = on; update(); }
    void setShowContours(bool on) { m_showContours = on; update(); }
    void setShowMesh(bool on) { m_showMesh = on; update(); }
    void setNumContours(int n) { m_numContours = n; update(); }
    int numContours() const { return m_numContours; }
    bool showDensity() const { return m_showDensity; }
    bool showContours() const { return m_showContours; }
    bool showMesh() const { return m_showMesh; }

    // Modes: 0=point, 1=contour, 2=area
    void setMode(int mode);
    int mode() const { return m_mode; }

    // Zoom/pan
    void zoomIn();
    void zoomOut();
    void zoomFit();
    void zoomTo(double x0, double y0, double x1, double y1);
    void zoomWindow();
    void panLeft();
    void panRight();
    void panUp();
    void panDown();

    // Grid
    void setShowGrid(bool on) { m_showGrid = on; update(); }
    bool showGrid() const { return m_showGrid; }
    void setSnapToGrid(bool on) { m_snapToGrid = on; }
    bool snapToGrid() const { return m_snapToGrid; }
    void setGridSize(double size) { m_gridSize = size; update(); }
    double gridSize() const { return m_gridSize; }

    // Postprocessor dialogs — delegated to handler
    void showPrefsDialog(QWidget *parent);
    void showIntegrateDialog(QWidget *parent, OutputDialog *out);
    void showXYPlotDialog(QWidget *parent);
    void showCircuitDialog(QWidget *parent);
    void showContourPlotDialog(QWidget *parent);
    void showDensityPlotDialog(QWidget *parent);
    void showVectorPlotDialog(QWidget *parent);
    void showProblemInfo(QWidget *parent);

    // Allow handler to emit status messages
    void emitStatus(const QString &msg) { emit statusMessage(msg); }

signals:
    void coordinatesChanged(double x, double y);
    void statusMessage(const QString &msg);
    void outputReady(const QString &text);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    bool event(QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    void showPointProperties(double xd, double yd);
    QPointF toScreen(double xd, double yd) const;
    void toDrawing(int xs, int ys, double &xd, double &yd) const;
    void snapToGrid(double &xd, double &yd) const;

    void drawDensityPlot(QPainter &p);
    void drawContours(QPainter &p);
    void drawMesh(QPainter &p);
    void drawGeometry(QPainter &p);
    void drawLegend(QPainter &p);
    void drawContourPath(QPainter &p);
    void drawSelectedBlocks(QPainter &p);
    void drawVectorPlot(QPainter &p);

    // 20-level FEMM color map
    static QColor densityColor(int level);

    // Uniform view of the underlying doc (either ElecResultsDoc or HeatResultsDoc)
public:
    const ScalarDocView& view() const { return m_view; }
private:
    ScalarDocView m_view;

    // Physics-type-specific doc pointers (only one is non-null)
    ElecResultsDoc *m_elecDoc = nullptr;
    HeatResultsDoc *m_heatDoc = nullptr;
    CurrResultsDoc *m_currDoc = nullptr;
    int m_physicsType = 1;

    PostprocessorHandler *m_handler;

    int m_mode = 0;  // 0=point, 1=contour, 2=area
    double m_mx = 0, m_my = 0;  // current mouse position in drawing coords
    double m_ox = 0, m_oy = 0, m_mag = 100;

    bool m_showDensity = true;
    bool m_showContours = true;
    bool m_showMesh = false;
    int m_numContours = 19;
public:
    bool m_showLegend = true;
    bool m_greyScale = false;
    int m_densityPlotType = 3;  // 0=off, 1=|D|, 2=|E|, 3=V, 4=energy density
    bool m_showVectors = false;
    int m_vectorType = 0;       // 0=D, 1=E
    double m_vectorPhase = 0;   // phase angle in degrees (AC current flow only)
    bool m_showMask = false;    // stress tensor mask contours
    bool m_showNames = false;   // show block label names
    bool m_showPoints = true;   // show input node squares
private:
    void drawBlockNames(QPainter &p);

    // Pan
    bool m_panning = false;
    QPoint m_panStart;
    double m_panOxStart = 0, m_panOyStart = 0;

    // Zoom window
    bool m_zoomWndFlag = false;
    QPoint m_zoomWndStart;
    bool m_zoomWndDragging = false;

    // Grid
    bool m_showGrid = false;
    bool m_snapToGrid = false;
    double m_gridSize = 1.0;

    // Phase slider and play button for AC vector plots (current flow)
    QSlider *m_phaseSlider = nullptr;
    QLabel *m_phaseLabel = nullptr;
    QPushButton *m_playBtn = nullptr;
    QTimer *m_playTimer = nullptr;
    void updatePhaseSliderVisibility();
    // Clear all area-mode selection: blocks, mesh nodes, and input geometry
    // (mesh nodes carry conductor selections, so clearing only labels would
    // leave a conductor selected). Invalidates the stress-tensor mask.
    void clearAreaSelection();
};

#endif // SCALAR_RESULTS_WIDGET_H
