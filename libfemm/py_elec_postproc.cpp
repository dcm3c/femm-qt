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
    // Same shape as mo_getallelements: four typed columns, emitted row-major on
    // the wire by capture_result(). Node indices are 1-based.
    m.def("eo_getallelements", []() -> mp_obj_t {
        check_doc(s_doc, EOERR);
        size_t ne = s_doc->meshelem.size();
        ndarray_obj_t *conn = bulkArray(ne, 3, NDARRAY_INT32);
        ndarray_obj_t *ctr  = bulkArray(ne, 2, NDARRAY_FLOAT);
        ndarray_obj_t *area = bulkArray(ne, 0, NDARRAY_FLOAT);
        ndarray_obj_t *grp  = bulkArray(ne, 0, NDARRAY_INT32);
        int32_t *pc = bulkI4(conn), *pg = bulkI4(grp);
        mp_float_t *pt = bulkF8(ctr), *pa = bulkF8(area);
        for (size_t k = 0; k < ne; k++) {
            auto &el = s_doc->meshelem[k];
            pc[3 * k]     = el.p[0] + 1;
            pc[3 * k + 1] = el.p[1] + 1;
            pc[3 * k + 2] = el.p[2] + 1;
            pt[2 * k]     = (mp_float_t)el.ctr.re;
            pt[2 * k + 1] = (mp_float_t)el.ctr.im;
            pa[k] = (mp_float_t)s_doc->ElmArea((int)k);
            pg[k] = (el.lbl >= 0 && el.lbl < (int)s_doc->labellist.size())
                        ? s_doc->labellist[el.lbl].InGroup : 0;
        }
        mp_obj_t items[4] = {MP_OBJ_FROM_PTR(conn), MP_OBJ_FROM_PTR(ctr),
                             MP_OBJ_FROM_PTR(area), MP_OBJ_FROM_PTR(grp)};
        return mp_obj_new_tuple(4, items);
    });
    m.def("eo_getallnodes", []() -> mp_obj_t {
        check_doc(s_doc, EOERR);
        size_t nn = s_doc->meshnode.size();
        ndarray_obj_t *a = bulkArray(nn, 2, NDARRAY_FLOAT);
        mp_float_t *d = bulkF8(a);
        for (size_t k = 0; k < nn; k++) {
            d[2 * k]     = (mp_float_t)s_doc->meshnode[k].x;
            d[2 * k + 1] = (mp_float_t)s_doc->meshnode[k].y;
        }
        return MP_OBJ_FROM_PTR(a);
    });

    // ---- Field and potential, the analogues of mo_getallb / mo_getalla ----
    //
    // ERElement::D is ONE CComplex holding the 2-D displacement VECTOR, re = Dx
    // and im = Dy -- not a phasor. Electrostatics has no frequency, so this is
    // two REAL columns; flattening it as a complex would put "Dx+Dy*I" on the
    // wire where two numbers belong. Same convention the line integrals use for
    // normal/tangential components.
    m.def("eo_getalld", []() -> mp_obj_t {
        check_doc(s_doc, EOERR);
        size_t ne = s_doc->meshelem.size();
        ndarray_obj_t *a = bulkArray(ne, 2, NDARRAY_FLOAT);
        mp_float_t *d = bulkF8(a);
        for (size_t k = 0; k < ne; k++) {
            d[2 * k]     = (mp_float_t)s_doc->meshelem[k].D.re;
            d[2 * k + 1] = (mp_float_t)s_doc->meshelem[k].D.im;
        }
        return MP_OBJ_FROM_PTR(a);
    });
    // Nodal voltage. Per NODE, unlike the element-centred field getters.
    m.def("eo_getallv", []() -> mp_obj_t {
        check_doc(s_doc, EOERR);
        size_t nn = s_doc->meshnode.size();
        ndarray_obj_t *a = bulkArray(nn, 0, NDARRAY_FLOAT);
        mp_float_t *d = bulkF8(a);
        for (size_t k = 0; k < nn; k++) d[k] = (mp_float_t)s_doc->meshnode[k].V;
        return MP_OBJ_FROM_PTR(a);
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

    // OF's 4th argument selected among three text layouts (multi-column with
    // or without a legend, Mathematica-style) and, when omitted, meant "write
    // an extended metafile instead of data". femm-qt writes one data format
    // and has no metafile output, so the argument is accepted and ignored --
    // dropping it would turn every ported call into a TypeError.
    m.def("eo_makeplot", [](int plotType, optional<int> numPoints,
                            optional<std::string> filename,
                            optional<int> /*fileFormat*/) -> double {
        check_doc(s_doc, EOERR);
        return ppMakePlot(s_doc, plotType, numPoints.value_or(150),
                          filename ? fixpath(*filename) : std::string(),
                          xyexport::elecPlotNames());
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
    // Takes the documented string flag, not a double -- see
    // gridSnapFlag in py_editor_common.h.
    m.def("eo_gridsnap", [](const std::string &flag) {
        double a[1] = {gridSnapFlag(flag)};
        ppViewCmd("eo_gridsnap", 1, a);
    });
    registerSetGrid(m, "eo", s_doc, EOERR);
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
    m.def("eo_savemetafile", [](std::optional<std::string>) {});  // No metafile output in femm-qt;
    // the filename is accepted and ignored so ported scripts do not raise.
    // Use *_savebitmap to save a picture of the view.

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
        doc_registry_add(s_doc);                  // see mo_reload
        replace_elec_results_doc(oldDoc, s_doc);
        auto cb = get_results_opened_callback();
        if (cb) cb(path.c_str(), s_doc);
        doc_registry_remove(oldDoc);
        delete oldDoc;
    });

    m.def("eo_close", []() {
        check_doc(s_doc, EOERR);
        clear_elec_results_doc();
        auto cb = get_view_command_callback();
        if (cb) cb("eo_close", 0, nullptr);
        else { auto *doc = s_doc; doc_registry_remove(doc); delete doc; }  // see mo_close
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
