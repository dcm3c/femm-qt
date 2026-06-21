// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors
//
// py_curr_postproc.cpp - co_* function implementations.
// Operates on CurrResultsDoc. Current flow can be AC,
// so most returned field values are complex.
#include "py_curr_postproc.h"
#include "py_curr_editor.h"
#include "py_editor_common.h"
#include "py_postproc_common.h"
#include "curr_results_doc.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <optional>
#include <string>
#include <tuple>

using femm_py::caster;
using std::optional;

static const char *COERR = "No current flow output in focus";

static CurrResultsDoc *s_doc = nullptr;
CurrResultsDoc *active_curr_results() { return s_doc; }

void register_curr_postproc_py(CurrResultsDoc *doc) {
    s_doc = doc;
    femm_py::Module &m = femm_py::femm_module;

    // ---- Point values: 15 complex (V,Jx,Jy,Kx,Ky,Ex,Ey,ex,ey,Jdx,Jdy,ox,oy,Jcx,Jcy) ----
    using PV = std::tuple<CComplex, CComplex, CComplex, CComplex, CComplex, CComplex, CComplex,
                          CComplex, CComplex, CComplex, CComplex, CComplex, CComplex, CComplex,
                          CComplex>;
    m.def("co_getpointvalues", [](double px, double py) -> optional<PV> {
        check_doc(s_doc, COERR);
        CPointVals u;
        if (!s_doc->GetPointValues(px, py, u)) return std::nullopt;
        return PV{u.V, u.Jx, u.Jy, u.Kx, u.Ky, u.Ex, u.Ey, u.ex, u.ey,
                  u.Jdx, u.Jdy, u.ox, u.oy, u.Jcx, u.Jcy};
    });

    // ---- Block selection / integration ----
    m.def("co_selectblock", [](double px, double py) {
        check_doc(s_doc, COERR);
        if (!s_doc->meshelem.empty()) {
            int k = s_doc->InTriangle(px, py);
            if (k >= 0) {
                s_doc->bHasMask = false;
                s_doc->labellist[s_doc->meshelem[k].lbl].ToggleSelect();
            }
        }
    });
    m.def("co_groupselectblock", [](optional<int> grp) {
        check_doc(s_doc, COERR);
        s_doc->bHasMask = false;
        if (!grp) {
            for (auto &b : s_doc->labellist) b.ToggleSelect();
        } else {
            for (auto &b : s_doc->labellist)
                if (b.InGroup == *grp) b.ToggleSelect();
        }
    });
    m.def("co_clearblock", []() {
        check_doc(s_doc, COERR);
        for (auto &b : s_doc->labellist) b.IsSelected = false;
        s_doc->bHasMask = false;
    });
    m.def("co_blockintegral", [](int type) -> CComplex {
        check_doc(s_doc, COERR);
        bool anySelected = false;
        for (const auto &b : s_doc->labellist)
            if (b.IsSelected) { anySelected = true; break; }
        // For type>=6 (force/torque) OF also accepts a mesh-node-only selection.
        if (!anySelected && type >= 6)
            for (const auto &mn : s_doc->meshnode)
                if (mn.IsSelected) { anySelected = true; break; }
        if (!anySelected)
            throw std::runtime_error("Cannot integrate\nNo area has been selected");
        if (type >= 6) s_doc->MakeMask();
        return s_doc->BlockIntegral(type);
    });

    // ---- Contour ----
    m.def("co_addcontour", [](double x, double y) {
        check_doc(s_doc, COERR);
        CComplex z(x, y);
        int n = (int)s_doc->contour.size();
        if (n > 0) {
            if (z.re != s_doc->contour[n - 1].re || z.im != s_doc->contour[n - 1].im)
                s_doc->contour.push_back(z);
        } else {
            s_doc->contour.push_back(z);
        }
    });
    m.def("co_clearcontour", []() {
        check_doc(s_doc, COERR);
        s_doc->contour.clear();
    });
    m.def("co_bendcontour", [](double angle, optional<double> anglestep) {
        check_doc(s_doc, COERR);
        s_doc->BendContour(angle, anglestep.value_or(1.0));
    });

    // ---- Line integral: 4 complex values ----
    m.def("co_lineintegral", [](int type) -> std::tuple<CComplex, CComplex, CComplex, CComplex> {
        check_doc(s_doc, COERR);
        if (type < 0 || type > 3)
            throw std::runtime_error("Invalid line integral selected " + std::to_string(type));
        CComplex z[4] = {};
        s_doc->LineIntegral(type, z);
        return std::make_tuple(z[0], z[1], z[2], z[3]);
    });

    // ---- Conductor properties (V, q complex); throws if unknown ----
    m.def("co_getconductorproperties", [](const std::string &name)
              -> std::tuple<CComplex, CComplex> {
        check_doc(s_doc, COERR);
        for (int i = 0; i < (int)s_doc->circproplist.size(); i++)
            if (name == s_doc->circproplist[i].CircName)
                return std::make_tuple(s_doc->circproplist[i].V, s_doc->circproplist[i].q);
        throw std::runtime_error("Unknown conductor");
    });

    // ---- Problem info (ProblemType, Frequency, Depth, LengthConv) ----
    m.def("co_getprobleminfo", []() -> std::tuple<double, double, double, double> {
        check_doc(s_doc, COERR);
        return std::make_tuple((double)s_doc->ProblemType, s_doc->Frequency, s_doc->Depth,
                               s_doc->LengthConv());
    });

    // ---- Mesh access (uses cmeshnode/cmeshelem) ----
    m.def("co_numnodes", []() -> double {
        check_doc(s_doc, COERR);
        return (double)s_doc->cmeshnode.size();
    });
    m.def("co_numelements", []() -> double {
        check_doc(s_doc, COERR);
        return (double)s_doc->cmeshelem.size();
    });
    m.def("co_getnode", [](int k_1) -> optional<std::tuple<double, double>> {
        check_doc(s_doc, COERR);
        int k = k_1 - 1;
        if (k < 0 || k >= (int)s_doc->cmeshnode.size()) return std::nullopt;
        return std::make_tuple(s_doc->cmeshnode[k].x, s_doc->cmeshnode[k].y);
    });
    m.def("co_getelement", [](int k_1)
              -> optional<std::tuple<double, double, double, double, double, double, double>> {
        check_doc(s_doc, COERR);
        int k = k_1 - 1;
        if (k < 0 || k >= (int)s_doc->cmeshelem.size()) return std::nullopt;
        auto &el = s_doc->cmeshelem[k];
        int grp = (el.lbl >= 0 && el.lbl < (int)s_doc->labellist.size())
                      ? s_doc->labellist[el.lbl].InGroup : 0;
        return std::make_tuple((double)(el.p[0] + 1), (double)(el.p[1] + 1), (double)(el.p[2] + 1),
                               el.ctr.re, el.ctr.im, s_doc->ElmArea(k), (double)grp);
    });

    // ---- Smoothing ----
    auto smooth_fn = [](optional<std::string> mode) {
        check_doc(s_doc, COERR);
        std::string s = mode.value_or("");
        for (auto &c : s) if (c >= 'A' && c <= 'Z') c = (char)(c + 32);  // MakeLower
        if (s == "off") s_doc->Smooth = false;
        else if (s == "on") s_doc->Smooth = true;
        else throw std::runtime_error("Unknown option for smoothing");
    };
    m.def("co_smooth", smooth_fn);

    // ---- Contour selection ----
    m.def("co_selectpoint", [](double mx, double my) {
        check_doc(s_doc, COERR);
        ppSelectPoint(mx, my, s_doc);
    });
    m.def("co_selectconductor", [](const std::string &name) {
        check_doc(s_doc, COERR);
        ppSelectConductor(name, s_doc);
    });

    // ---- Display control ----
    registerDisplayViewCmds(m, "co");

    // co_showdensityplot(legend, gscale, type, upper, lower) — type is numeric
    // (manual 0..8): 0=|V|,1=|ReV|,2=|ImV|,3=|J|,4=|ReJ|,5=|ImJ|,6=|E|,7=|ReE|,8=|ImE|.
    // GUI flag = type+1 (order matches). Out-of-range -> |V|.
    m.def("co_showdensityplot", [](optional<double> legend, optional<double> gscale,
                                   optional<double> type, optional<double> upper,
                                   optional<double> lower) {
        check_doc(s_doc, COERR);
        if (legend && (int)*legend == -1) { double a[1] = {-1}; ppViewCmd("co_showdensity", 1, a); return; }
        int n = 0; double a[5] = {};
        if (legend) { a[0] = *legend; n = 1; }
        if (gscale) { a[1] = *gscale; n = 2; }
        if (type)   { int t = (int)*type; if (t < 0 || t > 8) t = 0; a[2] = t + 1; n = 3; }
        if (upper)  { a[3] = *upper; n = 4; }
        if (lower)  { a[4] = *lower; n = 5; }
        ppViewCmd("co_showdensity", n, a);
    });

    // Override: co_showcontourplot(numcontours, lower, upper, type)
    m.def("co_showcontourplot", [](optional<double> a0, optional<double> a1, optional<double> a2,
                                   optional<mp_obj_t> a3, optional<double> a4,
                                   optional<double> a5) -> std::string {
        double args[6] = {};
        int n = 0;
        if (a0) args[n++] = *a0;
        if (a1) args[n++] = *a1;
        if (a2) args[n++] = *a2;
        if (a3) {
            mp_obj_t o = *a3;
            args[n++] = mp_obj_is_str(o) ? (double)ppContourType(caster<std::string>::load(o))
                                         : caster<double>::load(o);
        }
        if (a4) args[n++] = *a4;
        if (a5) args[n++] = *a5;
        return ppViewCmd("co_showcontour", n, args);
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
    regv("co_zoomnatural");
    regv("co_zoomout");
    regv("co_zoomin");
    regv("co_zoom");
    regv("co_refreshview");
    regv("co_showgrid");
    regv("co_hidegrid");
    regv("co_gridsnap");
    regv("co_setgrid");
    regv("co_resize");
    regv("co_minimize");
    regv("co_maximize");
    regv("co_restore");
    m.def("co_setfocus", [](optional<std::string> docname) {
        if (docname) doc_registry_activate_results_byname(*docname);
        auto cb = get_view_command_callback();
        if (!cb) return;
        double args[4] = {};
        if (docname)
            cb(("co_setfocus:" + *docname).c_str(), 0, args);
        else
            cb("co_setfocus", 0, args);
    });
    m.def("co_savemetafile", []() {});  // deprecated no-op

    m.def("co_makeplot", [](int plotType, optional<int> numPoints,
                            optional<std::string> filename) -> double {
        check_doc(s_doc, COERR);
        return ppMakePlot(s_doc, plotType, numPoints.value_or(150),
                          filename ? fixpath(*filename) : std::string());
    });

    m.def("co_seteditmode", [](const std::string &mode) {
        auto cb = get_view_command_callback();
        if (!cb) return;
        double md = 0;
        if (!mode.empty()) {
            if (mode[0] == 'c' || mode[0] == 'C') md = 1;
            else if (mode[0] == 'a' || mode[0] == 'A') md = 2;
        }
        double args[4] = {md, 0, 0, 0};
        cb("co_seteditmode", 1, args);
    });

    m.def("co_savebitmap", [](const std::string &fname) {
        auto cb = get_view_command_callback();
        if (!cb) return;
        std::string fp = fixpath(fname);
        double args[4] = {};
        cb(("co_savebitmap:" + fp).c_str(), 0, args);
    });

    m.def("co_gettitle", []() -> std::string {
        check_doc(s_doc, COERR);
        return s_doc->filePath;
    });

    m.def("co_reload", []() {
        check_doc(s_doc, COERR);
        if (s_doc->filePath.empty()) return;
        std::string path = s_doc->filePath;
        CurrResultsDoc *oldDoc = s_doc;
        s_doc = new CurrResultsDoc();
        if (!s_doc->OpenDocument(path)) {
            delete s_doc;
            s_doc = oldDoc;
            throw std::runtime_error("Failed to reload: " + path);
        }
        register_curr_postproc_py(s_doc);
        auto cb = get_results_opened_callback();
        if (cb) cb(path.c_str(), s_doc);
        delete oldDoc;
    });

    m.def("co_close", []() {
        check_doc(s_doc, COERR);
        clear_curr_results_doc();
        auto cb = get_view_command_callback();
        if (cb) cb("co_close", 0, nullptr);
        s_doc = nullptr;
    });

    // ---- Backward-compatible aliases ----
    m.alias("co_selectline", "co_selectpoint");
    m.alias("co_showdensity", "co_showdensityplot");
    m.alias("co_hidedensity", "co_hidedensityplot");
    m.alias("co_showcontour", "co_showcontourplot");
    m.alias("co_hidecontour", "co_hidecontourplot");
    m.alias("co_vectorplot", "co_showvectorplot");
    m.alias("co_smoothing", "co_smooth");
    m.alias("co_getconductorprops", "co_getconductorproperties");
}
