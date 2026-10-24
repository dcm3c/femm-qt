// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors
//
// py_mag_postproc.cpp - mo_* function implementations.
// Operates on ResultsDoc via femm_py::Module.
//
// Return-value mapping:
//   single value           -> CComplex / double / std::string
//   fixed N values          -> std::tuple<...>  (None if unavailable)
//   runtime-variable count  -> mp_obj_t tuple built by hand
//     (mo_lineintegral, mo_gapintegral, mo_getgapharmonics)
// The wire formatter renders a returned tuple as tab-separated values and a
// complex as re+I*im.
#include "py_mag_postproc.h"
#include "py_state.h"
#include "py_mag_editor.h"
#include "py_editor_common.h"
#include "py_postproc_common.h"
#include "results_doc.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <optional>
#include <string>
#include <tuple>

#ifndef PI
#define PI 3.141592653589793238462643383279
#endif

using femm_py::caster;
using std::optional;

static const char *MOERR = "No magnetics output in focus";

static ResultsDoc *s_rdoc = nullptr;

// A phasor at frequency, a plain number at DC. Every getter below uses this:
// a quantity that cannot have an imaginary part should not arrive carrying
// one. mo_blockintegral has applied the same rule for a while; these are the
// getters that never got it, which is why a DC force used to read (1234.5+0j).
// The TCP wire is unaffected -- its formatter already writes a zero-imaginary
// complex as a bare real, so the text every client parses is unchanged.
static mp_obj_t mag_num(CComplex v) {
    return (s_rdoc->Frequency == 0) ? femm_py::caster<double>::cast(v.re)
                                    : femm_py::caster<CComplex>::cast(v);
}
ResultsDoc *active_results_document() { return s_rdoc; }

static int findAGE(const char *name) {
    if (!s_rdoc || !name) return -1;
    for (int i = 0; i < (int)s_rdoc->agelist.size(); i++)
        if (s_rdoc->agelist[i].BdryName == name) return i;
    return -1;
}

void deactivate_mag_postproc_py() { s_rdoc = nullptr; }

void register_mag_postproc_py(ResultsDoc *doc) {
    s_rdoc = doc;
    femm_py::Module &m = femm_py::femm_module;

    // ---- Point values (14 values, or None) ----
    //
    // A, B, H, J and mu are phasors in an AC problem and plain numbers in a DC
    // one, so their Python type follows the problem: complex at frequency, real
    // at DC. Same rule mo_blockintegral applies -- a value that cannot have an
    // imaginary part should not arrive carrying one, which is what made
    // print() collapse 1+0j to 1 to hide it. The wire is unaffected: its
    // formatter already writes a zero-imaginary complex as a bare real.
    m.def("mo_getpointvalues", [](double px, double py) -> mp_obj_t {
        check_doc(s_rdoc, MOERR);
        ResultsDoc::PointVals u;
        if (!s_rdoc->GetPointValues(px, py, u)) return mp_const_none;
        auto D = [](double v) { return caster<double>::cast(v); };
        auto Z = [](CComplex v) { return mag_num(v); };
        mp_obj_t items[14] = { Z(u.A), Z(u.B1), Z(u.B2), D(u.c), D(u.E),
                               Z(u.H1), Z(u.H2), Z(u.Je), Z(u.Js),
                               Z(u.mu1), Z(u.mu2), D(u.Pe), D(u.Ph), D(u.ff) };
        return mp_obj_new_tuple(14, items);
    });

    // ---- Block selection / integration ----
    m.def("mo_selectblock", [](double px, double py) {
        check_doc(s_rdoc, MOERR);
        int k = s_rdoc->InTriangle(px, py);
        if (k >= 0) {
            int lbl = s_rdoc->meshelem[k].lbl;
            if (lbl >= 0 && lbl < (int)s_rdoc->labellist.size()) {
                s_rdoc->bHasMask = false;
                s_rdoc->labellist[lbl].ToggleSelect();
            }
        }
    });
    m.def("mo_groupselectblock", [](optional<int> grp) {
        check_doc(s_rdoc, MOERR);
        s_rdoc->bHasMask = false;
        if (!grp) {
            for (auto &lbl : s_rdoc->labellist) lbl.ToggleSelect();
        } else {
            for (auto &lbl : s_rdoc->labellist)
                if (lbl.InGroup == *grp) lbl.ToggleSelect();
        }
    });
    m.def("mo_clearblock", []() {
        check_doc(s_rdoc, MOERR);
        for (auto &lbl : s_rdoc->labellist) lbl.IsSelected = false;
    });
    m.def("mo_blockintegral", [](int type) -> mp_obj_t {
        check_doc(s_rdoc, MOERR);
        bool anySelected = false;
        for (auto &lbl : s_rdoc->labellist)
            if (lbl.IsSelected) { anySelected = true; break; }
        if (!anySelected) throw std::runtime_error("No area selected for integration");
        CComplex v = s_rdoc->BlockIntegral(type);
        if (s_rdoc->maskSelectionInvalid)
            throw std::runtime_error("The selected region is invalid. A valid selection "
                                     "cannot abut a region which is not free space.");
        // Return a plain float for real-valued quantities (everything at DC; the
        // real types at AC), complex only for genuinely-complex phasor quantities.
        // Keyed on the type (not the runtime value), so the Python return type is
        // stable per (type, frequency). See mo_blockintegral_cases.xlsx.
        if (s_rdoc->Frequency == 0 || !s_rdoc->BlockIntegralIsComplex(type))
            return caster<double>::cast(v.re);
        return caster<CComplex>::cast(v);
    });
    m.def("mo_lineintegral", [](int type) -> mp_obj_t {
        check_doc(s_rdoc, MOERR);
        CComplex z[4];
        s_rdoc->LineIntegral(type, z);
        auto C = [](CComplex v) { return caster<CComplex>::cast(v); };
        auto D = [](double v) { return caster<double>::cast(v); };
        switch (type) {
            // NOT mag_num: LineIntegral packs two real quantities into one
            // complex here (dividing a vector by the unit normal puts the normal
            // component in .re and the tangential one in .im), so the imaginary
            // part is meaningful at DC and must not be dropped.
            case 0: case 1: case 5: { mp_obj_t it[2] = {C(z[0]), C(z[1])}; return mp_obj_new_tuple(2, it); }
            case 2: { mp_obj_t it[2] = {D(z[0].re), D(z[0].im)}; return mp_obj_new_tuple(2, it); }
            case 3:
                if (s_rdoc->Frequency != 0) {
                    mp_obj_t it[4] = {D(z[2].re), D(z[3].re), C(z[0]), C(z[1])};
                    return mp_obj_new_tuple(4, it);
                } else {
                    mp_obj_t it[2] = {D(z[0].re), D(z[1].re)};
                    return mp_obj_new_tuple(2, it);
                }
            case 4:
                if (s_rdoc->Frequency != 0) {
                    mp_obj_t it[2] = {D(z[1].re), C(z[0])};
                    return mp_obj_new_tuple(2, it);
                } else {
                    mp_obj_t it[2] = {D(z[0].re), D(0.0)};
                    return mp_obj_new_tuple(2, it);
                }
        }
        return mp_obj_new_tuple(0, nullptr);
    });

    // ---- Circuit properties (amps, volts, flux) ----
    m.def("mo_getcircuitproperties", [](const std::string &name) -> mp_obj_t {
        check_doc(s_rdoc, MOERR);
        int k = -1;
        for (int i = 0; i < (int)s_rdoc->circproplist.size(); i++)
            if (s_rdoc->circproplist[i].CircName == name) { k = i; break; }
        if (k < 0) throw std::runtime_error("Unknown circuit " + name);
        mp_obj_t items[3] = { mag_num(s_rdoc->circproplist[k].Amps),
                              mag_num(s_rdoc->GetVoltageDrop(k)),
                              mag_num(s_rdoc->GetFluxLinkage(k)) };
        return mp_obj_new_tuple(3, items);
    });

    // ---- Contour ----
    m.def("mo_addcontour", [](double x, double y) {
        check_doc(s_rdoc, MOERR);
        CComplex pt(x, y);
        if (s_rdoc->contour.empty() || pt != s_rdoc->contour.back())
            s_rdoc->contour.push_back(pt);
    });
    m.def("mo_selectpoint", [](double mx, double my) {
        check_doc(s_rdoc, MOERR);
        ppSelectPoint(mx, my, s_rdoc);
    });
    m.def("mo_clearcontour", []() {
        check_doc(s_rdoc, MOERR);
        s_rdoc->contour.clear();
    });
    m.def("mo_bendcontour", [](double angle, optional<double> anglestep) {
        check_doc(s_rdoc, MOERR);
        s_rdoc->BendContour(angle, anglestep.value_or(1.0));
    });

    // ---- Mesh access ----
    m.def("mo_numnodes", []() -> double {
        check_doc(s_rdoc, MOERR);
        return (double)s_rdoc->meshnode.size();
    });
    m.def("mo_numelements", []() -> double {
        check_doc(s_rdoc, MOERR);
        return (double)s_rdoc->meshelem.size();
    });
    m.def("mo_getnode", [](int k_1) -> optional<std::tuple<double, double>> {
        check_doc(s_rdoc, MOERR);
        int k = k_1 - 1;
        if (k < 0 || k >= (int)s_rdoc->meshnode.size()) return std::nullopt;
        return std::make_tuple(s_rdoc->meshnode[k].x, s_rdoc->meshnode[k].y);
    });
    m.def("mo_getelement", [](int k_1)
              -> optional<std::tuple<double, double, double, double, double, double, double>> {
        check_doc(s_rdoc, MOERR);
        int k = k_1 - 1;
        if (k < 0 || k >= (int)s_rdoc->meshelem.size()) return std::nullopt;
        auto &el = s_rdoc->meshelem[k];
        int grp = (el.lbl >= 0 && el.lbl < (int)s_rdoc->labellist.size())
                      ? s_rdoc->labellist[el.lbl].InGroup : 0;
        return std::make_tuple((double)(el.p[0] + 1), (double)(el.p[1] + 1), (double)(el.p[2] + 1),
                               el.ctr.re, el.ctr.im, s_rdoc->ElmArea(k), (double)grp);
    });

    // ---- Bulk retrieval ----
    // These write directly to the wire result buffer (py_set_result) rather
    // than returning a std::string through the MicroPython GC heap.  A 1M-
    // element mesh produces ~80 MB of text, which would exceed any reasonable
    // GC heap size.
    // Returns FOUR typed columns rather than one float64 matrix: connectivity and
    // group are integers, and an all-double (ne,7) would both waste half its
    // memory on indices and hand back a float array that np.take refuses as a
    // gather index. capture_result() emits the tuple row-major, so the wire text
    // is unchanged. NODE INDICES ARE 1-BASED, as everywhere else in the API.
    m.def("mo_getallelements", []() -> mp_obj_t {
        check_doc(s_rdoc, MOERR);
        size_t ne = s_rdoc->meshelem.size();
        ndarray_obj_t *conn = bulkArray(ne, 3, NDARRAY_INT32);
        ndarray_obj_t *ctr  = bulkArray(ne, 2, NDARRAY_FLOAT);
        ndarray_obj_t *area = bulkArray(ne, 0, NDARRAY_FLOAT);
        ndarray_obj_t *grp  = bulkArray(ne, 0, NDARRAY_INT32);
        int32_t *pc = bulkI4(conn), *pg = bulkI4(grp);
        mp_float_t *pt = bulkF8(ctr), *pa = bulkF8(area);
        for (size_t k = 0; k < ne; k++) {
            auto &el = s_rdoc->meshelem[k];
            pc[3 * k]     = el.p[0] + 1;
            pc[3 * k + 1] = el.p[1] + 1;
            pc[3 * k + 2] = el.p[2] + 1;
            pt[2 * k]     = (mp_float_t)el.ctr.re;
            pt[2 * k + 1] = (mp_float_t)el.ctr.im;
            pa[k] = (mp_float_t)s_rdoc->ElmArea((int)k);
            pg[k] = (el.lbl >= 0 && el.lbl < (int)s_rdoc->labellist.size())
                        ? s_rdoc->labellist[el.lbl].InGroup : 0;
        }
        mp_obj_t items[4] = {MP_OBJ_FROM_PTR(conn), MP_OBJ_FROM_PTR(ctr),
                             MP_OBJ_FROM_PTR(area), MP_OBJ_FROM_PTR(grp)};
        return mp_obj_new_tuple(4, items);
    });
    m.def("mo_getallnodes", []() -> mp_obj_t {
        check_doc(s_rdoc, MOERR);
        size_t nn = s_rdoc->meshnode.size();
        ndarray_obj_t *a = bulkArray(nn, 2, NDARRAY_FLOAT);
        mp_float_t *d = bulkF8(a);
        for (size_t k = 0; k < nn; k++) {
            d[2 * k]     = (mp_float_t)s_rdoc->meshnode[k].x;
            d[2 * k + 1] = (mp_float_t)s_rdoc->meshnode[k].y;
        }
        return MP_OBJ_FROM_PTR(a);
    });
    // B1 and B2 are each a PHASOR, so the array is real at DC and complex at AC.
    m.def("mo_getallb", []() -> mp_obj_t {
        check_doc(s_rdoc, MOERR);
        size_t ne = s_rdoc->meshelem.size();
        bool ac = (s_rdoc->Frequency != 0);
        ndarray_obj_t *a = bulkArray(ne, 2, bulkFieldDtype(s_rdoc->Frequency));
        mp_float_t *d = bulkF8(a);
        for (size_t k = 0; k < ne; k++) {
            auto &el = s_rdoc->meshelem[k];
            if (ac) { bulkPutC(d, 2 * k, el.B1); bulkPutC(d, 2 * k + 1, el.B2); }
            else    { d[2 * k] = (mp_float_t)el.B1.re; d[2 * k + 1] = (mp_float_t)el.B2.re; }
        }
        return MP_OBJ_FROM_PTR(a);
    });
    m.def("mo_getalla", []() -> mp_obj_t {
        check_doc(s_rdoc, MOERR);
        size_t ne = s_rdoc->meshelem.size();
        bool ac = (s_rdoc->Frequency != 0);
        ndarray_obj_t *a = bulkArray(ne, 0, bulkFieldDtype(s_rdoc->Frequency));
        mp_float_t *d = bulkF8(a);
        for (size_t k = 0; k < ne; k++) {
            auto &el = s_rdoc->meshelem[k];
            CComplex Actr = (s_rdoc->meshnode[el.p[0]].A + s_rdoc->meshnode[el.p[1]].A +
                             s_rdoc->meshnode[el.p[2]].A) / 3.0;
            if (ac) bulkPutC(d, k, Actr);
            else    d[k] = (mp_float_t)Actr.re;
        }
        return MP_OBJ_FROM_PTR(a);
    });
    m.def("mo_getallgapb", [](const std::string &name, optional<int> opt_npts) -> mp_obj_t {
        check_doc(s_rdoc, MOERR);
        int idx = findAGE(name.c_str());
        if (idx < 0) return MP_OBJ_FROM_PTR(bulkArray(0, 2, NDARRAY_FLOAT));
        auto &age = s_rdoc->agelist[idx];
        int npts = opt_npts.value_or(360);
        if (npts < 1) npts = 1;
        bool ac = (s_rdoc->Frequency != 0);
        ndarray_obj_t *a = bulkArray((size_t)npts, 2, bulkFieldDtype(s_rdoc->Frequency));
        mp_float_t *d = bulkF8(a);
        for (int i = 0; i < npts; i++) {
            double tta = 2.0 * PI * i / npts;
            CComplex br(0, 0), bt(0, 0);
            for (int k = 0; k < age.nn; k++) {
                double n = age.nh[k];
                br = br + age.brc[k] * cos(n * tta) + age.brs[k] * sin(n * tta);
                bt = bt + age.btc[k] * cos(n * tta) + age.bts[k] * sin(n * tta);
            }
            if (ac) { bulkPutC(d, 2 * i, br); bulkPutC(d, 2 * i + 1, bt); }
            else    { d[2 * i] = (mp_float_t)br.re; d[2 * i + 1] = (mp_float_t)bt.re; }
        }
        return MP_OBJ_FROM_PTR(a);
    });
    m.def("mo_getallgapa", [](const std::string &name, optional<int> opt_npts) -> mp_obj_t {
        check_doc(s_rdoc, MOERR);
        int idx = findAGE(name.c_str());
        if (idx < 0) return MP_OBJ_FROM_PTR(bulkArray(0, 0, NDARRAY_FLOAT));
        auto &age = s_rdoc->agelist[idx];
        double R = (age.ri + age.ro) / 2.0;
        int npts = opt_npts.value_or(360);
        if (npts < 1) npts = 1;
        bool isAC = (s_rdoc->Frequency != 0);
        ndarray_obj_t *arr = bulkArray((size_t)npts, 0, bulkFieldDtype(s_rdoc->Frequency));
        mp_float_t *d = bulkF8(arr);
        for (int i = 0; i < npts; i++) {
            double tta = 2.0 * PI * i / npts;
            CComplex ac(0, 0);
            for (int k = 0; k < age.nn; k++) {
                double n = age.nh[k];
                if (n == 0) ac = ac + age.aco;
                else ac = ac + (R / n) * (-age.brs[k] * cos(n * tta) + age.brc[k] * sin(n * tta));
            }
            if (isAC) bulkPutC(d, (size_t)i, ac);
            else      d[i] = (mp_float_t)ac.re;
        }
        return MP_OBJ_FROM_PTR(arr);
    });
    m.def("mo_getallgapharmonics", [](const std::string &name) -> mp_obj_t {
        check_doc(s_rdoc, MOERR);
        int idx = findAGE(name.c_str());
        if (idx < 0) return MP_OBJ_FROM_PTR(bulkArray(0, 6, NDARRAY_FLOAT));
        auto &age = s_rdoc->agelist[idx];
        double R = (age.ri + age.ro) / 2.0;
        bool ac = (s_rdoc->Frequency != 0);
        ndarray_obj_t *arr = bulkArray((size_t)age.nn, 6, bulkFieldDtype(s_rdoc->Frequency));
        mp_float_t *d = bulkF8(arr);
        for (int k = 0; k < age.nn; k++) {
            double n = age.nh[k];
            CComplex acc(0, 0), acs(0, 0);
            if (n == 0) {
                acc = age.aco;
            } else {
                acc = -(R / n) * age.brs[k];
                acs = (R / n) * age.brc[k];
            }
            CComplex row[6] = {acc, acs, age.brc[k], age.brs[k], age.btc[k], age.bts[k]};
            for (int c = 0; c < 6; c++) {
                if (ac) bulkPutC(d, (size_t)k * 6 + c, row[c]);
                else    d[(size_t)k * 6 + c] = (mp_float_t)row[c].re;
            }
        }
        return MP_OBJ_FROM_PTR(arr);
    });

    // ---- Display control ----
    registerDisplayViewCmds(m, "mo");

    // Override: mo_showdensityplot(legend, gscale, upper_B, lower_B, type)
    m.def("mo_showdensityplot", [](optional<double> legend, optional<double> gscale,
                                   optional<double> ub, optional<double> lb,
                                   optional<std::string> type) -> std::string {
        check_doc(s_rdoc, MOERR);
        if (legend && (int)*legend == -1) {
            double a[1] = {-1};
            return ppViewCmd("mo_showdensity", 1, a);
        }
        if (!type) {  // fewer than 5 args
            double a[1] = {};
            return ppViewCmd("mo_showdensity", 0, a);
        }
        int legendV = (int)*legend, gscaleV = (int)*gscale;
        double ubV = *ub, lbV = *lb;
        const std::string &t = *type;
        int plotType = 0;
        bool ac = (s_rdoc->Frequency != 0);
        if (!ac) {
            if (t == "mag" || t == "bmag") plotType = 1;
            if (t == "real" || t == "breal") plotType = 1;
            if (t == "imag" || t == "bimag") plotType = 1;
            if (t == "hmag") plotType = 2;
            if (t == "hreal") plotType = 2;
            if (t == "himag") plotType = 2;
            if (t == "jmag") plotType = 3;
            if (t == "jreal") plotType = 3;
            if (t == "jimag") plotType = 3;
            if (t == "logb") plotType = 4;
        } else {
            if (t == "mag" || t == "bmag") plotType = 1;
            if (t == "real" || t == "breal") plotType = 2;
            if (t == "imag" || t == "bimag") plotType = 3;
            if (t == "hmag") plotType = 4;
            if (t == "hreal") plotType = 5;
            if (t == "himag") plotType = 6;
            if (t == "jmag") plotType = 7;
            if (t == "jreal") plotType = 8;
            if (t == "jimag") plotType = 9;
            if (t == "logb") plotType = 10;
        }
        double a[5] = {(double)legendV, (double)gscaleV, (double)plotType, ubV, lbV};
        return ppViewCmd("mo_showdensity", 5, a);
    });

    // Override: mo_showcontourplot(numcontours, lower_A, upper_A, type)
    m.def("mo_showcontourplot", [](optional<double> a0, optional<double> a1, optional<double> a2,
                                   optional<mp_obj_t> a3, optional<double> a4,
                                   optional<double> a5) -> std::string {
        check_doc(s_rdoc, MOERR);
        if (a0 && (int)*a0 == -1) {
            double a[1] = {-1};
            return ppViewCmd("mo_showcontour", 1, a);
        }
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
        return ppViewCmd("mo_showcontour", n, args);
    });

    auto smooth_fn = [](optional<std::string> mode) {
        check_doc(s_rdoc, MOERR);
        std::string s = mode.value_or("");
        for (auto &c : s) if (c >= 'A' && c <= 'Z') c = (char)(c + 32);  // MakeLower
        if (s == "off") s_rdoc->Smooth = false;
        else if (s == "on") s_rdoc->Smooth = true;
        else throw std::runtime_error("Unknown option for smoothing");
    };
    m.def("mo_smooth", smooth_fn);

    m.def("mo_setweightingscheme", [](int scheme) {
        check_doc(s_rdoc, MOERR);
        s_rdoc->WeightingScheme = scheme;
        s_rdoc->bHasMask = false;
    });

    m.def("mo_getprobleminfo", []() -> std::tuple<double, double, double, double> {
        check_doc(s_rdoc, MOERR);
        return std::make_tuple((double)s_rdoc->ProblemType, s_rdoc->Frequency, s_rdoc->Depth,
                               s_rdoc->LengthConv());
    });

    // OF's 4th argument selected among three text layouts (multi-column with
    // or without a legend, Mathematica-style) and, when omitted, meant "write
    // an extended metafile instead of data". femm-qt writes one data format
    // and has no metafile output, so the argument is accepted and ignored --
    // dropping it would turn every ported call into a TypeError.
    m.def("mo_makeplot", [](int plotType, optional<int> numPoints,
                            optional<std::string> filename,
                            optional<int> /*fileFormat*/) -> double {
        check_doc(s_rdoc, MOERR);
        return ppMakePlot(s_rdoc, plotType, numPoints.value_or(150),
                          filename ? fixpath(*filename) : std::string(),
                          xyexport::magPlotNames());
    });

    // ---- Air gap element functions ----
    m.def("mo_getgapb", [](const std::string &name, double angle_deg) -> mp_obj_t {
        check_doc(s_rdoc, MOERR);
        int idx = findAGE(name.c_str());
        CComplex br(0, 0), bt(0, 0);
        if (idx >= 0) {
            auto &age = s_rdoc->agelist[idx];
            double tta = angle_deg * PI / 180.0;
            for (int k = 0; k < age.nn; k++) {
                double n = age.nh[k];
                br = br + age.brc[k] * cos(n * tta) + age.brs[k] * sin(n * tta);
                bt = bt + age.btc[k] * cos(n * tta) + age.bts[k] * sin(n * tta);
            }
        }
        mp_obj_t items[2] = { mag_num(br), mag_num(bt) };
        return mp_obj_new_tuple(2, items);
    });

    m.def("mo_gapintegral", [](const std::string &name, int type) -> mp_obj_t {
        check_doc(s_rdoc, MOERR);
        auto C = [](CComplex v) { return caster<CComplex>::cast(v); };
        auto D = [](double v) { return caster<double>::cast(v); };
        int idx = findAGE(name.c_str());
        if (idx < 0) { mp_obj_t it[1] = {D(0.0)}; return mp_obj_new_tuple(1, it); }
        auto &age = s_rdoc->agelist[idx];
        double R = (age.ri + age.ro) / 2.0;

        if (type == 0) {  // DC torque
            double tq = 0;
            for (int k = 0; k < age.nn; k++)
                tq += (age.brc[k].re * age.btc[k].re + age.brc[k].im * age.btc[k].im +
                       age.brs[k].re * age.bts[k].re + age.brs[k].im * age.bts[k].im);
            tq *= PI * R * R * s_rdoc->Depth / muo;
            if (s_rdoc->Frequency != 0) tq /= 2.0;
            mp_obj_t it[1] = {D(tq)};
            return mp_obj_new_tuple(1, it);
        }
        if (type == 1) {  // DC force (360 deg gaps only)
            CComplex fx(0, 0), fy(0, 0);
            if (round(age.totalArcLength) == 360) {
                for (int k = 1; k < age.nn; k++) {
                    CComplex dfx = (age.brs[k] + age.btc[k]) * conj(age.brs[k - 1] - age.btc[k - 1]) +
                                   (age.brs[k - 1] - age.btc[k - 1]) * conj(age.brs[k] + age.btc[k]) +
                                   (age.brc[k] - age.bts[k]) * conj(age.brc[k - 1] + age.bts[k - 1]) +
                                   (age.brc[k - 1] + age.bts[k - 1]) * conj(age.brc[k] - age.bts[k]);
                    CComplex dfy = ((-age.brc[k] + age.bts[k]) * conj(age.brs[k - 1] - age.btc[k - 1]) +
                                    (age.brc[k - 1] + age.bts[k - 1]) * conj(age.brs[k] + age.btc[k]) +
                                    (age.brs[k] + age.btc[k]) * conj(age.brc[k - 1] + age.bts[k - 1]) +
                                    (-age.brs[k - 1] + age.btc[k - 1]) * conj(age.brc[k] - age.bts[k]));
                    fx = fx + CComplex(dfx.re, 0);
                    fy = fy + CComplex(dfy.re, 0);
                }
                fx = fx * s_rdoc->Depth * PI * R / (4.0 * muo);
                fy = fy * s_rdoc->Depth * PI * R / (4.0 * muo);
                if (s_rdoc->Frequency != 0) { fx = fx * 0.5; fy = fy * 0.5; }
            }
            mp_obj_t it[2] = {C(fx), C(fy)};
            return mp_obj_new_tuple(2, it);
        }
        if (type == 2) {  // stored energy
            double Ri = age.ri / R, Ro = age.ro / R;
            double dr = R * (Ro - Ri);
            CComplex W(0, 0);
            for (int k = 0; k < age.nn; k++) {
                int n = age.nh[k];
                if (n != 0)
                    W = W + (age.brs[k] * conj(age.brs[k]) + age.brc[k] * conj(age.brc[k]) +
                             age.bts[k] * conj(age.bts[k]) + age.btc[k] * conj(age.btc[k])) * dr;
                else
                    W = W + 2.0 * dr * age.btc[k] * conj(age.btc[k]);
            }
            W = CComplex(W.re * PI * R * s_rdoc->Depth / (2.0 * muo), 0);
            if (s_rdoc->Frequency != 0) W = W * 0.5;
            mp_obj_t it[1] = {C(W)};
            return mp_obj_new_tuple(1, it);
        }
        if (type == 3) {  // 2X torque
            CComplex tq(0, 0);
            if (s_rdoc->Frequency != 0) {
                for (int k = 0; k < age.nn; k++)
                    tq = tq + age.brc[k] * age.btc[k] + age.brs[k] * age.bts[k];
                tq = tq * PI * R * R * s_rdoc->Depth / (2.0 * muo);
            }
            mp_obj_t it[1] = {C(tq)};
            return mp_obj_new_tuple(1, it);
        }
        if (type == 4) {  // 2X force
            CComplex fx(0, 0), fy(0, 0);
            if (round(age.totalArcLength) == 360 && s_rdoc->Frequency != 0) {
                for (int k = 1; k < age.nn; k++) {
                    CComplex dfx = (age.brs[k - 1] - age.btc[k - 1]) * (age.brs[k] + age.btc[k]) +
                                   (age.brc[k - 1] + age.bts[k - 1]) * (age.brc[k] - age.bts[k]);
                    CComplex dfy = (age.brs[k] + age.btc[k]) * (age.brc[k - 1] + age.bts[k - 1]) -
                                   (age.brs[k - 1] - age.btc[k - 1]) * (age.brc[k] - age.bts[k]);
                    fx = fx + dfx;
                    fy = fy + dfy;
                }
                fx = fx * s_rdoc->Depth * PI * R / (4.0 * muo);
                fy = fy * s_rdoc->Depth * PI * R / (4.0 * muo);
            }
            mp_obj_t it[2] = {C(fx), C(fy)};
            return mp_obj_new_tuple(2, it);
        }
        if (type == 5) {  // incremental torque
            CComplex tq(0, 0);
            for (int k = 0; k < age.nn; k++)
                tq = tq + age.btcPrev[k] * age.brc[k] + age.brcPrev[k] * age.btc[k] +
                          age.btsPrev[k] * age.brs[k] + age.brsPrev[k] * age.bts[k];
            tq = tq * PI * R * R * s_rdoc->Depth / muo;
            mp_obj_t it[1] = {C(tq)};
            return mp_obj_new_tuple(1, it);
        }
        if (type == 6) {  // incremental force
            CComplex fx(0, 0), fy(0, 0);
            if (round(age.totalArcLength) == 360 && s_rdoc->Frequency != 0) {
                for (int k = 1; k < age.nn; k++) {
                    CComplex dfx = (age.brs[k] + age.btc[k]) * (age.brsPrev[k - 1] - age.btcPrev[k - 1]) +
                                   (age.brs[k - 1] - age.btc[k - 1]) * (age.brsPrev[k] + age.btcPrev[k]) +
                                   (age.brc[k] - age.bts[k]) * (age.brcPrev[k - 1] + age.btsPrev[k - 1]) +
                                   (age.brc[k - 1] + age.bts[k - 1]) * (age.brcPrev[k] - age.btsPrev[k]);
                    CComplex dfy = (age.brsPrev[k] + age.btcPrev[k]) * (age.brc[k - 1] + age.bts[k - 1]) -
                                   (age.brsPrev[k - 1] - age.btcPrev[k - 1]) * (age.brc[k] - age.bts[k]) +
                                   (age.brs[k] + age.btc[k]) * (age.brcPrev[k - 1] + age.btsPrev[k - 1]) -
                                   (age.brs[k - 1] - age.btc[k - 1]) * (age.brcPrev[k] - age.btsPrev[k]);
                    fx = fx + dfx;
                    fy = fy + dfy;
                }
                fx = fx * s_rdoc->Depth * PI * R / (2.0 * muo);
                fy = fy * s_rdoc->Depth * PI * R / (2.0 * muo);
            }
            mp_obj_t it[2] = {C(fx), C(fy)};
            return mp_obj_new_tuple(2, it);
        }
        mp_obj_t it[1] = {D(0.0)};
        return mp_obj_new_tuple(1, it);
    });

    m.def("mo_getgapa", [](const std::string &name, double angle_deg) -> mp_obj_t {
        check_doc(s_rdoc, MOERR);
        int idx = findAGE(name.c_str());
        CComplex ac(0, 0);
        if (idx >= 0) {
            auto &age = s_rdoc->agelist[idx];
            double R = (age.ri + age.ro) / 2.0;
            double tta = angle_deg * PI / 180.0;
            for (int k = 0; k < age.nn; k++) {
                double n = age.nh[k];
                if (n == 0) ac = ac + age.aco;
                else ac = ac + (R / n) * (-age.brs[k] * cos(n * tta) + age.brc[k] * sin(n * tta));
            }
        }
        return mag_num(ac);
    });

    m.def("mo_getgapharmonics", [](const std::string &name, optional<double> opt_n) -> mp_obj_t {
        check_doc(s_rdoc, MOERR);
        auto C = [](CComplex v) { return caster<CComplex>::cast(v); };
        int idx = findAGE(name.c_str());
        if (idx < 0) return mp_obj_new_tuple(0, nullptr);
        auto &age = s_rdoc->agelist[idx];

        if (!opt_n) {  // just name: highest harmonic number
            double hi = (age.nn == 0) ? 0.0 : (double)age.nh[age.nn - 1];
            return caster<double>::cast(hi);
        }
        if (age.nn == 0) return mp_obj_new_tuple(0, nullptr);
        double n = *opt_n;
        if (n < 0) return mp_obj_new_tuple(0, nullptr);

        int k;
        for (k = 0; k < age.nn; k++)
            if (age.nh[k] == (int)n) break;

        CComplex acc(0, 0), acs(0, 0), brc(0, 0), brs(0, 0), btc(0, 0), bts(0, 0);
        if (k < age.nn) {
            double R = (age.ri + age.ro) / 2.0;
            if (n == 0) {
                acc = age.aco;
            } else {
                acc = -(R / n) * age.brs[k];
                acs = (R / n) * age.brc[k];
                brc = age.brc[k];
                brs = age.brs[k];
                btc = age.btc[k];
                bts = age.bts[k];
            }
        }
        mp_obj_t it[6] = {C(acc), C(acs), C(brc), C(brs), C(btc), C(bts)};
        return mp_obj_new_tuple(6, it);
    });

    m.def("mo_gettitle", []() -> std::string {
        check_doc(s_rdoc, MOERR);
        return s_rdoc->filePath;
    });

    // ---- GUI view commands (forward up to 6 args; result string or "") ----
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
    regv("mo_zoomnatural");
    regv("mo_zoomout");
    regv("mo_zoomin");
    regv("mo_zoom");
    regv("mo_refreshview");
    regv("mo_showgrid");
    regv("mo_hidegrid");
    // The string flag this used to parse inline is now gridSnapFlag in
    // py_editor_common.h, shared with the seven commands that lacked it.
    m.def("mo_gridsnap", [](const std::string &flag) {
        double a[1] = {gridSnapFlag(flag)};
        ppViewCmd("mo_gridsnap", 1, a);
    });
    registerSetGrid(m, "mo", s_rdoc, MOERR);
    regv("mo_resize");
    regv("mo_minimize");
    regv("mo_maximize");
    regv("mo_restore");

    m.def("mo_setfocus", [](optional<std::string> docname) {
        // Magnetics results carry the .ans extension; normalize so a bare "temp"
        // matches the "temp.ans" tab label (OF strips .ans from both sides).
        std::string dn;
        if (docname) {
            dn = canonicalDocName(*docname, ".ans");
            doc_registry_activate_results_byname(dn);
        }
        auto cb = get_view_command_callback();
        if (!cb) return;
        double args[4] = {};
        if (docname)
            cb(("mo_setfocus:" + dn).c_str(), 0, args);
        else
            cb("mo_setfocus", 0, args);
    });

    m.def("mo_savemetafile", [](std::optional<std::string>) {});  // No metafile output in femm-qt;
    // the filename is accepted and ignored so ported scripts do not raise.
    // Use *_savebitmap to save a picture of the view.

    m.def("mo_seteditmode", [](const std::string &mode) {
        auto cb = get_view_command_callback();
        if (!cb) return;
        double md = 0;
        if (!mode.empty()) {
            if (mode[0] == 'c' || mode[0] == 'C') md = 1;       // contour
            else if (mode[0] == 'a' || mode[0] == 'A') md = 2;  // area
        }
        double args[4] = {md, 0, 0, 0};
        cb("mo_seteditmode", 1, args);
    });

    m.def("mo_savebitmap", [](const std::string &fname) {
        auto cb = get_view_command_callback();
        if (!cb) return;
        std::string fp = fixpath(fname);
        double args[4] = {};
        cb(("mo_savebitmap:" + fp).c_str(), 0, args);
    });

    m.def("mo_reload", []() {
        check_doc(s_rdoc, MOERR);
        if (s_rdoc->filePath.empty()) return;
        std::string path = s_rdoc->filePath;
        ResultsDoc *oldDoc = s_rdoc;
        s_rdoc = new ResultsDoc();
        if (!s_rdoc->OpenDocument(path)) {
            delete s_rdoc;
            s_rdoc = oldDoc;
            throw std::runtime_error("Failed to reload: " + path);
        }
        register_mag_postproc_py(s_rdoc);
        // Swap the doc everywhere it is known BEFORE freeing the old one. The
        // registry and the editor's cache used to keep the freed doc: the next
        // mi_loadsolution deleted it again (crashed the GUI), headless exit
        // cleanup deleted it again (crashed every reload-then-exit run), and
        // a lookup by name read it. Removing the old doc from the registry
        // runs the deactivate hook, which only matches the ACTIVE doc -- now
        // the new one -- so it leaves the new binding alone.
        doc_registry_add(s_rdoc);
        replace_mag_results_doc(oldDoc, s_rdoc);
        auto cb = get_results_opened_callback();
        if (cb) cb(path.c_str(), s_rdoc);
        doc_registry_remove(oldDoc);
        delete oldDoc;
    });

    // In the GUI the document belongs to its window, whose destructor frees
    // it. Headless there is no window, so close frees it here, as *_close in
    // py_editor_common.h does for input documents. Only dropping the pointer
    // leaked the whole solution on every close: a batch of load/close passes
    // over TCP grew without bound. Take the pointer first: doc_registry_remove
    // runs the deactivate hook, which nulls s_rdoc, so `delete s_rdoc` after
    // it deletes nothing.
    m.def("mo_close", []() {
        check_doc(s_rdoc, MOERR);
        ResultsDoc *doc = s_rdoc;
        clear_mag_results_doc();
        auto cb = get_view_command_callback();
        if (cb) cb("mo_close", 0, nullptr);
        else { doc_registry_remove(doc); delete doc; }
        s_rdoc = nullptr;
    });

    // ---- Backward-compatible aliases (old short names) ----
    m.alias("mo_showdensity", "mo_showdensityplot");
    m.alias("mo_hidedensity", "mo_hidedensityplot");
    m.alias("mo_showcontour", "mo_showcontourplot");
    m.alias("mo_hidecontour", "mo_hidecontourplot");
    m.alias("mo_vectorplot", "mo_showvectorplot");
    m.alias("mo_smoothing", "mo_smooth");
}
