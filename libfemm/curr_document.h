// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors

// curr_document.h — Current flow editor document
// Subclasses DocumentBase to add current-flow-specific properties and .fec file I/O.
// Ported from femm42src/femm/CDRAWDOC.H (current flow variant)
#ifndef CURR_DOCUMENT_H
#define CURR_DOCUMENT_H

#include "document.h"
#include "curr_types.h"
#include <string>
#include <vector>

class CurrDocument : public DocumentBase {
public:
    CurrDocument();
    ~CurrDocument() override = default;

    int physicsType() const override { return 3; }
    std::string fileExtension() const override { return ".fec"; }
    std::string solverName() const override { return "csolv"; }
    std::string resultsExtension() const override { return ".anc"; }

    int numMaterials() const override { return (int)blockproplist.size(); }
    std::string materialName(int i) const override { return blockproplist[i].BlockName; }
    int numBoundaries() const override { return (int)lineproplist.size(); }
    std::string boundaryName(int i) const override { return lineproplist[i].BdryName; }
    int numPointProps() const override { return (int)nodeproplist.size(); }
    std::string pointPropName(int i) const override { return nodeproplist[i].PointName; }
    int numCircuits() const override { return (int)circproplist.size(); }
    std::string circuitName(int i) const override { return circproplist[i].CircName; }

    bool SaveDocument(const std::string &pathname) override;
    bool OpenDocument(const std::string &pathname) override;

    // --- Current-flow-specific problem attributes ---
    double Frequency = 60.0;  // frequency in Hz (OF default: 60 Hz)

    // --- Property lists ---
    std::vector<CurrMaterialProp> blockproplist;
    std::vector<CurrBoundaryProp> lineproplist;
    std::vector<CurrPointProp> nodeproplist;
    std::vector<CurrCircuit> circproplist;

    // Clear document to new state
    void NewDocument();
};

#endif // CURR_DOCUMENT_H
