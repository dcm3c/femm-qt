// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors

// document.h — Base document class with shared geometry operations
// Provides geometry storage (nodes, segments, arcs, block labels)
// and common operations (add, delete, select, transform).
// Physics-specific subclasses add material/boundary properties and file I/O.
#ifndef DOCUMENT_H
#define DOCUMENT_H

#include "mag_types.h"
#include <string>
#include <vector>
#include <deque>
#include <cmath>
#include <cstring>

// MSVC _strnicmp compatibility for POSIX
#ifndef _WIN32
#include <strings.h>
#define _strnicmp strncasecmp
#endif

#ifndef PI
#define PI 3.141592653589793238462643383279
#endif

#define CLOSE_ENOUGH 1.e-06

class DocumentBase {
public:
    DocumentBase() = default;
    virtual ~DocumentBase() = default;

    // Physics type: 0=magnetics, 1=electrostatics, 2=heat, 3=current
    virtual int physicsType() const { return -1; }

    // File extension for this document type (e.g. ".fem", ".fee")
    virtual std::string fileExtension() const { return ""; }

    // Solver executable name (e.g. "fkn", "belasolv")
    virtual std::string solverName() const { return ""; }

    // Results file extension (e.g. ".ans", ".res")
    virtual std::string resultsExtension() const { return ""; }

    // Property name accessors for GUI combo boxes
    virtual int numMaterials() const { return 0; }
    virtual std::string materialName(int i) const { (void)i; return ""; }
    virtual double materialCoercivity(const std::string &name) const { (void)name; return 0; }
    virtual int numBoundaries() const { return 0; }
    virtual std::string boundaryName(int i) const { (void)i; return ""; }
    virtual int numPointProps() const { return 0; }
    virtual std::string pointPropName(int i) const { (void)i; return ""; }
    virtual int numCircuits() const { return 0; }
    virtual std::string circuitName(int i) const { (void)i; return ""; }

    // Save/open (type-specific, called generically)
    virtual bool SaveDocument(const std::string &path) { (void)path; return false; }
    virtual bool OpenDocument(const std::string &path) { (void)path; return false; }

    // --- Problem definition (shared across physics types) ---
    int ProblemType = 0;     // 0 = planar, 1 = axisymmetric
    int Coords = 0;          // 0 = cartesian, 1 = polar
    int LengthUnits = 1;     // 0=in, 1=mm, 2=cm, 3=m, 4=mils, 5=um
    double Depth = 1.0;      // depth into page (length units)
    double Precision = 1.e-8;
    double MinAngle = 30.0;
    int SmartMesh = 1;       // 0 = off, 1 = on
    double extRo = 0.0, extRi = 0.0, extZo = 0.0;  // exterior region (axisymmetric)
    std::string ProblemNote;
    std::string filePath;    // current file path

    // --- Geometry ---
    std::vector<FNode> nodelist;
    std::vector<FSegment> linelist;
    std::vector<FArcSegment> arclist;
    std::vector<FBlockLabel> blocklist;

    // --- Geometry operations (PSLG-aware) ---
    // These detect and resolve intersections, split segments/arcs, etc.
    bool AddNode(double x, double y, double d);
    bool AddSegment(int n0, int n1, FSegment *parsegm = nullptr, double tol = 0);
    bool AddArcSegment(FArcSegment &asegm, double tol = 0);
    bool AddBlockLabel(double x, double y, double d);

    // PSLG-aware overloads used by EnforcePSLG (re-add by coordinates)
    bool AddNode(FNode &node, double d);
    bool AddSegment(CComplex p0, CComplex p1, FSegment &segm, double tol);
    bool AddArcSegment(CComplex p0, CComplex p1, FArcSegment &asegm, double tol);
    bool AddBlockLabel(FBlockLabel &blabel, double d);

    int ClosestNode(double x, double y) const;
    int ClosestSegment(double x, double y) const;
    int ClosestArcSegment(double x, double y) const;
    int ClosestBlockLabel(double x, double y) const;

    double ShortestDistance(double p, double q, int segm) const;
    double ShortestDistanceFromArc(CComplex p, const FArcSegment &arc) const;
    void GetCircle(const FArcSegment &asegm, CComplex &c, double &R) const;

    // Intersection detection
    bool GetIntersection(int n0, int n1, int segm, double *xi, double *yi) const;
    int GetLineArcIntersection(FSegment &seg, FArcSegment &arc, CComplex *p) const;
    int GetArcArcIntersection(FArcSegment &arc0, FArcSegment &arc1, CComplex *p) const;

    // PSLG enforcement — rebuilds geometry to resolve all intersections
    void EnforcePSLG(double tol = 0);

    void UnselectAll();

    // Fillet: replace a corner node with a tangent arc of given radius
    bool CanCreateRadius(int n) const;
    bool CreateRadius(int n, double r);
    bool DeleteSelectedNodes();
    bool DeleteSelectedSegments();
    bool DeleteSelectedArcSegments();
    bool DeleteSelectedBlockLabels();

    // Transforms (EditAction: 0=nodes, 1=segs, 2=blocks, 3=arcs, 4=group)
    void markEndpointsOfSelectedSegments(int EditAction);
    void TranslateMove(double dx, double dy, int EditAction);
    void RotateMove(CComplex c, double t, int EditAction);
    void ScaleMove(double bx, double by, double sf, int EditAction);
    void MirrorSelected(double x0, double y0, double x1, double y1, int ea);
    void TranslateCopy(double dx, double dy, int ncopies, int EditAction);
    void RotateCopy(CComplex c, double t, int ncopies, int EditAction);

    // Default snap distance for geometry
    double defaultSnapDistance() const;

    // Length unit scale factor to meters
    double lengthScale() const;

    // Bounding box including nodes, block labels, and arc sample points.
    // Returns false if geometry is empty.
    bool getBoundingBox(double &xmin, double &ymin, double &xmax, double &ymax) const;

    // --- Undo/Redo ---
    // Call pushUndo() at the start of every user-initiated operation
    // (mouse click, toolbar button, Lua command that modifies geometry).
    // Do NOT call from internal helpers — only from top-level entry points.
    void pushUndo();
    bool canUndo() const;
    bool canRedo() const;
    void undo();
    void redo();
    void clearUndoHistory();

private:
    static constexpr int MAX_UNDO_LEVELS = 50;

    struct GeometrySnapshot {
        std::vector<FNode> nodes;
        std::vector<FSegment> segs;
        std::vector<FArcSegment> arcs;
        std::vector<FBlockLabel> blocks;
    };

    GeometrySnapshot takeSnapshot() const;
    void restoreSnapshot(const GeometrySnapshot &snap);

    std::deque<GeometrySnapshot> m_undoStack;
    std::deque<GeometrySnapshot> m_redoStack;
};

#endif // DOCUMENT_H
