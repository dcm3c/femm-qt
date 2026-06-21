// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors

// heat_document.h — Heat flow editor document
// Subclasses DocumentBase to add heat-flow-specific properties and .feh file I/O.
// Ported from femm42src/femm/hdrawView.h and hd_nosebl.h
#ifndef HEAT_DOCUMENT_H
#define HEAT_DOCUMENT_H

#include "document.h"
#include "heat_types.h"
#include <string>
#include <vector>

class HeatDocument : public DocumentBase {
public:
    HeatDocument();
    ~HeatDocument() override = default;

    int physicsType() const override { return 2; }
    std::string fileExtension() const override { return ".feh"; }
    std::string solverName() const override { return "hsolv"; }
    std::string resultsExtension() const override { return ".anh"; }

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

    // --- Heat-flow-specific problem attributes ---

    // Transient analysis
    double dT = 0.0;              // time step (0 = steady state)
    std::string PrevSoln;         // path to previous solution for transient

    // --- Property lists ---
    std::vector<HMaterialProp> blockproplist;
    std::vector<HBoundaryProp> lineproplist;
    std::vector<HPointProp> nodeproplist;
    std::vector<HCircuit> circproplist;

    // Clear document to new state
    void NewDocument();
};

#endif // HEAT_DOCUMENT_H
