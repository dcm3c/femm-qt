// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors

// plot_widget.cpp — Reusable XY line plot widget
// Ported from femm42src/femm/Xyplot.cpp and bhplot.cpp
// Renders using QPainter instead of Windows GDI CDC.

#include "plot_widget.h"

#include <QPainter>
#include <QPen>
#include <QFontMetrics>
#include <QVBoxLayout>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QLabel>
#include <QFileDialog>
#include <QMouseEvent>
#include <cmath>
#include <cstdio>
#include <algorithm>

// Series colors matching original FEMM
static const QColor seriesColors[] = {
    QColor(0, 0, 0),       // black
    QColor(0, 0, 255),     // blue
    QColor(0, 128, 0),     // green
    QColor(255, 0, 0),     // red
    QColor(255, 255, 0),   // yellow
    QColor(255, 0, 255),   // magenta
    QColor(0, 255, 255),   // cyan
};
static const int numSeriesColors = sizeof(seriesColors) / sizeof(seriesColors[0]);

// ================================================================
// PlotWidget
// ================================================================

PlotWidget::PlotWidget(QWidget *parent) : QWidget(parent)
{
    setMinimumSize(400, 300);
    setBackgroundRole(QPalette::Base);
    setAutoFillBackground(true);
    setMouseTracking(true);
}

void PlotWidget::clearAll()
{
    m_series.clear();
    m_markers = PlotMarkers();
    m_xLabel.clear();
    m_yLabel.clear();
    m_logX = false;
    update();
}

void PlotWidget::addSeries(const PlotSeries &s) { m_series.push_back(s); update(); }
void PlotWidget::setMarkers(const PlotMarkers &m) { m_markers = m; update(); }
void PlotWidget::setAxisLabels(const QString &xLabel, const QString &yLabel) { m_xLabel = xLabel; m_yLabel = yLabel; update(); }
void PlotWidget::setLogX(bool on) { m_logX = on; update(); }

void PlotWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    if (m_series.empty()) {
        p.drawText(rect(), Qt::AlignCenter, "No data");
        return;
    }

    // Margins
    QFontMetrics fm(p.font());
    const double marginLeft = 80;
    const double marginRight = 20;
    const double marginTop = 20;
    const double marginBottom = 50;
    const double legendWidth = 0; // legend drawn inside plot area

    const double plotW = width() - marginLeft - marginRight - legendWidth;
    const double plotH = height() - marginTop - marginBottom;
    if (plotW < 10 || plotH < 10) return;

    // Find data ranges
    double xMin = 1e30, xMax = -1e30, yMin = 1e30, yMax = -1e30;
    for (auto &s : m_series) {
        for (size_t i = 0; i < s.x.size(); i++) {
            double xv = m_logX ? (s.x[i] > 0 ? log10(s.x[i]) : -30) : s.x[i];
            if (xv < xMin) xMin = xv;
            if (xv > xMax) xMax = xv;
            if (s.y[i] < yMin) yMin = s.y[i];
            if (s.y[i] > yMax) yMax = s.y[i];
        }
    }

    if (xMax <= xMin) { xMin -= 1; xMax += 1; }
    if (yMax <= yMin) { yMin -= 1; yMax += 1; }

    // Nice axis scaling (matching original FEMM algorithm)
    double dy = pow(10.0, floor(log10(yMax - yMin)));
    yMin = dy * floor(yMin / dy);
    yMax = dy * ceil(yMax / dy);

    double dx;
    if (m_logX) {
        xMin = floor(xMin);
        xMax = ceil(xMax);
        dx = 1.0;
    } else {
        dx = pow(10.0, floor(log10(std::max(xMax - xMin, 1e-30))));
    }

    // Transform: data → pixel
    auto toPixX = [&](double x) -> double {
        return marginLeft + (x - xMin) * plotW / (xMax - xMin);
    };
    auto toPixY = [&](double y) -> double {
        return marginTop + (yMax - y) * plotH / (yMax - yMin);
    };

    // Cache axis params for mouse tracking
    m_axXMin = xMin; m_axXMax = xMax;
    m_axYMin = yMin; m_axYMax = yMax;
    m_axMarginLeft = marginLeft; m_axPlotW = plotW;
    m_axMarginTop = marginTop; m_axPlotH = plotH;

    // Compute tick counts (needed for grid and labels)
    int nTicksY = (int)((yMax - yMin) / dy + 0.5);
    if (nTicksY < 4) { dy /= 2.0; nTicksY = (int)((yMax - yMin) / dy + 0.5); }
    if (nTicksY == 0) nTicksY = 1;
    int nTicksX = (int)((xMax - xMin) / dx);
    if (nTicksX < 4 && !m_logX) { dx /= 2.0; nTicksX = (int)((xMax - xMin) / dx); }
    if (nTicksX == 0) nTicksX = 1;

    // Grid lines (light gray, behind everything)
    p.setPen(QPen(QColor(220, 220, 220), 1));
    for (int i = 0; i <= nTicksY; i++) {
        double py = toPixY(yMin + i * dy);
        p.drawLine(QPointF(marginLeft, py), QPointF(marginLeft + plotW, py));
    }
    for (int i = 0; i <= nTicksX; i++) {
        double px = toPixX(xMin + i * dx);
        p.drawLine(QPointF(px, marginTop), QPointF(px, marginTop + plotH));
    }

    // Draw plot area border
    p.setPen(QPen(Qt::black, 1));
    p.drawRect(QRectF(marginLeft, marginTop, plotW, plotH));

    // Y-axis ticks and labels
    p.setPen(QPen(Qt::black, 1));
    for (int i = 0; i <= nTicksY; i++) {
        double yv = yMin + i * dy;
        double py = toPixY(yv);
        p.drawLine(QPointF(marginLeft - 5, py), QPointF(marginLeft, py));
        QString label = QString::number(yv, 'g', 4);
        QRectF labelRect(0, py - fm.height() / 2, marginLeft - 8, fm.height());
        p.drawText(labelRect, Qt::AlignRight | Qt::AlignVCenter, label);
    }

    // X-axis ticks and labels
    for (int i = 0; i <= nTicksX; i++) {
        double xv = xMin + i * dx;
        double px = toPixX(xv);
        p.drawLine(QPointF(px, marginTop + plotH), QPointF(px, marginTop + plotH + 5));
        QString label;
        if (m_logX)
            label = QString::number(pow(10.0, xv), 'g', 4);
        else
            label = QString::number(xv, 'g', 4);
        QRectF labelRect(px - 40, marginTop + plotH + 7, 80, fm.height());
        p.drawText(labelRect, Qt::AlignCenter, label);
    }

    // Axis labels
    if (!m_xLabel.isEmpty()) {
        QRectF xLabelRect(marginLeft, height() - fm.height() - 4, plotW, fm.height());
        p.drawText(xLabelRect, Qt::AlignCenter, m_xLabel);
    }
    if (!m_yLabel.isEmpty()) {
        p.save();
        p.translate(14, marginTop + plotH / 2);
        p.rotate(-90);
        p.drawText(QRectF(-plotH / 2, 0, plotH, fm.height()), Qt::AlignCenter, m_yLabel);
        p.restore();
    }

    // Clip to plot area
    p.setClipRect(QRectF(marginLeft, marginTop, plotW, plotH));

    // Draw markers (raw data points as small squares, like original FEMM)
    if (!m_markers.x.empty()) {
        p.setPen(QPen(QColor(0, 0, 255), 1));
        p.setBrush(Qt::NoBrush);
        for (size_t i = 0; i < m_markers.x.size(); i++) {
            double xv = m_logX ? (m_markers.x[i] > 0 ? log10(m_markers.x[i]) : -30) : m_markers.x[i];
            double px = toPixX(xv);
            double py = toPixY(m_markers.y[i]);
            p.drawRect(QRectF(px - 3, py - 3, 6, 6));
        }
    }

    // Draw series lines
    for (size_t si = 0; si < m_series.size(); si++) {
        auto &s = m_series[si];
        if (s.x.size() < 2) continue;
        QColor col = seriesColors[si % numSeriesColors];
        p.setPen(QPen(col, 2));
        p.setBrush(Qt::NoBrush);

        QVector<QPointF> pts;
        pts.reserve((int)s.x.size());
        for (size_t i = 0; i < s.x.size(); i++) {
            double xv = m_logX ? (s.x[i] > 0 ? log10(s.x[i]) : -30) : s.x[i];
            pts.append(QPointF(toPixX(xv), toPixY(s.y[i])));
        }
        p.drawPolyline(pts.data(), pts.size());
    }

    p.setClipping(false);

    // Legend (top-right corner inside plot)
    if (m_series.size() > 1) {
        int lx = (int)(marginLeft + plotW - 10);
        int ly = (int)(marginTop + 10);
        for (size_t si = 0; si < m_series.size(); si++) {
            QColor col = seriesColors[si % numSeriesColors];
            p.setPen(QPen(col, 2));
            int y = ly + (int)si * (fm.height() + 2);
            p.drawLine(lx - 30, y + fm.height() / 2, lx - 5, y + fm.height() / 2);
            p.setPen(Qt::black);
            p.drawText(QRectF(lx - 150, y, 115, fm.height()), Qt::AlignRight, m_series[si].label);
        }
    }

    // Draw cursor tracking dots on each series
    if (m_hasCursor && !m_series.empty()) {
        p.setClipRect(QRectF(marginLeft, marginTop, plotW, plotH));
        for (size_t si = 0; si < m_series.size(); si++) {
            double yVal = interpolateY(m_series[si], m_cursorX);
            double px = toPixX(m_logX && m_cursorX > 0 ? log10(m_cursorX) : m_cursorX);
            double py = toPixY(yVal);
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(255, 0, 0));
            p.drawEllipse(QPointF(px, py), 4, 4);
        }
        p.setClipping(false);
    }
}

double PlotWidget::interpolateY(const PlotSeries &s, double xVal) const
{
    if (s.x.empty()) return 0;
    if (xVal <= s.x.front()) return s.y.front();
    if (xVal >= s.x.back()) return s.y.back();
    // Binary search for the interval
    auto it = std::lower_bound(s.x.begin(), s.x.end(), xVal);
    int idx = (int)(it - s.x.begin());
    if (idx == 0) return s.y[0];
    double x0 = s.x[idx - 1], x1 = s.x[idx];
    double y0 = s.y[idx - 1], y1 = s.y[idx];
    double t = (xVal - x0) / (x1 - x0 + 1e-30);
    return y0 + t * (y1 - y0);
}

void PlotWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (m_series.empty() || m_axPlotW < 1) return;

    double px = event->pos().x();
    // Convert pixel to data x
    double dataX = m_axXMin + (px - m_axMarginLeft) * (m_axXMax - m_axXMin) / m_axPlotW;
    if (m_logX) dataX = pow(10.0, dataX);

    // Clamp to data range
    double xLo = m_series[0].x.empty() ? 0 : m_series[0].x.front();
    double xHi = m_series[0].x.empty() ? 0 : m_series[0].x.back();
    if (dataX < xLo) dataX = xLo;
    if (dataX > xHi) dataX = xHi;

    m_cursorX = dataX;
    m_hasCursor = true;

    // Build info string
    QString info = QString("x = %1").arg(dataX, 0, 'g', 6);
    for (size_t si = 0; si < m_series.size(); si++) {
        double yVal = interpolateY(m_series[si], dataX);
        info += QString("   %1 = %2").arg(m_series[si].label).arg(yVal, 0, 'g', 6);
    }
    emit cursorInfo(info);
    update();
}

void PlotWidget::leaveEvent(QEvent *)
{
    m_hasCursor = false;
    emit cursorInfo(QString());
    update();
}

bool PlotWidget::exportToFile(const QString &path, int format) const
{
    FILE *fp = fopen(path.toStdString().c_str(), "wt");
    if (!fp) return false;

    if (m_series.empty()) { fclose(fp); return false; }

    // Use first series for x values
    auto &s0 = m_series[0];
    // Header row with column names
    fprintf(fp, "%s", m_xLabel.toStdString().c_str());
    for (size_t si = 0; si < m_series.size(); si++)
        fprintf(fp, ", %s", m_series[si].label.toStdString().c_str());
    fprintf(fp, "\n");
    for (size_t i = 0; i < s0.x.size(); i++) {
        fprintf(fp, "%.8e", s0.x[i]);
        for (size_t si = 0; si < m_series.size(); si++) {
            if (i < m_series[si].y.size())
                fprintf(fp, ", %.8e", m_series[si].y[i]);
        }
        fprintf(fp, "\n");
    }
    fclose(fp);
    return true;
}

// ================================================================
// PlotDialog
// ================================================================

PlotDialog::PlotDialog(const QString &title, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(title);
    resize(640, 440);

    m_plot = new PlotWidget;

    auto *exportBtn = new QPushButton(tr("Export..."));
    connect(exportBtn, &QPushButton::clicked, [this]() {
        QString path = QFileDialog::getSaveFileName(this, tr("Export Plot Data"),
            QString(), tr("CSV Files (*.csv);;All Files (*)"));
        if (!path.isEmpty())
            m_plot->exportToFile(path, 0);
    });

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    buttons->addButton(exportBtn, QDialogButtonBox::ActionRole);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close);

    m_cursorLabel = new QLabel;
    m_cursorLabel->setFont(QFont("Consolas", 8));
    m_cursorLabel->setMinimumHeight(16);

    connect(m_plot, &PlotWidget::cursorInfo, [this](const QString &text) {
        m_cursorLabel->setText(text);
    });

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(m_plot, 1);
    layout->addWidget(m_cursorLabel);
    layout->addWidget(buttons);
}
