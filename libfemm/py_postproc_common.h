// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors
//
// py_postproc_common.h - shared postprocessor display view-command registrations
// (registerDisplayViewCmds) plus the contour/plot template helpers used by the
// per-physics postprocessors.
#ifndef PY_POSTPROC_COMMON_H
#define PY_POSTPROC_COMMON_H

#include "py_bind.h"
#include "utf8_io.h"
#include "script_callbacks.h"
#include "coord_mode.h"
#include "xy_export.h"
#include "mag_types.h"
#include "postproc_select.h"

#include <cmath>
#include <cstdio>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#ifndef PI
#define PI 3.141592653589793238462643383279
#endif

// ---------------------------------------------------------------------------
// Bulk getters: array construction, shared by all four physics
// ---------------------------------------------------------------------------
// A bulk getter returns typed ulab arrays rather than building wire text. The
// in-process caller gets something it can compute with; the IPC caller gets the
// same bytes as before, because py_state.cpp's format_value flattens an ndarray
// through the very same per-value formatter every scalar result already uses.
//
// Values are written straight into the payload, which ulab allocates pointer-
// free (gc_alloc_atomic), so there is no intermediate string and nothing for the
// collector to scan.
//
// The DC/AC dtype rule lives here too, because getting it wrong is silent:
//   magnetics and current flow   have a Frequency -> f8 at DC, c16 at AC
//   electrostatics and heat flow have none        -> f8 always
// Byte-identity holds either way (CComplex::ToString and format_complex both
// collapse a zero imaginary part), so this is a choice made for the script side.
extern "C" {
#include "ndarray.h"   // ulab — header is not extern-"C" guarded, so wrap it
}

// (rows, cols) -> a dense ndarray; cols == 0 makes it 1-D of length rows.
//
// ulab indexes shape[] RIGHT-ALIGNED -- the last axis is shape[ULAB_MAX_DIMS-1],
// and ndarray_new_ndarray() reads the top `ndim` slots. So a 1-D array cannot be
// built from a one-element shape array: it would read shape[ULAB_MAX_DIMS-1]
// past the end. Use ulab's own linear-array helper for that, and fill the 2-D
// case right-aligned so it stays correct if ULAB_MAX_DIMS is ever raised.
// (This is the left-vs-right-aligned trap documented in docs/ulab-max-dims.md,
// which coincidentally works out for 2-D at the current ULAB_MAX_DIMS of 2.)
inline ndarray_obj_t *bulkArray(size_t rows, size_t cols, uint8_t dtype) {
    if (cols == 0) return ndarray_new_linear_array(rows, dtype);
    size_t sh[ULAB_MAX_DIMS] = {};
    sh[ULAB_MAX_DIMS - 2] = rows;
    sh[ULAB_MAX_DIMS - 1] = cols;
    return ndarray_new_dense_ndarray(2, sh, dtype);
}

inline mp_float_t *bulkF8(ndarray_obj_t *a) { return (mp_float_t *)a->array; }
inline int32_t *bulkI4(ndarray_obj_t *a) { return (int32_t *)a->array; }

// One complex element: ulab stores c16 as two contiguous mp_float_t, which is
// also CComplex's layout — see py_clinalg.cpp, which relies on the same thing.
inline void bulkPutC(mp_float_t *p, size_t i, const CComplex &z) {
    p[2 * i] = (mp_float_t)z.re;
    p[2 * i + 1] = (mp_float_t)z.im;
}

// The dtype a field quantity takes for a document at this frequency.
inline uint8_t bulkFieldDtype(double frequency) {
    return (frequency != 0) ? NDARRAY_COMPLEX : NDARRAY_FLOAT;
}

// Dispatch a view command with numeric args; returns the result string (empty
// if there is no callback / no result).
inline std::string ppViewCmd(const std::string &cmd, int nargs, double *args) {
    auto cb = get_view_command_callback();
    if (!cb) return {};
    const char *result = cb(cmd.c_str(), nargs, args);
    return result ? std::string(result) : std::string();
}

// Register *o_setgrid(density, "cart"|"polar") for a postprocessor prefix.
// Grid spacing is view state and is forwarded; the coordinate mode is document
// state and is set here, so it works with or without a GUI. Both slots are
// optional and a negative value in the forwarded pair means "not specified" --
// spacing is positive by construction and Coords is 0 or 1.
template<typename DocType>
void registerSetGrid(femm_py::Module &m, const std::string &pfx,
                     DocType *&s_doc, const char *errmsg) {
    std::string cmd = pfx + "_setgrid";
    m.def(cmd, [&s_doc, errmsg, cmd](std::optional<double> density,
                                     std::optional<std::string> ptype) {
        int coords = -1;
        if (ptype) {
            coords = parseCoordMode(*ptype);
            if (coords < 0) throw std::runtime_error(COORD_MODE_ERR);
            if (!s_doc) throw std::runtime_error(errmsg);
            s_doc->Coords = coords;
        }
        double a[2] = {density.value_or(-1.0), (double)coords};
        ppViewCmd(cmd, 2, a);
    });
}

// Register the shared display view commands for a postprocessor prefix.
// All forward up to 6 numeric args. Physics with string params
// (mo_/co_showdensityplot, *_showcontourplot) override the relevant entries
// after calling this.
inline void registerDisplayViewCmds(femm_py::Module &m, const std::string &pfx) {
    auto reg = [&m, &pfx](const std::string &suffix, const std::string &viewname) {
        std::string cmd = pfx + "_" + viewname;
        m.def(pfx + "_" + suffix,
              [cmd](std::optional<double> a0, std::optional<double> a1, std::optional<double> a2,
                    std::optional<double> a3, std::optional<double> a4, std::optional<double> a5)
                  -> std::string {
                  std::optional<double> o[6] = {a0, a1, a2, a3, a4, a5};
                  double a[6] = {};
                  int n = 0;
                  for (int i = 0; i < 6; i++) {
                      if (o[i]) a[n++] = *o[i];
                      else break;
                  }
                  return ppViewCmd(cmd, n, a);
              });
    };
    reg("showmesh", "showmesh");
    reg("hidemesh", "hidemesh");
    reg("showdensityplot", "showdensity");
    reg("hidedensityplot", "hidedensity");
    reg("showcontourplot", "showcontour");
    reg("hidecontourplot", "hidecontour");
    reg("showvectorplot", "vectorplot");
    reg("showpoints", "showpoints");
    reg("hidepoints", "hidepoints");
    reg("shownames", "shownames");
}

// Contour type string -> int (shared by mo_ and co_).
inline int ppContourType(const std::string &type) {
    if (type == "imag") return 1;
    if (type == "both") return 2;
    return 0;  // "real" or default
}

// ppClosestNode now lives in postproc_select.h so the GUI can share it.

// selectpoint(x, y) - add a point to the contour, following segments/arcs.
template<class Doc>
void ppSelectPoint(double mx, double my, Doc *doc) {
    if (doc->nodelist.empty()) return;

    int i = ppClosestNode(mx, my, doc->nodelist);
    CComplex z(doc->nodelist[i].x, doc->nodelist[i].y);

    if (!doc->contour.empty()) {
        CComplex y = doc->contour.back();

        if (y.re == z.re && y.im == z.im) return;

        int j = ppClosestNode(y.re, y.im, doc->nodelist);
        CComplex x(doc->nodelist[j].x, doc->nodelist[j].y);

        int lineno = -1, arcno = -1;
        int flag = 0;
        double d1 = 1.e08, d2;

        if (abs(x - y) < 1.e-08) {
            for (int k = 0; k < (int)doc->linelist.size(); k++) {
                if ((doc->linelist[k].n0 == j && doc->linelist[k].n1 == i) ||
                    (doc->linelist[k].n0 == i && doc->linelist[k].n1 == j)) {
                    d2 = fabs(doc->ShortestDistanceFromSegment(mx, my, k));
                    if (d2 < d1) { lineno = k; d1 = d2; }
                }
            }
        }

        if (abs(x - y) < 1.e-08) {
            for (int k = 0; k < (int)doc->arclist.size(); k++) {
                if (doc->arclist[k].n0 == j && doc->arclist[k].n1 == i) {
                    d2 = doc->ShortestDistanceFromInputArc(CComplex(mx, my), k);
                    if (d2 < d1) { arcno = k; lineno = -1; flag = 1; d1 = d2; }
                }
                if (doc->arclist[k].n0 == i && doc->arclist[k].n1 == j) {
                    d2 = doc->ShortestDistanceFromInputArc(CComplex(mx, my), k);
                    if (d2 < d1) { arcno = k; lineno = -1; flag = 0; d1 = d2; }
                }
            }
        }

        if (lineno < 0 && arcno < 0) {
            doc->contour.push_back(z);
        }

        if (lineno >= 0) {
            int mm = (int)doc->contour.size();
            if (mm > 1) {
                if (abs(doc->contour[mm - 2] - z) < 1.e-08) return;
            }
            doc->contour.push_back(z);
        }

        if (arcno >= 0) {
            int k = arcno;
            CComplex c;
            double R;
            doc->GetCircle(doc->arclist[k], c, R);
            int npts = (int)ceil(doc->arclist[k].ArcLength / doc->arclist[k].MaxSideLength);
            CComplex rot;
            CComplex I(0, 1);
            if (flag)
                rot = exp(I * doc->arclist[k].ArcLength * PI / (180.0 * (double)npts));
            else
                rot = exp(-I * doc->arclist[k].ArcLength * PI / (180.0 * (double)npts));
            for (int ii = 0; ii < npts; ii++) {
                y = (y - c) * rot + c;
                int mm = (int)doc->contour.size();
                if (mm > 1) {
                    if (abs(doc->contour[mm - 2] - y) < 1.e-08) return;
                }
                doc->contour.push_back(y);
            }
        }
    } else {
        doc->contour.push_back(z);
    }
}

// ppSelectConductor / ppSelectConductorNear live in postproc_select.h (Python-
// free) so the GUI can share them; included below.

// makeplot(plotType, numPoints, filename) - sample field values along the
// contour, optionally write to a file, return number of points. File I/O is
// C++ (fopen), independent of MicroPython's VFS.
//
// The file is written by xyexport::writeCsv, the same call the postprocessor's
// Export... button makes, so a scripted export and a hand export of the same
// plot produce the same file. `names` is the physics' plot-quantity table
// (xyexport::magPlotNames() and friends); it supplies the column headings.
template<class Doc>
double ppMakePlot(Doc *doc, int plotType, int numPoints, const std::string &filename,
                  const std::vector<std::string> &names) {
    if (doc->contour.size() < 2)
        throw std::runtime_error("No contour defined for makeplot");
    if (numPoints < 2) numPoints = 2;

    std::vector<typename Doc::XYPlotPoint> pts;
    doc->GetLineValues(pts, plotType, numPoints);

    if (!filename.empty() && !pts.empty()) {
        const std::string name =
            (plotType >= 0 && plotType < (int)names.size())
                ? xyexport::shortName(names[plotType])
                : std::string("Value");
        const int ncols = (int)pts[0].vals.size();

        std::vector<double> x;
        std::vector<std::vector<double>> cols((size_t)ncols);
        x.reserve(pts.size());
        for (auto &p : pts) {
            x.push_back(p.x);
            for (int c = 0; c < ncols; c++)
                cols[c].push_back(c < (int)p.vals.size() ? p.vals[c] : 0.0);
        }
        if (!xyexport::writeCsv(filename, xyexport::contourAxisLabel(),
                                xyexport::columnLabels(name, ncols), x, cols))
            throw std::runtime_error("Could not open output file: " + filename);
    }

    return (double)pts.size();
}

#endif  // PY_POSTPROC_COMMON_H
