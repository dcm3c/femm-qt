// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors

// bh_dialog.h — B-H curve data editor for nonlinear magnetic materials
#ifndef BH_DIALOG_H
#define BH_DIALOG_H

#include <QDialog>
#include <QTextEdit>
#include <vector>

class QPushButton;

struct FMaterialProp;

class BHDataDialog : public QDialog {
    Q_OBJECT
public:
    BHDataDialog(QWidget *parent = nullptr);
    void setMaterialName(const QString &name);
    void setData(const std::vector<double> &B, const std::vector<double> &H, int npts);
    void getData(std::vector<double> &B, std::vector<double> &H, int &npts) const;

private slots:
    void onImport();
    void onPlotBH(bool logScale = false);

private:
    QTextEdit *m_bEdit;
    QTextEdit *m_hEdit;
    QString m_matName;
};

#endif // BH_DIALOG_H
