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
ResultsDoc *active_results_document() { return s_rdoc; }

static int findAGE(const char *name) {
    if (!s_rdoc || !name) return -1;
    for (int i = 0; i < (int)s_rdoc->agelist.size(); i++)
        if (s_rdoc->agelist[i].BdryName == name) return i;
    return -1;
}

// Flat tab-separated serialization helpers (same %.17g / re+I*im format).
static void appendComplex(std::string &buf, CComplex z) {
    char c[80];
    z.ToString(c);
    buf += c;
    buf += '\t';
}
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

void register_mag_postproc_py(ResultsDoc *doc) {
    s_rdoc = doc;
    femm_py::Module &m = femm_py::femm_module;

    // ---- Point values (14 values, or None) ----
    using PV = std::tuple<CComplex, CComplex, CComplex, double, double, CComplex, CComplex,
                          CComplex, CComplex, CComplex, CComplex, double, double, double>;
    m.def("mo_getpointvalues", [](double px, double py) -> optional<PV> {
        check_doc(s_rdoc, MOERR);
        ResultsDoc::PointVals u;
        if (!s_rdoc->GetPointValues(px, py, u)) return std::nullopt;
        return PV{u.A, u.B1, u.B2, u.c, u.E, u.H1, u.H2, u.Je, u.Js, u.mu1, u.mu2, u.Pe, u.Ph, u.ff};
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
    m.def("mo_blockintegral", [](int type) -> CComplex {
        check_doc(s_rdoc, MOERR);
        bool anySelected = false;
        for (auto &lbl : s_rdoc->labellist)
            if (lbl.IsSelected) { anySelected = true; break; }
        if (!anySelected) throw std::runtime_error("No area selected for integration");
        return s_rdoc->BlockIntegral(type);
    });
    m.def("mo_lineintegral", [](int type) -> mp_obj_t {
        check_doc(s_rdoc, MOERR);
        CComplex z[4];
        s_rdoc->LineIntegral(type, z);
        auto C = [](CComplex v) { return caster<CComplex>::cast(v); };
        auto D = [](double v) { return caster<double>::cast(v); };
        switch (type) {
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
    m.def("mo_getcircuitproperties", [](const std::string &name)
              -> std::tuple<CComplex, CComplex, CComplex> {
        check_doc(s_rdoc, MOERR);
        int k = -1;
        for (int i = 0; i < (int)s_rdoc->circproplist.size(); i++)
            if (s_rdoc->circproplist[i].CircName == name) { k = i; break; }
        if (k < 0) throw std::runtime_error("Unknown circuit " + name);
        return std::make_tuple(s_rdoc->circproplist[k].Amps, s_rdoc->GetVoltageDrop(k),
                               s_rdoc->GetFluxLinkage(k));
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
    m.def("mo_getallelements", []() {
        check_doc(s_rdoc, MOERR);
        int ne = (int)s_rdoc->meshelem.size();
        std::string buf;
        buf.reserve(ne * 70);
        for (int k = 0; k < ne; k++) {
            auto &el = s_rdoc->meshelem[k];
            int grp = (el.lbl >= 0 && el.lbl < (int)s_rdoc->labellist.size())
                          ? s_rdoc->labellist[el.lbl].InGroup : 0;
            appendInt(buf, el.p[0] + 1);
            appendInt(buf, el.p[1] + 1);
            appendInt(buf, el.p[2] + 1);
            appendDouble(buf, el.ctr.re);
            appendDouble(buf, el.ctr.im);
            appendDouble(buf, s_rdoc->ElmArea(k));
            appendInt(buf, grp);
        }
        if (!buf.empty()) buf.pop_back();
        py_set_result(std::move(buf));
    });
    m.def("mo_getallnodes", []() {
        check_doc(s_rdoc, MOERR);
        int nn = (int)s_rdoc->meshnode.size();
        std::string buf;
        buf.reserve(nn * 50);
        for (int k = 0; k < nn; k++) {
            appendDouble(buf, s_rdoc->meshnode[k].x);
            appendDouble(buf, s_rdoc->meshnode[k].y);
        }
        if (!buf.empty()) buf.pop_back();
        py_set_result(std::move(buf));
    });
    m.def("mo_getallb", []() {
        check_doc(s_rdoc, MOERR);
        int ne = (int)s_rdoc->meshelem.size();
        std::string buf;
        buf.reserve(ne * 50);
        for (int k = 0; k < ne; k++) {
            appendComplex(buf, s_rdoc->meshelem[k].B1);
            appendComplex(buf, s_rdoc->meshelem[k].B2);
        }
        if (!buf.empty()) buf.pop_back();
        py_set_result(std::move(buf));
    });
    m.def("mo_getalla", []() {
        check_doc(s_rdoc, MOERR);
        int ne = (int)s_rdoc->meshelem.size();
        std::string buf;
        buf.reserve(ne * 30);
        for (int k = 0; k < ne; k++) {
            auto &el = s_rdoc->meshelem[k];
            CComplex Actr = (s_rdoc->meshnode[el.p[0]].A + s_rdoc->meshnode[el.p[1]].A +
                             s_rdoc->meshnode[el.p[2]].A) / 3.0;
            appendComplex(buf, Actr);
        }
        if (!buf.empty()) buf.pop_back();
        py_set_result(std::move(buf));
    });
    m.def("mo_getallgapb", [](const std::string &name, optional<int> opt_npts) {
        check_doc(s_rdoc, MOERR);
        int idx = findAGE(name.c_str());
        if (idx < 0) { py_set_result(""); return; }
        auto &age = s_rdoc->agelist[idx];
        int npts = opt_npts.value_or(360);
        if (npts < 1) npts = 1;
        std::string buf;
        buf.reserve(npts * 60);
        for (int i = 0; i < npts; i++) {
            double tta = 2.0 * PI * i / npts;
            CComplex br(0, 0), bt(0, 0);
            for (int k = 0; k < age.nn; k++) {
                double n = age.nh[k];
                br = br + age.brc[k] * cos(n * tta) + age.brs[k] * sin(n * tta);
                bt = bt + age.btc[k] * cos(n * tta) + age.bts[k] * sin(n * tta);
            }
            appendComplex(buf, br);
            appendComplex(buf, bt);
        }
        if (!buf.empty()) buf.pop_back();
        py_set_result(std::move(buf));
    });
    m.def("mo_getallgapa", [](const std::string &name, optional<int> opt_npts) {
        check_doc(s_rdoc, MOERR);
        int idx = findAGE(name.c_str());
        if (idx < 0) { py_set_result(""); return; }
        auto &age = s_rdoc->agelist[idx];
        double R = (age.ri + age.ro) / 2.0;
        int npts = opt_npts.value_or(360);
        if (npts < 1) npts = 1;
        std::string buf;
        buf.reserve(npts * 30);
        for (int i = 0; i < npts; i++) {
            double tta = 2.0 * PI * i / npts;
            CComplex ac(0, 0);
            for (int k = 0; k < age.nn; k++) {
                double n = age.nh[k];
                if (n == 0) ac = ac + age.aco;
                else ac = ac + (R / n) * (-age.brs[k] * cos(n * tta) + age.brc[k] * sin(n * tta));
            }
            appendComplex(buf, ac);
        }
        if (!buf.empty()) buf.pop_back();
        py_set_result(std::move(buf));
    });
    m.def("mo_getallgapharmonics", [](const std::string &name) {
        check_doc(s_rdoc, MOERR);
        int idx = findAGE(name.c_str());
        if (idx < 0) { py_set_result(""); return; }
        auto &age = s_rdoc->agelist[idx];
        double R = (age.ri + age.ro) / 2.0;
        std::string buf;
        buf.reserve(age.nn * 150);
        for (int k = 0; k < age.nn; k++) {
            double n = age.nh[k];
            CComplex acc(0, 0), acs(0, 0);
            if (n == 0) {
                acc = age.aco;
            } else {
                acc = -(R / n) * age.brs[k];
                acs = (R / n) * age.brc[k];
            }
            appendComplex(buf, acc);
            appendComplex(buf, acs);
            appendComplex(buf, age.brc[k]);
            appendComplex(buf, age.brs[k]);
            appendComplex(buf, age.btc[k]);
            appendComplex(buf, age.bts[k]);
        }
        if (!buf.empty()) buf.pop_back();
        py_set_result(std::move(buf));
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

    m.def("mo_makeplot", [](int plotType, optional<int> numPoints,
                            optional<std::string> filename) -> double {
        check_doc(s_rdoc, MOERR);
        return ppMakePlot(s_rdoc, plotType, numPoints.value_or(150),
                          filename ? fixpath(*filename) : std::string());
    });

    // ---- Air gap element functions ----
    m.def("mo_getgapb", [](const std::string &name, double angle_deg)
              -> std::tuple<CComplex, CComplex> {
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
        return std::make_tuple(br, bt);
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

    m.def("mo_getgapa", [](const std::string &name, double angle_deg) -> CComplex {
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
        return ac;
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
    // mo_gridsnap("on"/"off"): parse to a numeric flag (the view callback only
    // carries doubles). Matches OF lua_gridsnap, incl. the bad-input error.
    m.def("mo_gridsnap", [](const std::string &flag) {
        std::string s = flag;
        for (auto &c : s) if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
        double on;
        if (s == "on") on = 1;
        else if (s == "off") on = 0;
        else throw std::runtime_error("Unknown option for grid snap");
        double a[1] = {on};
        ppViewCmd("mo_gridsnap", 1, a);
    });
    regv("mo_setgrid");
    regv("mo_resize");
    regv("mo_minimize");
    regv("mo_maximize");
    regv("mo_restore");

    m.def("mo_setfocus", [](optional<std::string> docname) {
        if (docname) doc_registry_activate_results_byname(*docname);
        auto cb = get_view_command_callback();
        if (!cb) return;
        double args[4] = {};
        if (docname)
            cb(("mo_setfocus:" + *docname).c_str(), 0, args);
        else
            cb("mo_setfocus", 0, args);
    });

    m.def("mo_savemetafile", []() {});  // deprecated no-op

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
        auto cb = get_results_opened_callback();
        if (cb) cb(path.c_str(), s_rdoc);
        delete oldDoc;
    });

    m.def("mo_close", []() {
        check_doc(s_rdoc, MOERR);
        clear_mag_results_doc();
        auto cb = get_view_command_callback();
        if (cb) cb("mo_close", 0, nullptr);
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
