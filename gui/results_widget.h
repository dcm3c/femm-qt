// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors

// results_widget.h — Magnetics postprocessor results display widget
// Renders density plot (colored triangles) and equipotential contour lines.
#ifndef RESULTS_WIDGET_H
#define RESULTS_WIDGET_H

#include <QWidget>
#include <QColor>

class QSlider;
class QLabel;
class QPushButton;
class QTimer;
class ResultsDoc;
class OutputDialog;
class MagneticsHandler;

class ResultsWidget : public QWidget
{
    Q_OBJECT

public:
    explicit ResultsWidget(ResultsDoc *doc, QWidget *parent = nullptr);
    ~ResultsWidget();

    ResultsDoc *document() const { return m_doc; }
    void setDocument(ResultsDoc *doc) { m_doc = doc; zoomFit(); }

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

    // Postprocessor dialogs — delegated to MagneticsHandler
    void showPrefsDialog(QWidget *parent);
    void showIntegrateDialog(QWidget *parent, OutputDialog *out);
    void showXYPlotDialog(QWidget *parent);
    void showCircuitDialog(QWidget *parent);
    void showContourPlotDialog(QWidget *parent);
    void showDensityPlotDialog(QWidget *parent);
    void showVectorPlotDialog(QWidget *parent);
    void showProblemInfo(QWidget *parent);
    void showBHCurves(QWidget *parent);

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
    QPointF toScreen(double xd, double yd) const;
    void toDrawing(int xs, int ys, double &xd, double &yd) const;
    void snapToGrid(double &xd, double &yd) const;
    void showPointProperties(double xd, double yd);

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

    ResultsDoc *m_doc;
    MagneticsHandler *m_handler;

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
    int m_densityPlotType = 1;  // 1-based like OF: 0=off, 1+=type
    bool m_showAr = true;   // show Re(A) contours
    bool m_showAi = false;  // show Im(A) contours (AC only)
    bool m_showMask = false; // show stress tensor mask contours
    bool m_showVectors = false;
    int m_vectorType = 0;     // 0=B, 1=H
    double m_vectorPhase = 0; // phase angle in degrees (AC only)
    bool m_showNames = false; // show block label names
    bool m_showPoints = true; // show input node squares
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

    // Phase slider and play button for AC vector plots
    QSlider *m_phaseSlider = nullptr;
    QLabel *m_phaseLabel = nullptr;
    QPushButton *m_playBtn = nullptr;
    QTimer *m_playTimer = nullptr;
    void updatePhaseSliderVisibility();
};

#endif // RESULTS_WIDGET_H
