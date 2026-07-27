// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors
//
// py_elec_postproc.cpp - eo_* function implementations.
// Operates on ElecResultsDoc. All returned field
// values are real (electrostatics is DC).
#include "py_elec_postproc.h"
#include "py_elec_editor.h"
#include "py_editor_common.h"
#include "py_postproc_common.h"
#include "elec_results_doc.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <optional>
#include <string>
#include <tuple>

using femm_py::caster;
using std::optional;

static const char *EOERR = "No current electrostatics output in focus";

static ElecResultsDoc *s_doc = nullptr;
ElecResultsDoc *active_elec_results() { return s_doc; }

static void appendDouble(std::string &buf, double v) {
    char c[32];
    snprintf(c, sizeof(c), "%.17g", v);
    buf += c;
    buf += '\t';
}
static void appendInt(std::string &buf, int v) {
    buf += std::to_string(v);
    buf += '\t';
}

void deactivate_elec_postproc_py() { s_doc = nullptr; }

void register_elec_postproc_py(ElecResultsDoc *doc) {
    s_doc = doc;
    femm_py::Module &m = femm_py::femm_module;

    // ---- Point values (8 reals, or None) ----
    using PV = std::tuple<double, double, double, double, double, double, double, double>;
    m.def("eo_getpointvalues", [](double px, double py) -> optional<PV> {
        check_doc(s_doc, EOERR);
        EPointVals u;
        if (!s_doc->GetPointValues(px, py, u)) return std::nullopt;
        return PV{u.V, u.D.re, u.D.im, u.E.re, u.E.im, u.e.re, u.e.im, u.nrg};
    });

    // ---- Block selection / integration ----
    m.def("eo_selectblock", [](double px, double py) {
        check_doc(s_doc, EOERR);
        if (!s_doc->meshelem.empty()) {
            int k = s_doc->InTriangle(px, py);
            if (k >= 0) {
                s_doc->bHasMask = false;
                s_doc->labellist[s_doc->meshelem[k].lbl].ToggleSelect();
            }
        }
    });
    m.def("eo_groupselectblock", [](optional<int> grp) {
        check_doc(s_doc, EOERR);
        s_doc->bHasMask = false;
        if (!grp) {
            for (auto &lbl : s_doc->labellist) lbl.ToggleSelect();
        } else {
            for (auto &lbl : s_doc->labellist)
                if (lbl.InGroup == *grp) lbl.ToggleSelect();
        }
    });
    m.def("eo_clearblock", []() {
        check_doc(s_doc, EOERR);
        ppClearSelection(s_doc);
    });
    m.def("eo_blockintegral", [](int type) -> std::tuple<double, double> {
        check_doc(s_doc, EOERR);
        bool anySelected = false;
        for (auto &lbl : s_doc->labellist)
            if (lbl.IsSelected) { anySelected = true; break; }
        // For type>=5 (force/torque) OF also accepts a mesh-node-only selection.
        if (!anySelected && type >= 5)
            for (auto &mn : s_doc->meshnode)
                if (mn.IsSelected) { anySelected = true; break; }
        if (!anySelected)
            throw std::runtime_error("Cannot integrate\nNo area has been selected");
        if (type >= 5) s_doc->MakeMask();
        CComplex z = s_doc->BlockIntegral(type);
        if (s_doc->maskSelectionInvalid)
            throw std::runtime_error("The selected region is invalid. A valid selection "
                                     "cannot abut a region which is not free space.");
        return std::make_tuple(z.re, z.im);
    });

    // ---- Line integral ----
    m.def("eo_lineintegral", [](int type) -> std::tuple<double, double> {
        check_doc(s_doc, EOERR);
        if (type < 0 || type > 4)
            throw std::runtime_error("Invalid line integral selected " + std::to_string(type));
        double z[4] = {};
        s_doc->LineIntegral(type, z);
        return std::make_tuple(z[0], z[1]);
    });

    // ---- Conductor properties (V, q), or None ----
    m.def("eo_getconductorproperties", [](const std::string &name)
              -> optional<std::tuple<double, double>> {
        check_doc(s_doc, EOERR);
        for (int i = 0; i < (int)s_doc->circproplist.size(); i++)
            if (name == s_doc->circproplist[i].CircName)
                return std::make_tuple(s_doc->circproplist[i].V, s_doc->circproplist[i].q);
        throw std::runtime_error("Unknown conductor");  // matches OF + co_
    });

    // ---- Contour ----
    m.def("eo_addcontour", [](double x, double y) {
        check_doc(s_doc, EOERR);
        CComplex pt(x, y);
        if (s_doc->contour.empty() || pt != s_doc->contour.back())
            s_doc->contour.push_back(pt);
    });
    m.def("eo_selectpoint", [](double mx, double my) {
        check_doc(s_doc, EOERR);
        ppSelectPoint(mx, my, s_doc);
    });
    m.def("eo_clearcontour", []() {
        check_doc(s_doc, EOERR);
        s_doc->contour.clear();
    });
    m.def("eo_bendcontour", [](double angle, optional<double> anglestep) {
        check_doc(s_doc, EOERR);
        s_doc->BendContour(angle, anglestep.value_or(1.0));
    });
    m.def("eo_selectconductor", [](const std::string &name) {
        check_doc(s_doc, EOERR);
        ppSelectConductor(name, s_doc);
    });

    // ---- Mesh access ----
    m.def("eo_numnodes", []() -> double {
        check_doc(s_doc, EOERR);
        return (double)s_doc->meshnode.size();
    });
    m.def("eo_numelements", []() -> double {
        check_doc(s_doc, EOERR);
        return (double)s_doc->meshelem.size();
    });
    m.def("eo_getnode", [](int k_1) -> optional<std::tuple<double, double>> {
        check_doc(s_doc, EOERR);
        int k = k_1 - 1;
        if (k < 0 || k >= (int)s_doc->meshnode.size()) return std::nullopt;
        return std::make_tuple(s_doc->meshnode[k].x, s_doc->meshnode[k].y);
    });
    m.def("eo_getelement", [](int k_1)
              -> optional<std::tuple<double, double, double, double, double, double, double>> {
        check_doc(s_doc, EOERR);
        int k = k_1 - 1;
        if (k < 0 || k >= (int)s_doc->meshelem.size()) return std::nullopt;
        auto &el = s_doc->meshelem[k];
        int grp = (el.lbl >= 0 && el.lbl < (int)s_doc->labellist.size())
                      ? s_doc->labellist[el.lbl].InGroup : 0;
        return std::make_tuple((double)(el.p[0] + 1), (double)(el.p[1] + 1), (double)(el.p[2] + 1),
                               el.ctr.re, el.ctr.im, s_doc->ElmArea(k), (double)grp);
    });

    // ---- Bulk retrieval ----
    m.def("eo_getallelements", []() -> std::string {
        check_doc(s_doc, EOERR);
        int ne = (int)s_doc->meshelem.size();
        std::string buf;
        buf.reserve(ne * 70);
        for (int k = 0; k < ne; k++) {
            auto &el = s_doc->meshelem[k];
            int grp = (el.lbl >= 0 && el.lbl < (int)s_doc->labellist.size())
                          ? s_doc->labellist[el.lbl].InGroup : 0;
            appendInt(buf, el.p[0] + 1);
            appendInt(buf, el.p[1] + 1);
            appendInt(buf, el.p[2] + 1);
            appendDouble(buf, el.ctr.re);
            appendDouble(buf, el.ctr.im);
            appendDouble(buf, s_doc->ElmArea(k));
            appendInt(buf, grp);
        }
        if (!buf.empty()) buf.pop_back();
        return buf;
    });
    m.def("eo_getallnodes", []() -> std::string {
        check_doc(s_doc, EOERR);
        int nn = (int)s_doc->meshnode.size();
        std::string buf;
        buf.reserve(nn * 50);
        for (int k = 0; k < nn; k++) {
            appendDouble(buf, s_doc->meshnode[k].x);
            appendDouble(buf, s_doc->meshnode[k].y);
        }
        if (!buf.empty()) buf.pop_back();
        return buf;
    });

    // ---- Display control ----
    registerDisplayViewCmds(m, "eo");

    // eo_showdensityplot(legend, gscale, type, upper_D, lower_D) — type is numeric
    // (manual): 0=V, 1=|D|, 2=|E|. The GUI density flag order is 1=|D|, 2=|E|, 3=V,
    // so remap the documented type to the GUI flag; out-of-range -> V (OF clamp).
    m.def("eo_showdensityplot", [](optional<double> legend, optional<double> gscale,
                                   optional<double> type, optional<double> upper,
                                   optional<double> lower) {
        check_doc(s_doc, EOERR);
        if (legend && (int)*legend == -1) { double a[1] = {-1}; ppViewCmd("eo_showdensity", 1, a); return; }
        int n = 0; double a[5] = {};
        if (legend) { a[0] = *legend; n = 1; }
        if (gscale) { a[1] = *gscale; n = 2; }
        if (type)   { int t = (int)*type; a[2] = (t == 1) ? 1 : (t == 2) ? 2 : 3; n = 3; }
        if (upper)  { a[3] = *upper; n = 4; }
        if (lower)  { a[4] = *lower; n = 5; }
        ppViewCmd("eo_showdensity", n, a);
    });

    auto smooth_fn = [](optional<std::string> mode) {
        check_doc(s_doc, EOERR);
        std::string s = mode.value_or("");
        for (auto &c : s) if (c >= 'A' && c <= 'Z') c = (char)(c + 32);  // MakeLower
        if (s == "off") s_doc->Smooth = false;
        else if (s == "on") s_doc->Smooth = true;
        else throw std::runtime_error("Unknown option for smoothing");
    };
    m.def("eo_smooth", smooth_fn);

    m.def("eo_getprobleminfo", []() -> std::tuple<double, double, double> {
        check_doc(s_doc, EOERR);
        return std::make_tuple((double)s_doc->ProblemType, s_doc->Depth, s_doc->LengthConv());
    });

    m.def("eo_makeplot", [](int plotType, optional<int> numPoints,
                            optional<std::string> filename) -> double {
        check_doc(s_doc, EOERR);
        return ppMakePlot(s_doc, plotType, numPoints.value_or(150),
                          filename ? fixpath(*filename) : std::string());
    });

    m.def("eo_gettitle", []() -> std::string {
        check_doc(s_doc, EOERR);
        return s_doc->filePath;
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
    regv("eo_zoomnatural");
    regv("eo_zoomout");
    regv("eo_zoomin");
    regv("eo_zoom");
    regv("eo_refreshview");
    regv("eo_showgrid");
    regv("eo_hidegrid");
    regv("eo_gridsnap");
    regv("eo_setgrid");
    regv("eo_resize");
    regv("eo_minimize");
    regv("eo_maximize");
    regv("eo_restore");
    m.def("eo_setfocus", [](optional<std::string> docname) {
        // Electrostatics results carry the .res extension; normalize so a bare
        // "temp" matches the "temp.res" tab label.
        std::string dn;
        if (docname) {
            dn = canonicalDocName(*docname, ".res");
            doc_registry_activate_results_byname(dn);
        }
        auto cb = get_view_command_callback();
        if (!cb) return;
        double args[4] = {};
        if (docname)
            cb(("eo_setfocus:" + dn).c_str(), 0, args);
        else
            cb("eo_setfocus", 0, args);
    });
    m.def("eo_savemetafile", []() {});  // deprecated no-op

    m.def("eo_seteditmode", [](const std::string &mode) {
        auto cb = get_view_command_callback();
        if (!cb) return;
        double md = 0;
        if (!mode.empty()) {
            if (mode[0] == 'c' || mode[0] == 'C') md = 1;
            else if (mode[0] == 'a' || mode[0] == 'A') md = 2;
        }
        double args[4] = {md, 0, 0, 0};
        cb("eo_seteditmode", 1, args);
    });

    m.def("eo_savebitmap", [](const std::string &fname) {
        auto cb = get_view_command_callback();
        if (!cb) return;
        std::string fp = fixpath(fname);
        double args[4] = {};
        cb(("eo_savebitmap:" + fp).c_str(), 0, args);
    });

    m.def("eo_reload", []() {
        check_doc(s_doc, EOERR);
        if (s_doc->filePath.empty()) return;
        std::string path = s_doc->filePath;
        ElecResultsDoc *oldDoc = s_doc;
        s_doc = new ElecResultsDoc();
        if (!s_doc->OpenDocument(path)) {
            delete s_doc;
            s_doc = oldDoc;
            throw std::runtime_error("Failed to reload: " + path);
        }
        register_elec_postproc_py(s_doc);
        auto cb = get_results_opened_callback();
        if (cb) cb(path.c_str(), s_doc);
        delete oldDoc;
    });

    m.def("eo_close", []() {
        check_doc(s_doc, EOERR);
        clear_elec_results_doc();
        auto cb = get_view_command_callback();
        if (cb) cb("eo_close", 0, nullptr);
        s_doc = nullptr;
    });

    // ---- Backward-compatible aliases ----
    m.alias("eo_selectline", "eo_selectpoint");
    m.alias("eo_showdensity", "eo_showdensityplot");
    m.alias("eo_hidedensity", "eo_hidedensityplot");
    m.alias("eo_showcontour", "eo_showcontourplot");
    m.alias("eo_hidecontour", "eo_hidecontourplot");
    m.alias("eo_vectorplot", "eo_showvectorplot");
    m.alias("eo_smoothing", "eo_smooth");
    m.alias("eo_getconductorprops", "eo_getconductorproperties");
}
