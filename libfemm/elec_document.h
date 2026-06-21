// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors

// elec_document.h — Electrostatics editor document
// Subclasses DocumentBase to add electrostatics-specific properties and .fee file I/O.
// Ported from femm42src/femm/beladrawDoc.h
#ifndef ELEC_DOCUMENT_H
#define ELEC_DOCUMENT_H

#include "document.h"
#include "elec_types.h"
#include <string>
#include <vector>

class ElecDocument : public DocumentBase {
public:
    ElecDocument();
    ~ElecDocument() override = default;

    int physicsType() const override { return 1; }
    std::string fileExtension() const override { return ".fee"; }
    std::string solverName() const override { return "belasolv"; }
    std::string resultsExtension() const override { return ".res"; }

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

    // --- Electrostatics-specific problem attributes ---

    // --- Property lists ---
    std::vector<EMaterialProp> blockproplist;
    std::vector<EBoundaryProp> lineproplist;
    std::vector<EPointProp> nodeproplist;
    std::vector<ECircuit> circproplist;

    // Clear document to new state
    void NewDocument();
};

#endif // ELEC_DOCUMENT_H
