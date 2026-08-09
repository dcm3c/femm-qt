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
