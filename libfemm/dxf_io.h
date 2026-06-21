// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors

// dxf_io.h — DXF import/export for FEMM geometry
#ifndef DXF_IO_H
#define DXF_IO_H

#include <string>

class DocumentBase;

// Read a DXF file and add its geometry to the document.
// Handles: LINE, ARC, CIRCLE, LWPOLYLINE, POLYLINE/VERTEX (with bulge),
// ELLIPSE (approximated as arcs), SPLINE (approximated as polyline).
// Returns true on success.
bool ReadDXF(DocumentBase &doc, const std::string &filename, double tolerance = -1);

// Write the document's geometry as a DXF file.
// Exports segments as LINE entities and arcs as ARC entities.
bool WriteDXF(const DocumentBase &doc, const std::string &filename);

#endif // DXF_IO_H
