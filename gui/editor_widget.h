// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors

// editor_widget.h — FEMM geometry editor widget
// Matches the original FEMM interaction model:
//   - EditAction selects the current mode (node/segment/arc/block/group)
//   - Left click creates the entity for the current mode
//   - Right click toggles selection of the nearest entity of that type
//   - Space bar opens property dialog for selected entities
//   - Middle-button drag pans the view
#ifndef EDITOR_WIDGET_H
#define EDITOR_WIDGET_H

#include <QWidget>
#include <QColor>

class DocumentBase;

class EditorWidget : public QWidget
{
    Q_OBJECT

public:
    explicit EditorWidget(DocumentBase *doc, QWidget *parent = nullptr);
    ~EditorWidget();

    DocumentBase *document() const { return m_doc; }

    // EditAction: 0=Node, 1=Segment, 2=Block Label, 3=Arc Segment, 4=Group
    void setEditAction(int action);
    int editAction() const { return m_editAction; }

    // Zoom/pan
    void zoomIn();
    void zoomOut();
    void zoomFit();
    void zoomTo(double x0, double y0, double x1, double y1);
    void viewBounds(double &x0, double &y0, double &x1, double &y1) const;
    void zoomWindow();   // enter zoom-window mode
    void selectWindow(); // enter select-window mode
    void selectCircle(); // enter select-circle mode
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

    // Display options
    void setShowNames(bool on) { m_showNames = on; update(); }
    bool showNames() const { return m_showNames; }
    void setShowMesh(bool on) { m_showMesh = on; update(); }
    bool showMesh() const { return m_showMesh; }
    bool loadMesh();  // load .node/.ele from document path (fallback)
    void loadMeshFromData(int numNodes, const double *nx, const double *ny,
                          int numTris, const int *triNodes, const int *triRegion);
    void clearMesh();
    int meshNodeCount() const { return (int)m_meshNodes.size(); }
    int meshEdgeCount() const { return (int)m_meshLines.size() + (int)m_greyMeshLines.size(); }
    int greyMeshLineCount() const { return (int)m_greyMeshLines.size(); }

    // Open properties for selected entities (space bar)
    void openSelected();

signals:
    void coordinatesChanged(double x, double y);
    void statusMessage(const QString &msg);
    void editActionChanged(int action);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    bool focusNextPrevChild(bool next) override { (void)next; return false; }
    void resizeEvent(QResizeEvent *event) override;

private:
    // Coordinate transforms
    QPointF toScreen(double xd, double yd) const;
    void toDrawing(int xs, int ys, double &xd, double &yd) const;
    void applySnap(double &x, double &y) const;

    // Drawing helpers
    void drawGrid(QPainter &p);
    void drawNodes(QPainter &p);
    void drawSegments(QPainter &p);
    void drawArcSegments(QPainter &p);
    void drawBlockLabels(QPainter &p);
    void drawMesh(QPainter &p);

    DocumentBase *m_doc;

    // View transform
    double m_ox = 0.0, m_oy = 0.0;
    double m_mag = 100.0;

    // Edit state
    int m_editAction = 0;   // 0=Node, 1=Seg, 2=Block, 3=Arc, 4=Group
    int m_firstPoint = -1;  // first node for segment/arc creation

    // Persistent arc dialog values (carry over between draws)
    double m_arcAngle = 90.0;
    double m_arcMaxSeg = 10.0;
    int m_arcBdryIdx = 0;   // 0 = "<None>"

    // Grid
    bool m_showGrid = true;
    bool m_snapToGrid = true;
    double m_gridSize = 1.0;

    // Display
    bool m_showNames = true;
    bool m_showMesh = false;

    // Mesh display (loaded from tangle output)
    struct MeshNode { double x, y; };
    struct MeshEdge { int n0, n1; };
    std::vector<MeshNode> m_meshNodes;
    std::vector<MeshEdge> m_meshLines;     // edges in defined regions (yellow)
    std::vector<MeshEdge> m_greyMeshLines; // edges in undefined regions (grey)

    // Pan state (middle-button drag)
    bool m_panning = false;
    QPoint m_panStart;
    double m_panOxStart = 0.0, m_panOyStart = 0.0;

    // Zoom window / select window / select circle mode
    int m_zoomWndFlag = 0;     // 0=inactive, 1=waiting, 2=dragging
    int m_selectWndFlag = 0;
    int m_selectCircFlag = 0;
    double m_wndX0 = 0, m_wndY0 = 0;  // start corner/center in drawing coords
    QPoint m_rubberStart;              // start in screen coords

    // Current mouse position in drawing coords
    double m_mx = 0, m_my = 0;

    // Colors (matching original FEMM)
    static constexpr QColor nodeColor() { return QColor(0, 0, 0); }
    static constexpr QColor lineColor() { return QColor(0, 0, 255); }
    static constexpr QColor blockColor() { return QColor(0, 128, 0); }
    static constexpr QColor selColor() { return QColor(255, 0, 0); }
    static constexpr QColor gridColor() { return QColor(220, 220, 220); }
    static constexpr QColor nameColor() { return QColor(255, 128, 0); }
};

#endif // EDITOR_WIDGET_H
