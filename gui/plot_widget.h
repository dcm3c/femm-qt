// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors

// plot_widget.h — Reusable XY line plot widget and dialog
// Used for BH curve plots and postprocessor line plots (mo_makeplot).
// Ported from femm42src/femm/Xyplot.cpp and bhplot.cpp.
#ifndef PLOT_WIDGET_H
#define PLOT_WIDGET_H

#include <QWidget>
#include <QDialog>
#include <QString>
#include <vector>

// A single data series (line) on the plot
struct PlotSeries {
    std::vector<double> x, y;
    QString label;
};

// Discrete data points drawn as small squares (for BH raw data overlay)
struct PlotMarkers {
    std::vector<double> x, y;
};

// Reusable XY plot widget — renders with QPainter
class PlotWidget : public QWidget {
    Q_OBJECT
public:
    explicit PlotWidget(QWidget *parent = nullptr);

    void clearAll();
    void addSeries(const PlotSeries &s);
    void setMarkers(const PlotMarkers &m);
    void setAxisLabels(const QString &xLabel, const QString &yLabel);
    void setLogX(bool on);

    // Export data to file
    bool exportToFile(const QString &path, int format = 0) const;

signals:
    void cursorInfo(const QString &text);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;
    QSize sizeHint() const override { return QSize(600, 400); }

private:
    // Interpolate series value at given x
    double interpolateY(const PlotSeries &s, double xVal) const;

    std::vector<PlotSeries> m_series;
    PlotMarkers m_markers;
    QString m_xLabel, m_yLabel;
    bool m_logX = false;
    bool m_hasCursor = false;
    double m_cursorX = 0;  // data x-coordinate of cursor

    // Cached axis parameters (set during paintEvent, used by mouseMoveEvent)
    double m_axXMin = 0, m_axXMax = 1;
    double m_axYMin = 0, m_axYMax = 1;
    double m_axMarginLeft = 80, m_axPlotW = 1, m_axPlotH = 1;
    double m_axMarginTop = 20;
};

// Dialog wrapper for PlotWidget — modeless so it doesn't block
class QLabel;
class PlotDialog : public QDialog {
    Q_OBJECT
public:
    PlotDialog(const QString &title, QWidget *parent = nullptr);
    PlotWidget *plot() { return m_plot; }

private:
    PlotWidget *m_plot;
    QLabel *m_cursorLabel;
};

#endif // PLOT_WIDGET_H
