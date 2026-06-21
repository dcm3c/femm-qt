// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors

// mag_document.h — Magnetics editor document
// Subclasses DocumentBase to add magnetics-specific properties and .fem file I/O.
// Ported from femm42src/femm/FemmeDoc.h
#ifndef MAG_DOCUMENT_H
#define MAG_DOCUMENT_H

#include "document.h"
#include <string>
#include <vector>

class MagDocument : public DocumentBase {
public:
    MagDocument();
    ~MagDocument() override = default;

    int physicsType() const override { return 0; }
    std::string fileExtension() const override { return ".fem"; }
    std::string solverName() const override { return "fkn"; }
    std::string resultsExtension() const override { return ".ans"; }

    int numMaterials() const override { return (int)blockproplist.size(); }
    std::string materialName(int i) const override { return blockproplist[i].BlockName; }
    double materialCoercivity(const std::string &name) const override {
        for (auto &m : blockproplist) if (m.BlockName == name) return m.H_c;
        return 0;
    }
    int numBoundaries() const override { return (int)lineproplist.size(); }
    std::string boundaryName(int i) const override { return lineproplist[i].BdryName; }
    int numPointProps() const override { return (int)nodeproplist.size(); }
    std::string pointPropName(int i) const override { return nodeproplist[i].PointName; }
    int numCircuits() const override { return (int)circproplist.size(); }
    std::string circuitName(int i) const override { return circproplist[i].CircName; }

    bool SaveDocument(const std::string &pathname) override;
    bool OpenDocument(const std::string &pathname) override;

    // --- Magnetics-specific problem attributes ---
    double Frequency = 0.0;
    int ACSolver = 0;     // 0 = successive approx, 1 = Newton
    std::string PrevSoln;
    int PrevType = 0;     // 0=none, 1=incremental, 2=frozen

    // --- Property lists ---
    std::vector<FMaterialProp> blockproplist;
    std::vector<FBoundaryProp> lineproplist;
    std::vector<FPointProp> nodeproplist;
    std::vector<FCircuit> circproplist;

    // Clear document to new state
    void NewDocument();
};

#endif // MAG_DOCUMENT_H
