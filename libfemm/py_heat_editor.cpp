// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors
//
// py_heat_editor.cpp - hi_* function implementations.
// Operates on HeatDocument. All values real.
#include "py_heat_editor.h"
#include "utf8_io.h"
#include "material_lib_path.h"
#include "py_heat_postproc.h"
#include "py_editor_common.h"
#include "heat_document.h"
#include "heat_results_doc.h"
#include "dxf_io.h"
#include "femm_makeabc.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <tuple>

#ifndef PI
#define PI 3.141592653589793238462643383279
#endif
#define MINANGLE_MAX 33.8

using femm_py::caster;
using std::optional;

static const char *HERR = "No current heat flow input in focus";

static HeatDocument *s_doc = nullptr;
static HeatResultsDoc *s_resultsDoc = nullptr;

HeatDocument *active_heat_document() { return s_doc; }
void deactivate_heat_editor_py() { s_doc = nullptr; }
void clear_heat_results_doc() { s_resultsDoc = nullptr; }

static int s_editAction = 0;

static char *hi_StripKey(char *s) {
    char *p = strchr(s, '=');
    return p ? p + 1 : s;
}
static std::string hi_ExtractQuoted(char *v) {
    char *start = strchr(v, '"');
    if (!start) return std::string(v);
    start++;
    char *end = strrchr(start, '"');
    if (end) *end = 0;
    return std::string(start);
}

void register_heat_editor_py(HeatDocument *doc) {
    s_doc = doc;
    femm_py::Module &m = femm_py::femm_module;

    register_common_editor(m, "hi", s_doc, HERR, s_editAction);

    // Improvised Asymptotic Boundary Conditions (open boundary). Native C++
    // (femm_makeabc.cpp), shared with the Lua tree; replaces the former Python
    // _make_abc helper in the init script. All five args optional.
    m.def("hi_makeABC", [](optional<int> n, optional<double> R, optional<double> x,
                           optional<double> y, optional<int> bc) {
        check_doc(s_doc, HERR);
        MakeABCParams p;
        p.n = n; p.R = R; p.x = x; p.y = y; p.bctype = bc;
        heat_makeABC(*s_doc, p);
    });

    // ---- Problem definition (units, type, precision, depth, minangle,
    //      prevsoln, dT) - heat flow supports transient previous solution ----
    m.def("hi_probdef", [](const std::string &units, const std::string &ptype, double prec,
                           optional<double> depth, optional<double> minangle,
                           optional<std::string> prevsolnOpt, optional<double> dTOpt) {
        check_doc(s_doc, HERR);
        int lu = -1;
        if (units == "inches") lu = 0;
        else if (units == "millimeters") lu = 1;
        else if (units == "centimeters") lu = 2;
        else if (units == "meters") lu = 3;
        else if (units == "mills" || units == "mils") lu = 4;
        else if (units == "micrometers") lu = 5;
        if (lu == -1) throw std::runtime_error("Unknown length unit " + units);
        s_doc->LengthUnits = lu;
        if (ptype == "planar") s_doc->ProblemType = 0;
        else if (ptype == "axi") s_doc->ProblemType = 1;
        else throw std::runtime_error("Unknown problem type " + ptype);
        double p = prec;
        if (p == 0 || p > 1.e-8) p = 1.e-8;
        if (p < 1.e-16) p = 1.e-16;
        s_doc->Precision = p;
        if (depth) s_doc->Depth = *depth;
        if (s_doc->Depth <= 0) s_doc->Depth = 1;
        if (minangle) {
            double ma = *minangle;
            if (ma >= 1.0 && ma <= MINANGLE_MAX) s_doc->MinAngle = ma;
        }
        std::string prevsoln = prevsolnOpt ? fixpath(*prevsolnOpt) : std::string();
        double dT = dTOpt.value_or(0.0);
        if (prevsoln.empty()) dT = 0;
        if (dT == 0) prevsoln = "";
        s_doc->PrevSoln = prevsoln;
        s_doc->dT = dT;
    });

    // ---- Set properties on selected ----
    m.def("hi_setnodeprop", [](const std::string &nodeprop, int groupno,
                               optional<std::string> inconductor) {
        check_doc(s_doc, HERR);
        std::string inc = inconductor.value_or("<None>");
        for (auto &nd : s_doc->nodelist) {
            if (nd.IsSelected) {
                nd.InGroup = groupno;
                nd.BoundaryMarker = nodeprop;
                nd.InConductor = inc;
            }
        }
    });

    m.def("hi_setblockprop", [](optional<std::string> blocktype, optional<double> automesh,
                                optional<double> meshsize, optional<double> group) {
        check_doc(s_doc, HERR);
        std::string bt = "<None>";
        int am = 1;
        double ms = 0;
        int grp = 0;
        if (blocktype) bt = *blocktype;
        if (automesh) am = (int)*automesh;
        if (meshsize) ms = *meshsize;
        if (group) grp = (int)*group;
        for (auto &b : s_doc->blocklist) {
            if (b.IsSelected) {
                b.MaxArea = PI * ms * ms / 4.0;
                b.BlockType = bt;
                b.InGroup = grp;
                if (am == 1) b.MaxArea = 0;
            }
        }
    });

    m.def("hi_setsegmentprop", [](const std::string &prop, double elesize, int automesh,
                                  int hide, int group, optional<std::string> inconductor) {
        check_doc(s_doc, HERR);
        std::string inc = inconductor.value_or("<None>");
        for (auto &seg : s_doc->linelist) {
            if (seg.IsSelected) {
                if (automesh == 1) seg.MaxSideLength = -1;
                else if (elesize > 0) seg.MaxSideLength = elesize;
                else seg.MaxSideLength = -1;
                seg.BoundaryMarker = prop;
                seg.Hidden = (hide != 0);
                seg.InGroup = group;
                seg.InConductor = inc;
            }
        }
    });

    m.def("hi_setarcsegmentprop", [](double maxsegdeg, const std::string &boundprop, int hide,
                                     int group, optional<std::string> inconductor) {
        check_doc(s_doc, HERR);
        if (maxsegdeg > 10) maxsegdeg = 10;
        if (maxsegdeg <= 0) maxsegdeg = 1;
        std::string inc = inconductor.value_or("<None>");
        for (auto &a : s_doc->arclist) {
            if (a.IsSelected) {
                a.BoundaryMarker = boundprop;
                a.MaxSideLength = maxsegdeg;
                a.Hidden = (hide != 0);
                a.InGroup = group;
                a.InConductor = inc;
            }
        }
    });

    // ---- Material properties (Kx, Ky, qv, Kt) ----
    m.def("hi_addmaterial", [](const std::string &name, optional<double> Kx, optional<double> Ky,
                               optional<double> qv, optional<double> Kt) {
        check_doc(s_doc, HERR);
        HMaterialProp mt;
        mt.BlockName = name;
        if (Kx) { mt.Kx = *Kx; mt.Ky = *Kx; }
        if (Ky) mt.Ky = *Ky;
        if (qv) mt.qv = *qv;
        if (Kt) mt.Kt = *Kt;
        s_doc->blockproplist.push_back(mt);
    });
    m.def("hi_modifymaterial", [](const std::string &name, int prop, mp_obj_t value) {
        check_doc(s_doc, HERR);
        int k = -1;
        for (int i = 0; i < (int)s_doc->blockproplist.size(); i++)
            if (s_doc->blockproplist[i].BlockName == name) { k = i; break; }
        if (k < 0) return;
        auto &mt = s_doc->blockproplist[k];
        switch (prop) {
            case 0: mt.BlockName = caster<std::string>::load(value); break;
            case 1: mt.Kx = caster<double>::load(value); break;
            case 2: mt.Ky = caster<double>::load(value); break;
            case 3: mt.qv = caster<double>::load(value); break;
            case 4: mt.Kt = caster<double>::load(value); break;
        }
    });

    // ---- Boundary properties (BdryFormat, Tset, qs, Tinf, h, beta, TinfRad) ----
    m.def("hi_addboundprop", [](const std::string &name, optional<double> BdryFormat,
                                optional<double> Tset, optional<double> qs, optional<double> Tinf,
                                optional<double> h, optional<double> beta,
                                optional<double> TinfRad) {
        check_doc(s_doc, HERR);
        HBoundaryProp b;
        b.BdryName = name;
        if (BdryFormat) b.BdryFormat = (int)*BdryFormat;
        if (Tset) b.Tset = *Tset;
        if (qs) b.qs = *qs;
        if (Tinf) { b.Tinf = *Tinf; b.TinfRad = *Tinf; }  // TinfRad defaults to Tinf
        if (h) b.h = *h;
        if (beta) b.beta = *beta;
        if (TinfRad) b.TinfRad = *TinfRad;
        s_doc->lineproplist.push_back(b);
    });
    m.def("hi_modifyboundprop", [](const std::string &name, int prop, mp_obj_t value) {
        check_doc(s_doc, HERR);
        int k = -1;
        for (int i = 0; i < (int)s_doc->lineproplist.size(); i++)
            if (s_doc->lineproplist[i].BdryName == name) { k = i; break; }
        if (k < 0) return;
        auto &b = s_doc->lineproplist[k];
        switch (prop) {
            case 0: b.BdryName = caster<std::string>::load(value); break;
            case 1: b.BdryFormat = caster<int>::load(value); break;
            case 2: b.Tset = caster<double>::load(value); break;
            case 3: b.qs = caster<double>::load(value); break;
            case 4: b.Tinf = caster<double>::load(value); break;
            case 5: b.h = caster<double>::load(value); break;
            case 6: b.beta = caster<double>::load(value); break;
            case 7: b.TinfRad = caster<double>::load(value); break;
        }
    });

    // ---- Point properties (T, qp) ----
    m.def("hi_addpointprop", [](const std::string &name, optional<double> T, optional<double> qp) {
        check_doc(s_doc, HERR);
        HPointProp p;
        p.PointName = name;
        if (T) p.T = *T;
        if (qp) p.qp = *qp;
        s_doc->nodeproplist.push_back(p);
    });
    m.def("hi_modifypointprop", [](const std::string &name, int prop, mp_obj_t value) {
        check_doc(s_doc, HERR);
        int k = -1;
        for (int i = 0; i < (int)s_doc->nodeproplist.size(); i++)
            if (s_doc->nodeproplist[i].PointName == name) { k = i; break; }
        if (k < 0) return;
        auto &p = s_doc->nodeproplist[k];
        switch (prop) {
            case 0: p.PointName = caster<std::string>::load(value); break;
            case 1: p.T = caster<double>::load(value); break;
            case 2: p.qp = caster<double>::load(value); break;
        }
    });

    // ---- Conductor properties (T, q, conductortype) ----
    m.def("hi_addconductorprop", [](const std::string &name, optional<double> T, optional<double> q,
                                    optional<double> conductortype) {
        check_doc(s_doc, HERR);
        HCircuit c;
        c.CircName = name;
        if (T) c.T = *T;
        if (q) c.q = *q;
        if (conductortype) c.CircType = (int)*conductortype;
        for (auto &existing : s_doc->circproplist) {
            if (existing.CircName == c.CircName)
                throw std::runtime_error("Duplicate conductor name (" + c.CircName + ") not allowed");
        }
        s_doc->circproplist.push_back(c);
    });
    m.def("hi_modifyconductorprop", [](const std::string &name, int prop, mp_obj_t value) {
        check_doc(s_doc, HERR);
        int k = -1;
        for (int i = 0; i < (int)s_doc->circproplist.size(); i++)
            if (s_doc->circproplist[i].CircName == name) { k = i; break; }
        if (k < 0) return;
        auto &c = s_doc->circproplist[k];
        switch (prop) {
            case 0: c.CircName = caster<std::string>::load(value); break;
            case 1: c.T = caster<double>::load(value); break;
            case 2: c.q = caster<double>::load(value); break;
            case 3: c.CircType = caster<int>::load(value); break;
        }
    });
    m.def("hi_deleteconductor", [](const std::string &name) {
        check_doc(s_doc, HERR);
        auto &list = s_doc->circproplist;
        for (auto it = list.begin(); it != list.end(); ++it)
            if (it->CircName == name) { list.erase(it); break; }
    });

    // ---- Problem info ----
    m.def("hi_getprobleminfo", []() -> std::tuple<double, double, double> {
        check_doc(s_doc, HERR);
        return std::make_tuple((double)s_doc->ProblemType, s_doc->Depth, s_doc->lengthScale());
    });

    // ---- T-K curve points (heat-only) ----
    m.def("hi_addtkpoint", [](const std::string &name, double T, double K) {
        check_doc(s_doc, HERR);
        int k = -1;
        for (int i = 0; i < (int)s_doc->blockproplist.size(); i++)
            if (s_doc->blockproplist[i].BlockName == name) { k = i; break; }
        if (k < 0) return;
        int np = s_doc->blockproplist[k].npts;
        if (np < 128) {
            s_doc->blockproplist[k].Kn[np] = CComplex(T, K);
            s_doc->blockproplist[k].npts++;
        }
    });
    m.def("hi_cleartkpoints", [](const std::string &name) {
        check_doc(s_doc, HERR);
        for (auto &mat : s_doc->blockproplist) {
            if (mat.BlockName == name) { mat.npts = 0; break; }
        }
    });

    // ---- Load solution ----
    m.def("hi_loadsolution", []() {
        check_doc(s_doc, HERR);
        if (s_doc->filePath.empty())
            throw std::runtime_error("No file path - save before loading solution");
        std::string resPath = s_doc->filePath;
        auto dot = resPath.rfind('.');
        if (dot != std::string::npos) resPath = resPath.substr(0, dot);
        resPath += ".anh";
        namespace fs = std::filesystem;
        resPath = femm_u8string(fs::absolute(femm_path(resPath)));
        std::replace(resPath.begin(), resPath.end(), '\\', '/');
        if (!fs::exists(resPath))
            throw std::runtime_error("Cannot open solution file " + resPath);

        HeatResultsDoc *oldDoc = s_resultsDoc;
        s_resultsDoc = new HeatResultsDoc();
        if (!s_resultsDoc->OpenDocument(resPath)) {
            delete s_resultsDoc;
            s_resultsDoc = nullptr;
            throw std::runtime_error("Failed to load solution: " + resPath);
        }
        register_heat_postproc_py(s_resultsDoc);
        doc_registry_add(s_resultsDoc);
        auto cb = get_results_opened_callback();
        if (cb) cb(resPath.c_str(), s_resultsDoc);
        if (oldDoc && doc_registry_contains(oldDoc)) {
            doc_registry_remove(oldDoc);
            delete oldDoc;
        }
    });

    // ---- Edit mode / selection / getmaterial / newdocument ----
    m.def("hi_seteditmode", [](const std::string &mode) {
        check_doc(s_doc, HERR);
        std::string md = mode;
        for (auto &ch : md) ch = (char)tolower((unsigned char)ch);
        if (md == "nodes") s_editAction = 0;
        else if (md == "segments") s_editAction = 1;
        else if (md == "blocks") s_editAction = 2;
        else if (md == "arcsegments") s_editAction = 3;
        else if (md == "group") s_editAction = 4;
        auto cb = get_view_command_callback();
        if (cb) { double args[4] = {(double)s_editAction, 0, 0, 0}; cb("hi_seteditmode", 1, args); }
    });

    m.def("hi_selectrectangle", [](double x1, double y1, double x2, double y2, optional<int> eaOpt) {
        check_doc(s_doc, HERR);
        int ea = eaOpt.value_or(s_editAction);
        if (ea < 0 || ea > 4) return;
        if (x1 > x2) std::swap(x1, x2);
        if (y1 > y2) std::swap(y1, y2);
        auto inRect = [&](double x, double y) { return x >= x1 && x <= x2 && y >= y1 && y <= y2; };
        if (ea == 0 || ea == 4)
            for (auto &nd : s_doc->nodelist) if (inRect(nd.x, nd.y)) nd.IsSelected = true;
        if (ea == 2 || ea == 4)
            for (auto &bl : s_doc->blocklist) if (inRect(bl.x, bl.y)) bl.IsSelected = true;
        if (ea == 1 || ea == 4)
            for (auto &seg : s_doc->linelist) {
                auto &n0 = s_doc->nodelist[seg.n0];
                auto &n1 = s_doc->nodelist[seg.n1];
                if (inRect(n0.x, n0.y) && inRect(n1.x, n1.y)) seg.IsSelected = true;
            }
        if (ea == 3 || ea == 4)
            for (auto &arc : s_doc->arclist) {
                auto &n0 = s_doc->nodelist[arc.n0];
                auto &n1 = s_doc->nodelist[arc.n1];
                if (inRect(n0.x, n0.y) && inRect(n1.x, n1.y)) arc.IsSelected = true;
            }
    });

    m.def("hi_selectcircle", [](double cx, double cy, double R, optional<int> eaOpt) {
        check_doc(s_doc, HERR);
        int ea = eaOpt.value_or(s_editAction);
        if (ea < 0 || ea > 4) return;
        auto inCirc = [&](double x, double y) {
            double dx = x - cx, dy = y - cy;
            return sqrt(dx * dx + dy * dy) <= R;
        };
        if (ea == 0 || ea == 4)
            for (auto &nd : s_doc->nodelist) if (inCirc(nd.x, nd.y)) nd.IsSelected = true;
        if (ea == 2 || ea == 4)
            for (auto &bl : s_doc->blocklist) if (inCirc(bl.x, bl.y)) bl.IsSelected = true;
        if (ea == 1 || ea == 4)
            for (auto &seg : s_doc->linelist) {
                auto &n0 = s_doc->nodelist[seg.n0];
                auto &n1 = s_doc->nodelist[seg.n1];
                if (inCirc(n0.x, n0.y) && inCirc(n1.x, n1.y)) seg.IsSelected = true;
            }
        if (ea == 3 || ea == 4)
            for (auto &arc : s_doc->arclist) {
                auto &n0 = s_doc->nodelist[arc.n0];
                auto &n1 = s_doc->nodelist[arc.n1];
                if (inCirc(n0.x, n0.y) && inCirc(n1.x, n1.y)) arc.IsSelected = true;
            }
    });

    m.def("hi_getmaterial", [](const std::string &matname) {
        check_doc(s_doc, HERR);
        std::string libPath = materialLibPath("heatlib.dat");
        FILE *fp = femm_fopen(libPath, "rt");
        if (!fp) throw std::runtime_error("Couldn't open " + libPath);
        char s[1024], q[1024];
        char *v;
        HMaterialProp MProp;
        while (fgets(s, 1024, fp) != NULL) {
            if (sscanf(s, "%s", q) == EOF) continue;
            if (_strnicmp(q, "<beginblock>", 12) == 0) { MProp = HMaterialProp(); MProp.Kt = 3; }
            if (_strnicmp(q, "<blockname>", 11) == 0) MProp.BlockName = hi_ExtractQuoted(hi_StripKey(s));
            if (_strnicmp(q, "<Kx>", 4) == 0) { v = hi_StripKey(s); sscanf(v, "%lf", &MProp.Kx); }
            if (_strnicmp(q, "<Ky>", 4) == 0) { v = hi_StripKey(s); sscanf(v, "%lf", &MProp.Ky); }
            if (_strnicmp(q, "<Kt>", 4) == 0) { v = hi_StripKey(s); sscanf(v, "%lf", &MProp.Kt); }
            if (_strnicmp(q, "<qv>", 4) == 0) { v = hi_StripKey(s); sscanf(v, "%lf", &MProp.qv); }
            if (_strnicmp(q, "<TKPoints>", 10) == 0) {
                v = hi_StripKey(s);
                sscanf(v, "%i", &MProp.npts);
                if (MProp.npts > 0) {
                    for (int j = 0; j < MProp.npts && j < 128; j++) {
                        fgets(s, 1024, fp);
                        sscanf(s, "%lf\t%lf", &MProp.Kn[j].re, &MProp.Kn[j].im);
                    }
                }
            }
            if (_strnicmp(q, "<endblock>", 10) == 0) {
                if (MProp.BlockName == matname) {
                    s_doc->blockproplist.push_back(MProp);
                    fclose(fp);
                    return;
                }
            }
        }
        fclose(fp);
        throw std::runtime_error("Couldn't load \"" + matname + "\" from the materials library");
    });

    m.def("hi_newdocument", []() {
        check_doc(s_doc, HERR);
        s_doc->NewDocument();
        s_doc->filePath = "Untitled.feh";
    });
}
