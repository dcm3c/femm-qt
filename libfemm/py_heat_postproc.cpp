// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors
//
// py_heat_postproc.cpp - ho_* function implementations.
// Operates on HeatResultsDoc. All returned values real.
#include "py_heat_postproc.h"
#include "py_heat_editor.h"
#include "py_editor_common.h"
#include "py_postproc_common.h"
#include "heat_results_doc.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <optional>
#include <string>
#include <tuple>

using femm_py::caster;
using std::optional;

static const char *HOERR = "No current heat flow output in focus";

static HeatResultsDoc *s_doc = nullptr;
HeatResultsDoc *active_heat_results() { return s_doc; }
void deactivate_heat_postproc_py() { s_doc = nullptr; }

void register_heat_postproc_py(HeatResultsDoc *doc) {
    s_doc = doc;
    femm_py::Module &m = femm_py::femm_module;

    // ---- Point values (7 reals, or None) ----
    using PV = std::tuple<double, double, double, double, double, double, double>;
    m.def("ho_getpointvalues", [](double px, double py) -> optional<PV> {
        check_doc(s_doc, HOERR);
        HPointVals u;
        if (!s_doc->GetPointValues(px, py, u)) return std::nullopt;
        return PV{u.T, u.F.re, u.F.im, u.G.re, u.G.im, u.K.re, u.K.im};
    });

    // ---- Contour ----
    m.def("ho_addcontour", [](double x, double y) {
        check_doc(s_doc, HOERR);
        CComplex pt(x, y);
        if (s_doc->contour.empty() || pt != s_doc->contour.back())
            s_doc->contour.push_back(pt);
    });
    auto selectpoint_fn = [](double mx, double my) {
        check_doc(s_doc, HOERR);
        ppSelectPoint(mx, my, s_doc);
    };
    m.def("ho_selectpoint", selectpoint_fn);
    m.def("ho_clearcontour", []() {
        check_doc(s_doc, HOERR);
        s_doc->contour.clear();
    });
    m.def("ho_bendcontour", [](double angle, optional<double> anglestep) {
        check_doc(s_doc, HOERR);
        s_doc->BendContour(angle, anglestep.value_or(1.0));
    });

    // ---- Line integral ----
    m.def("ho_lineintegral", [](int type) -> std::tuple<double, double> {
        check_doc(s_doc, HOERR);
        if (type < 0 || type > 3)
            throw std::runtime_error("Invalid line integral selected " + std::to_string(type));
        double z[4] = {};
        s_doc->LineIntegral(type, z);
        return std::make_tuple(z[0], z[1]);
    });

    // ---- Block selection / integration ----
    m.def("ho_selectblock", [](double px, double py) {
        check_doc(s_doc, HOERR);
        if (!s_doc->meshelem.empty()) {
            int k = s_doc->InTriangle(px, py);
            if (k >= 0) {
                s_doc->bHasMask = false;
                s_doc->labellist[s_doc->meshelem[k].lbl].ToggleSelect();
            }
        }
    });
    m.def("ho_groupselectblock", [](optional<int> grp) {
        check_doc(s_doc, HOERR);
        s_doc->bHasMask = false;
        if (!grp) {
            for (auto &lbl : s_doc->labellist) lbl.ToggleSelect();
        } else {
            for (auto &lbl : s_doc->labellist)
                if (lbl.InGroup == *grp) lbl.ToggleSelect();
        }
    });
    m.def("ho_clearblock", []() {
        check_doc(s_doc, HOERR);
        ppClearSelection(s_doc);
    });
    m.def("ho_blockintegral", [](int type) -> std::tuple<double, double> {
        check_doc(s_doc, HOERR);
        bool anySelected = false;
        for (auto &lbl : s_doc->labellist)
            if (lbl.IsSelected) { anySelected = true; break; }
        if (!anySelected)
            throw std::runtime_error("Cannot integrate\nNo area has been selected");
        CComplex z = s_doc->BlockIntegral(type);
        return std::make_tuple(z.re, z.im);
    });

    // ---- Conductor properties (V, q), or None ----
    m.def("ho_getconductorproperties", [](const std::string &name)
              -> optional<std::tuple<double, double>> {
        check_doc(s_doc, HOERR);
        for (int i = 0; i < (int)s_doc->circproplist.size(); i++)
            if (name == s_doc->circproplist[i].CircName)
                return std::make_tuple(s_doc->circproplist[i].V, s_doc->circproplist[i].q);
        throw std::runtime_error("Unknown conductor");  // matches OF + co_
    });
    m.def("ho_selectconductor", [](const std::string &name) {
        check_doc(s_doc, HOERR);
        ppSelectConductor(name, s_doc);
    });

    // ---- Problem info ----
    m.def("ho_getprobleminfo", []() -> std::tuple<double, double, double> {
        check_doc(s_doc, HOERR);
        return std::make_tuple((double)s_doc->ProblemType, s_doc->Depth, s_doc->LengthConv());
    });

    // ---- Mesh access ----
    m.def("ho_numnodes", []() -> double {
        check_doc(s_doc, HOERR);
        return (double)s_doc->meshnode.size();
    });
    m.def("ho_numelements", []() -> double {
        check_doc(s_doc, HOERR);
        return (double)s_doc->meshelem.size();
    });
    m.def("ho_getnode", [](int k_1) -> optional<std::tuple<double, double>> {
        check_doc(s_doc, HOERR);
        int k = k_1 - 1;
        if (k < 0 || k >= (int)s_doc->meshnode.size()) return std::nullopt;
        return std::make_tuple(s_doc->meshnode[k].x, s_doc->meshnode[k].y);
    });
    m.def("ho_getelement", [](int k_1)
              -> optional<std::tuple<double, double, double, double, double, double, double>> {
        check_doc(s_doc, HOERR);
        int k = k_1 - 1;
        if (k < 0 || k >= (int)s_doc->meshelem.size()) return std::nullopt;
        auto &el = s_doc->meshelem[k];
        int grp = (el.lbl >= 0 && el.lbl < (int)s_doc->labellist.size())
                      ? s_doc->labellist[el.lbl].InGroup : 0;
        return std::make_tuple((double)(el.p[0] + 1), (double)(el.p[1] + 1), (double)(el.p[2] + 1),
                               el.ctr.re, el.ctr.im, s_doc->ElmArea(k), (double)grp);
    });

    // ---- Smoothing ----
    auto smooth_fn = [](optional<std::string> mode) {
        check_doc(s_doc, HOERR);
        std::string s = mode.value_or("");
        for (auto &c : s) if (c >= 'A' && c <= 'Z') c = (char)(c + 32);  // MakeLower
        if (s == "off") s_doc->Smooth = false;
        else if (s == "on") s_doc->Smooth = true;
        else throw std::runtime_error("Unknown option for smoothing");
    };
    m.def("ho_smooth", smooth_fn);

    // ---- Display control ----
    registerDisplayViewCmds(m, "ho");

    // ho_showdensityplot(legend, gscale, type, upper, lower) — type is numeric
    // (manual): 0=T, 1=|F|, 2=|G|. GUI flag = type+1 (order matches). Out-of-range -> T.
    m.def("ho_showdensityplot", [](optional<double> legend, optional<double> gscale,
                                   optional<double> type, optional<double> upper,
                                   optional<double> lower) {
        check_doc(s_doc, HOERR);
        if (legend && (int)*legend == -1) { double a[1] = {-1}; ppViewCmd("ho_showdensity", 1, a); return; }
        int n = 0; double a[5] = {};
        if (legend) { a[0] = *legend; n = 1; }
        if (gscale) { a[1] = *gscale; n = 2; }
        if (type)   { int t = (int)*type; if (t < 0 || t > 2) t = 0; a[2] = t + 1; n = 3; }
        if (upper)  { a[3] = *upper; n = 4; }
        if (lower)  { a[4] = *lower; n = 5; }
        ppViewCmd("ho_showdensity", n, a);
    });

    // ---- GUI view commands ----
    auto regv = [](const char *name) {
        std::string nm = name;
        m.def(nm, [nm](optional<double> a0, optional<double> a1, optional<double> a2,
                       optional<double> a3, optional<double> a4, optional<double> a5) -> std::string {
            optional<double> o[6] = {a0, a1, a2, a3, a4, a5};
            double a[6] = {};
            int n = 0;
            for (int i = 0; i < 6; i++) { if (o[i]) a[n++] = *o[i]; else break; }
            return ppViewCmd(nm, n, a);
        });
    };
    regv("ho_zoomnatural");
    regv("ho_zoomout");
    regv("ho_zoomin");
    regv("ho_zoom");
    regv("ho_refreshview");
    regv("ho_showgrid");
    regv("ho_hidegrid");
    // Takes the documented string flag, not a double -- see
    // gridSnapFlag in py_editor_common.h.
    m.def("ho_gridsnap", [](const std::string &flag) {
        double a[1] = {gridSnapFlag(flag)};
        ppViewCmd("ho_gridsnap", 1, a);
    });
    registerSetGrid(m, "ho", s_doc, HOERR);
    regv("ho_resize");
    regv("ho_minimize");
    regv("ho_maximize");
    regv("ho_restore");
    m.def("ho_setfocus", [](optional<std::string> docname) {
        // Heat-flow results carry the .anh extension; normalize so a bare "temp"
        // matches the "temp.anh" tab label.
        std::string dn;
        if (docname) {
            dn = canonicalDocName(*docname, ".anh");
            doc_registry_activate_results_byname(dn);
        }
        auto cb = get_view_command_callback();
        if (!cb) return;
        double args[4] = {};
        if (docname)
            cb(("ho_setfocus:" + dn).c_str(), 0, args);
        else
            cb("ho_setfocus", 0, args);
    });
    m.def("ho_savemetafile", [](std::optional<std::string>) {});  // No metafile output in femm-qt;
    // the filename is accepted and ignored so ported scripts do not raise.
    // Use *_savebitmap to save a picture of the view.

    m.def("ho_seteditmode", [](const std::string &mode) {
        auto cb = get_view_command_callback();
        if (!cb) return;
        double md = 0;
        if (!mode.empty()) {
            if (mode[0] == 'c' || mode[0] == 'C') md = 1;
            else if (mode[0] == 'a' || mode[0] == 'A') md = 2;
        }
        double args[4] = {md, 0, 0, 0};
        cb("ho_seteditmode", 1, args);
    });

    m.def("ho_savebitmap", [](const std::string &fname) {
        auto cb = get_view_command_callback();
        if (!cb) return;
        std::string fp = fixpath(fname);
        double args[4] = {};
        cb(("ho_savebitmap:" + fp).c_str(), 0, args);
    });

    // OF's 4th argument selected among three text layouts (multi-column with
    // or without a legend, Mathematica-style) and, when omitted, meant "write
    // an extended metafile instead of data". femm-qt writes one data format
    // and has no metafile output, so the argument is accepted and ignored --
    // dropping it would turn every ported call into a TypeError.
    m.def("ho_makeplot", [](int plotType, optional<int> numPoints,
                            optional<std::string> filename,
                            optional<int> /*fileFormat*/) -> double {
        check_doc(s_doc, HOERR);
        return ppMakePlot(s_doc, plotType, numPoints.value_or(150),
                          filename ? fixpath(*filename) : std::string(),
                          xyexport::heatPlotNames());
    });

    m.def("ho_gettitle", []() -> std::string {
        check_doc(s_doc, HOERR);
        return s_doc->filePath;
    });

    m.def("ho_reload", []() {
        check_doc(s_doc, HOERR);
        if (s_doc->filePath.empty()) return;
        std::string path = s_doc->filePath;
        HeatResultsDoc *oldDoc = s_doc;
        s_doc = new HeatResultsDoc();
        if (!s_doc->OpenDocument(path)) {
            delete s_doc;
            s_doc = oldDoc;
            throw std::runtime_error("Failed to reload: " + path);
        }
        register_heat_postproc_py(s_doc);
        auto cb = get_results_opened_callback();
        if (cb) cb(path.c_str(), s_doc);
        delete oldDoc;
    });

    m.def("ho_close", []() {
        check_doc(s_doc, HOERR);
        clear_heat_results_doc();
        auto cb = get_view_command_callback();
        if (cb) cb("ho_close", 0, nullptr);
        s_doc = nullptr;
    });

    // ---- Backward-compatible aliases ----
    m.alias("ho_selectline", "ho_selectpoint");
    m.alias("ho_showdensity", "ho_showdensityplot");
    m.alias("ho_hidedensity", "ho_hidedensityplot");
    m.alias("ho_showcontour", "ho_showcontourplot");
    m.alias("ho_hidecontour", "ho_hidecontourplot");
    m.alias("ho_vectorplot", "ho_showvectorplot");
    m.alias("ho_smoothing", "ho_smooth");
    m.alias("ho_getconductorprops", "ho_getconductorproperties");
}
