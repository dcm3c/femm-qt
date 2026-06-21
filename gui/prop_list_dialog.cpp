// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors

// prop_list_dialog.cpp — Generic property list dialog (Add/Delete/Modify)
// Opens the appropriate editor dialog based on property type and physics type.

#include "prop_list_dialog.h"
#include "prop_editors.h"
#include "elec_prop_editors.h"
#include "heat_prop_editors.h"
#include "curr_prop_editors.h"
#include "document.h"
#include "mag_document.h"
#include "elec_document.h"
#include "heat_document.h"
#include "curr_document.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QDialogButtonBox>
#include <QMessageBox>

PropertyListDialog::PropertyListDialog(DocumentBase *doc, int propType, QWidget *parent)
    : QDialog(parent), m_doc(doc), m_propType(propType)
{
    static const char *titles[] = {
        "Point Properties", "Boundary Conditions", "Materials", "Circuits"
    };
    // Electrostatics calls circuits "Conductors"
    const char *circLabel = (doc->physicsType() == 0) ? "Circuits" : "Conductors";
    setWindowTitle(tr(propType == 3 ? circLabel : titles[propType]));
    resize(300, 350);

    m_list = new QListWidget;
    populateList();

    auto *addBtn = new QPushButton(tr("Add"));
    auto *delBtn = new QPushButton(tr("Delete"));
    auto *modBtn = new QPushButton(tr("Modify"));
    connect(addBtn, &QPushButton::clicked, this, &PropertyListDialog::onAdd);
    connect(delBtn, &QPushButton::clicked, this, &PropertyListDialog::onDelete);
    connect(modBtn, &QPushButton::clicked, this, &PropertyListDialog::onModify);
    connect(m_list, &QListWidget::itemDoubleClicked, this, &PropertyListDialog::onModify);

    auto *btnLayout = new QVBoxLayout;
    btnLayout->addWidget(addBtn);
    btnLayout->addWidget(delBtn);
    btnLayout->addWidget(modBtn);
    btnLayout->addStretch();

    auto *closeBtn = new QDialogButtonBox(QDialogButtonBox::Close);
    connect(closeBtn, &QDialogButtonBox::rejected, this, &QDialog::accept);

    auto *topLayout = new QHBoxLayout;
    topLayout->addWidget(m_list);
    topLayout->addLayout(btnLayout);

    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->addLayout(topLayout);
    mainLayout->addWidget(closeBtn);
}

void PropertyListDialog::populateList()
{
    m_list->clear();
    switch (m_propType) {
    case 0:
        for (int i = 0; i < m_doc->numPointProps(); i++)
            m_list->addItem(QString::fromStdString(m_doc->pointPropName(i)));
        break;
    case 1:
        for (int i = 0; i < m_doc->numBoundaries(); i++)
            m_list->addItem(QString::fromStdString(m_doc->boundaryName(i)));
        break;
    case 2:
        for (int i = 0; i < m_doc->numMaterials(); i++)
            m_list->addItem(QString::fromStdString(m_doc->materialName(i)));
        break;
    case 3:
        for (int i = 0; i < m_doc->numCircuits(); i++)
            m_list->addItem(QString::fromStdString(m_doc->circuitName(i)));
        break;
    }
}

void PropertyListDialog::onAdd()
{
    if (openEditor(-1))
        populateList();
}

void PropertyListDialog::onDelete()
{
    int row = m_list->currentRow();
    if (row < 0) return;

    // Magnetics
    if (auto *mag = dynamic_cast<MagDocument*>(m_doc)) {
        switch (m_propType) {
        case 0: if (row < (int)mag->nodeproplist.size()) mag->nodeproplist.erase(mag->nodeproplist.begin() + row); break;
        case 1: if (row < (int)mag->lineproplist.size()) mag->lineproplist.erase(mag->lineproplist.begin() + row); break;
        case 2: if (row < (int)mag->blockproplist.size()) mag->blockproplist.erase(mag->blockproplist.begin() + row); break;
        case 3: if (row < (int)mag->circproplist.size()) mag->circproplist.erase(mag->circproplist.begin() + row); break;
        }
    }
    // Electrostatics
    else if (auto *elec = dynamic_cast<ElecDocument*>(m_doc)) {
        switch (m_propType) {
        case 0: if (row < (int)elec->nodeproplist.size()) elec->nodeproplist.erase(elec->nodeproplist.begin() + row); break;
        case 1: if (row < (int)elec->lineproplist.size()) elec->lineproplist.erase(elec->lineproplist.begin() + row); break;
        case 2: if (row < (int)elec->blockproplist.size()) elec->blockproplist.erase(elec->blockproplist.begin() + row); break;
        case 3: if (row < (int)elec->circproplist.size()) elec->circproplist.erase(elec->circproplist.begin() + row); break;
        }
    }
    // Heat flow
    else if (auto *heat = dynamic_cast<HeatDocument*>(m_doc)) {
        switch (m_propType) {
        case 0: if (row < (int)heat->nodeproplist.size()) heat->nodeproplist.erase(heat->nodeproplist.begin() + row); break;
        case 1: if (row < (int)heat->lineproplist.size()) heat->lineproplist.erase(heat->lineproplist.begin() + row); break;
        case 2: if (row < (int)heat->blockproplist.size()) heat->blockproplist.erase(heat->blockproplist.begin() + row); break;
        case 3: if (row < (int)heat->circproplist.size()) heat->circproplist.erase(heat->circproplist.begin() + row); break;
        }
    }
    // Current flow
    else if (auto *cf = dynamic_cast<CurrDocument*>(m_doc)) {
        switch (m_propType) {
        case 0: if (row < (int)cf->nodeproplist.size()) cf->nodeproplist.erase(cf->nodeproplist.begin() + row); break;
        case 1: if (row < (int)cf->lineproplist.size()) cf->lineproplist.erase(cf->lineproplist.begin() + row); break;
        case 2: if (row < (int)cf->blockproplist.size()) cf->blockproplist.erase(cf->blockproplist.begin() + row); break;
        case 3: if (row < (int)cf->circproplist.size()) cf->circproplist.erase(cf->circproplist.begin() + row); break;
        }
    }
    populateList();
}

void PropertyListDialog::onModify()
{
    int row = m_list->currentRow();
    if (row < 0) return;
    if (openEditor(row))
        populateList();
}

bool PropertyListDialog::openEditor(int index)
{
    // Magnetics property editors
    if (auto *mag = dynamic_cast<MagDocument*>(m_doc)) {
        switch (m_propType) {
        case 0: { // Point properties
            PointPropEditorDialog dlg(this);
            if (index >= 0 && index < (int)mag->nodeproplist.size())
                dlg.fromProp(mag->nodeproplist[index]);
            if (dlg.exec() != QDialog::Accepted) return false;
            FPointProp p;
            dlg.toProp(p);
            if (index < 0) mag->nodeproplist.push_back(p);
            else mag->nodeproplist[index] = p;
            return true;
        }
        case 1: { // Boundaries
            BoundaryEditorDialog dlg(this);
            if (index >= 0 && index < (int)mag->lineproplist.size())
                dlg.fromProp(mag->lineproplist[index]);
            if (dlg.exec() != QDialog::Accepted) return false;
            FBoundaryProp b;
            dlg.toProp(b);
            if (index < 0) mag->lineproplist.push_back(b);
            else mag->lineproplist[index] = b;
            return true;
        }
        case 2: { // Materials
            MaterialEditorDialog dlg(this);
            if (index >= 0 && index < (int)mag->blockproplist.size())
                dlg.fromProp(mag->blockproplist[index]);
            if (dlg.exec() != QDialog::Accepted) return false;
            FMaterialProp m;
            if (index >= 0) m = mag->blockproplist[index]; // preserve B-H data
            dlg.toProp(m);
            if (index < 0) mag->blockproplist.push_back(m);
            else mag->blockproplist[index] = m;
            return true;
        }
        case 3: { // Circuits
            CircuitEditorDialog dlg(this);
            if (index >= 0 && index < (int)mag->circproplist.size())
                dlg.fromProp(mag->circproplist[index]);
            if (dlg.exec() != QDialog::Accepted) return false;
            FCircuit c;
            dlg.toProp(c);
            if (index < 0) mag->circproplist.push_back(c);
            else mag->circproplist[index] = c;
            return true;
        }
        }
    }
    // Electrostatics property editors — use simplified dialogs
    // For now, reuse magnetics dialogs where field names overlap,
    // and use simple input dialogs for electrostatics-specific types.
    else if (auto *elec = dynamic_cast<ElecDocument*>(m_doc)) {
        switch (m_propType) {
        case 0: { // Point properties
            ElecPointPropEditorDialog dlg(this);
            if (index >= 0 && index < (int)elec->nodeproplist.size())
                dlg.fromProp(elec->nodeproplist[index]);
            if (dlg.exec() != QDialog::Accepted) return false;
            EPointProp p;
            dlg.toProp(p);
            if (index < 0) elec->nodeproplist.push_back(p);
            else elec->nodeproplist[index] = p;
            return true;
        }
        case 1: { // Boundaries
            ElecBoundaryEditorDialog dlg(this);
            if (index >= 0 && index < (int)elec->lineproplist.size())
                dlg.fromProp(elec->lineproplist[index]);
            if (dlg.exec() != QDialog::Accepted) return false;
            EBoundaryProp b;
            dlg.toProp(b);
            if (index < 0) elec->lineproplist.push_back(b);
            else elec->lineproplist[index] = b;
            return true;
        }
        case 2: { // Materials
            ElecMaterialEditorDialog dlg(this);
            if (index >= 0 && index < (int)elec->blockproplist.size())
                dlg.fromProp(elec->blockproplist[index]);
            if (dlg.exec() != QDialog::Accepted) return false;
            EMaterialProp m;
            dlg.toProp(m);
            if (index < 0) elec->blockproplist.push_back(m);
            else elec->blockproplist[index] = m;
            return true;
        }
        case 3: { // Conductors
            ElecConductorEditorDialog dlg(this);
            if (index >= 0 && index < (int)elec->circproplist.size())
                dlg.fromProp(elec->circproplist[index]);
            if (dlg.exec() != QDialog::Accepted) return false;
            ECircuit c;
            dlg.toProp(c);
            if (index < 0) elec->circproplist.push_back(c);
            else elec->circproplist[index] = c;
            return true;
        }
        }
    }
    // Heat flow property editors
    else if (auto *heat = dynamic_cast<HeatDocument*>(m_doc)) {
        switch (m_propType) {
        case 0: { // Point properties
            HeatPointPropEditorDialog dlg(this);
            if (index >= 0 && index < (int)heat->nodeproplist.size())
                dlg.fromProp(heat->nodeproplist[index]);
            if (dlg.exec() != QDialog::Accepted) return false;
            HPointProp p;
            dlg.toProp(p);
            if (index < 0) heat->nodeproplist.push_back(p);
            else heat->nodeproplist[index] = p;
            return true;
        }
        case 1: { // Boundaries
            HeatBoundaryEditorDialog dlg(this);
            if (index >= 0 && index < (int)heat->lineproplist.size())
                dlg.fromProp(heat->lineproplist[index]);
            if (dlg.exec() != QDialog::Accepted) return false;
            HBoundaryProp b;
            dlg.toProp(b);
            if (index < 0) heat->lineproplist.push_back(b);
            else heat->lineproplist[index] = b;
            return true;
        }
        case 2: { // Materials
            HeatMaterialEditorDialog dlg(this);
            if (index >= 0 && index < (int)heat->blockproplist.size())
                dlg.fromProp(heat->blockproplist[index]);
            if (dlg.exec() != QDialog::Accepted) return false;
            HMaterialProp m;
            if (index >= 0) m = heat->blockproplist[index]; // preserve T-K data
            dlg.toProp(m);
            if (index < 0) heat->blockproplist.push_back(m);
            else heat->blockproplist[index] = m;
            return true;
        }
        case 3: { // Conductors
            HeatConductorEditorDialog dlg(this);
            if (index >= 0 && index < (int)heat->circproplist.size())
                dlg.fromProp(heat->circproplist[index]);
            if (dlg.exec() != QDialog::Accepted) return false;
            HCircuit c;
            dlg.toProp(c);
            if (index < 0) heat->circproplist.push_back(c);
            else heat->circproplist[index] = c;
            return true;
        }
        }
    }
    // Current flow property editors
    else if (auto *cf = dynamic_cast<CurrDocument*>(m_doc)) {
        switch (m_propType) {
        case 0: { // Point properties
            CurrPointPropEditorDialog dlg(this);
            if (index >= 0 && index < (int)cf->nodeproplist.size())
                dlg.fromProp(cf->nodeproplist[index]);
            if (dlg.exec() != QDialog::Accepted) return false;
            CurrPointProp p;
            dlg.toProp(p);
            if (index < 0) cf->nodeproplist.push_back(p);
            else cf->nodeproplist[index] = p;
            return true;
        }
        case 1: { // Boundaries
            CurrBoundaryEditorDialog dlg(this);
            if (index >= 0 && index < (int)cf->lineproplist.size())
                dlg.fromProp(cf->lineproplist[index]);
            if (dlg.exec() != QDialog::Accepted) return false;
            CurrBoundaryProp b;
            dlg.toProp(b);
            if (index < 0) cf->lineproplist.push_back(b);
            else cf->lineproplist[index] = b;
            return true;
        }
        case 2: { // Materials
            CurrMaterialEditorDialog dlg(this);
            if (index >= 0 && index < (int)cf->blockproplist.size())
                dlg.fromProp(cf->blockproplist[index]);
            if (dlg.exec() != QDialog::Accepted) return false;
            CurrMaterialProp m;
            dlg.toProp(m);
            if (index < 0) cf->blockproplist.push_back(m);
            else cf->blockproplist[index] = m;
            return true;
        }
        case 3: { // Conductors
            CurrConductorEditorDialog dlg(this);
            if (index >= 0 && index < (int)cf->circproplist.size())
                dlg.fromProp(cf->circproplist[index]);
            if (dlg.exec() != QDialog::Accepted) return false;
            CurrCircuit c;
            dlg.toProp(c);
            if (index < 0) cf->circproplist.push_back(c);
            else cf->circproplist[index] = c;
            return true;
        }
        }
    }
    return false;
}
