// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors
//
// py_curr_editor.cpp - ci_* function implementations.
// Operates on CurrDocument. Current flow can be AC, so
// boundary/point properties are complex.
#include "py_curr_editor.h"
#include "utf8_io.h"
#include "material_lib_path.h"
#include "py_curr_postproc.h"
#include "py_editor_common.h"
#include "curr_document.h"
#include "curr_results_doc.h"
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

static const char *CIERR = "No current flow input in focus";

static CurrDocument *s_doc = nullptr;
static CurrResultsDoc *s_resultsDoc = nullptr;

CurrDocument *active_curr_document() { return s_doc; }
void deactivate_curr_editor_py() { s_doc = nullptr; }
void clear_curr_results_doc() { s_resultsDoc = nullptr; }

static int s_editAction = 0;

static char *ci_StripKey(char *c) {
    char *d = strchr(c, '=');
    return d ? d + 1 : c;
}
static std::string ci_ExtractQuoted(char *v) {
    if (!v) return "";
    int k = (int)strlen(v);
    for (int i = 0; i < k; i++) if (v[i] == '\"') { v = v + i + 1; break; }
    k = (int)strlen(v);
    if (k > 0) for (int i = k - 1; i >= 0; i--) if (v[i] == '\"') { v[i] = 0; break; }
    return std::string(v);
}

void register_curr_editor_py(CurrDocument *doc) {
    s_doc = doc;
    femm_py::Module &m = femm_py::femm_module;

    register_common_editor(m, "ci", s_doc, CIERR, s_editAction);

    // Improvised Asymptotic Boundary Conditions (open boundary). Native C++
    // (femm_makeabc.cpp), shared with the Lua tree; replaces the former Python
    // _make_abc helper in the init script. All five args optional.
    m.def("ci_makeABC", [](optional<int> n, optional<double> R, optional<double> x,
                           optional<double> y, optional<int> bc) {
        check_doc(s_doc, CIERR);
        MakeABCParams p;
        p.n = n; p.R = R; p.x = x; p.y = y; p.bctype = bc;
        curr_makeABC(*s_doc, p);
    });

    // ---- Problem definition (units, type, frequency, precision, depth, minangle) ----
    m.def("ci_probdef", [](const std::string &units, optional<std::string> ptype,
                           optional<double> freq, optional<double> prec, optional<double> depth,
                           optional<double> minangle) {
        check_doc(s_doc, CIERR);
        int lu = -1;
        if (units == "inches") lu = 0;
        else if (units == "millimeters") lu = 1;
        else if (units == "centimeters") lu = 2;
        else if (units == "meters") lu = 3;
        else if (units == "mills" || units == "mils") lu = 4;
        else if (units == "micrometers") lu = 5;
        if (lu == -1) throw std::runtime_error("Unknown length unit " + units);
        s_doc->LengthUnits = lu;
        if (!ptype) return;
        if (*ptype == "planar") s_doc->ProblemType = 0;
        else if (*ptype == "axi") s_doc->ProblemType = 1;
        else throw std::runtime_error("Unknown problem type " + *ptype);
        if (!freq) return;
        s_doc->Frequency = fabs(*freq);
        if (!prec) return;
        double p = *prec;
        if (p == 0 || p > 1.e-8) p = 1.e-8;
        if (p < 1.e-16) p = 1.e-16;
        s_doc->Precision = p;
        if (!depth) return;
        s_doc->Depth = *depth;
        if (s_doc->Depth <= 0) s_doc->Depth = 1;
        if (!minangle) return;
        double ma = *minangle;
        if (ma >= 1.0 && ma <= MINANGLE_MAX) s_doc->MinAngle = ma;
    });

    // ---- Set properties on selected ----
    m.def("ci_setnodeprop", [](const std::string &nodeprop, int groupno,
                               optional<std::string> inconductor) {
        check_doc(s_doc, CIERR);
        std::string inc = inconductor.value_or("<None>");
        for (auto &nd : s_doc->nodelist) {
            if (nd.IsSelected) {
                nd.InGroup = groupno;
                nd.BoundaryMarker = nodeprop;
                nd.InConductor = inc;
            }
        }
    });

    m.def("ci_setblockprop", [](optional<std::string> blocktype, optional<double> automesh,
                                optional<double> meshsize, optional<double> group) {
        check_doc(s_doc, CIERR);
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

    m.def("ci_setsegmentprop", [](const std::string &prop, double elesize, int automesh,
                                  int hide, int group, optional<std::string> inconductor) {
        check_doc(s_doc, CIERR);
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

    m.def("ci_setarcsegmentprop", [](double maxsegdeg, const std::string &boundprop, int hide,
                                     int group, optional<std::string> inconductor) {
        check_doc(s_doc, CIERR);
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

    // ---- Material properties (ox, oy, ex, ey, ltx, lty) ----
    m.def("ci_addmaterial", [](const std::string &name, optional<double> ox, optional<double> oy,
                               optional<double> ex, optional<double> ey, optional<double> ltx,
                               optional<double> lty) {
        check_doc(s_doc, CIERR);
        CurrMaterialProp mt;
        mt.BlockName = name;
        if (ox) { mt.ox = *ox; mt.oy = *ox; }
        if (oy) mt.oy = *oy;
        if (ex) { mt.ex = *ex; mt.ey = *ex; }
        if (ey) mt.ey = *ey;
        if (ltx) { mt.ltx = *ltx; mt.lty = *ltx; }
        if (lty) mt.lty = *lty;
        s_doc->blockproplist.push_back(mt);
    });
    m.def("ci_modifymaterial", [](const std::string &name, int prop, mp_obj_t value) {
        check_doc(s_doc, CIERR);
        int k;
        for (k = 0; k < (int)s_doc->blockproplist.size(); k++)
            if (name == s_doc->blockproplist[k].BlockName) break;
        if (k == (int)s_doc->blockproplist.size()) return;
        switch (prop) {
            case 0: s_doc->blockproplist[k].BlockName = caster<std::string>::load(value); break;
            case 1: s_doc->blockproplist[k].ox = caster<double>::load(value); break;
            case 2: s_doc->blockproplist[k].oy = caster<double>::load(value); break;
            case 3: s_doc->blockproplist[k].ex = caster<double>::load(value); break;
            case 4: s_doc->blockproplist[k].ey = caster<double>::load(value); break;
            case 5: s_doc->blockproplist[k].ltx = caster<double>::load(value); break;
            case 6: s_doc->blockproplist[k].lty = caster<double>::load(value); break;
        }
    });

    // ---- Boundary properties (vs, qs, c0, c1 complex; BdryFormat) ----
    m.def("ci_addboundprop", [](const std::string &name, optional<CComplex> vs, optional<CComplex> qs,
                                optional<CComplex> c0, optional<CComplex> c1,
                                optional<double> BdryFormat) {
        check_doc(s_doc, CIERR);
        CurrBoundaryProp b;
        b.BdryName = name;
        if (vs) b.vs = *vs;
        if (qs) b.qs = *qs;
        if (c0) b.c0 = *c0;
        if (c1) b.c1 = *c1;
        if (BdryFormat) b.BdryFormat = (int)*BdryFormat;
        s_doc->lineproplist.push_back(b);
    });
    m.def("ci_modifyboundprop", [](const std::string &name, int prop, mp_obj_t value) {
        check_doc(s_doc, CIERR);
        int k;
        for (k = 0; k < (int)s_doc->lineproplist.size(); k++)
            if (name == s_doc->lineproplist[k].BdryName) break;
        if (k == (int)s_doc->lineproplist.size()) return;
        switch (prop) {
            case 0: s_doc->lineproplist[k].BdryName = caster<std::string>::load(value); break;
            case 1: s_doc->lineproplist[k].vs = caster<CComplex>::load(value); break;
            case 2: s_doc->lineproplist[k].qs = caster<CComplex>::load(value); break;
            case 3: s_doc->lineproplist[k].c0 = caster<CComplex>::load(value); break;
            case 4: s_doc->lineproplist[k].c1 = caster<CComplex>::load(value); break;
            case 5: s_doc->lineproplist[k].BdryFormat = caster<int>::load(value); break;
        }
    });

    // ---- Point properties (vp, qp complex) ----
    m.def("ci_addpointprop", [](const std::string &name, optional<CComplex> vp, optional<CComplex> qp) {
        check_doc(s_doc, CIERR);
        CurrPointProp p;
        p.PointName = name;
        if (vp) p.vp = *vp;
        if (qp) p.qp = *qp;
        s_doc->nodeproplist.push_back(p);
    });
    m.def("ci_modifypointprop", [](const std::string &name, int prop, mp_obj_t value) {
        check_doc(s_doc, CIERR);
        int k;
        for (k = 0; k < (int)s_doc->nodeproplist.size(); k++)
            if (name == s_doc->nodeproplist[k].PointName) break;
        if (k == (int)s_doc->nodeproplist.size()) return;
        switch (prop) {
            case 0: s_doc->nodeproplist[k].PointName = caster<std::string>::load(value); break;
            case 1: s_doc->nodeproplist[k].vp = caster<CComplex>::load(value); break;
            case 2: s_doc->nodeproplist[k].qp = caster<CComplex>::load(value); break;
        }
    });

    // ---- Conductor properties (vc/qc supplied as separate re/im doubles) ----
    m.def("ci_addconductorprop", [](const std::string &name, optional<CComplex> vc,
                                    optional<CComplex> qc, optional<double> conductortype) {
        check_doc(s_doc, CIERR);
        CurrCircuit c;
        c.CircName = name;
        if (vc) c.vc = *vc;
        if (qc) c.qc = *qc;
        if (conductortype) c.CircType = (int)*conductortype;
        for (int k = 0; k < (int)s_doc->circproplist.size(); k++) {
            if (c.CircName == s_doc->circproplist[k].CircName)
                throw std::runtime_error("Duplicate conductor name (" + c.CircName + ") not allowed");
        }
        s_doc->circproplist.push_back(c);
    });
    m.def("ci_modifyconductorprop", [](const std::string &name, int prop, mp_obj_t value) {
        check_doc(s_doc, CIERR);
        int k;
        for (k = 0; k < (int)s_doc->circproplist.size(); k++)
            if (name == s_doc->circproplist[k].CircName) break;
        if (k == (int)s_doc->circproplist.size()) return;
        switch (prop) {
            case 0: s_doc->circproplist[k].CircName = caster<std::string>::load(value); break;
            case 1: s_doc->circproplist[k].vc.re = caster<double>::load(value); break;
            case 2: s_doc->circproplist[k].vc.im = caster<double>::load(value); break;
            case 3: s_doc->circproplist[k].qc.re = caster<double>::load(value); break;
            case 4: s_doc->circproplist[k].qc.im = caster<double>::load(value); break;
            case 5: s_doc->circproplist[k].CircType = caster<int>::load(value); break;
        }
    });
    m.def("ci_deleteconductor", [](const std::string &name) {
        check_doc(s_doc, CIERR);
        auto &list = s_doc->circproplist;
        for (auto it = list.begin(); it != list.end(); ++it)
            if (it->CircName == name) { list.erase(it); break; }
    });

    // ---- Problem info (length scale computed from units) ----
    m.def("ci_getprobleminfo", []() -> std::tuple<double, double, double> {
        check_doc(s_doc, CIERR);
        double scale;
        switch (s_doc->LengthUnits) {
            case 1: scale = 0.001;    break;  // mm
            case 2: scale = 0.01;     break;  // cm
            case 3: scale = 1.0;      break;  // m
            case 4: scale = 2.54e-05; break;  // mils
            case 5: scale = 1.0e-06;  break;  // um
            default: scale = 0.0254;  break;  // inches
        }
        return std::make_tuple((double)s_doc->ProblemType, s_doc->Depth, scale);
    });

    // ---- Load solution (.anc) ----
    m.def("ci_loadsolution", []() {
        check_doc(s_doc, CIERR);
        std::string resPath = s_doc->filePath;
        auto dot = resPath.rfind('.');
        if (dot != std::string::npos) resPath = resPath.substr(0, dot);
        resPath += ".anc";
        namespace fs = std::filesystem;
        resPath = femm_u8string(fs::absolute(femm_path(resPath)));
        std::replace(resPath.begin(), resPath.end(), '\\', '/');
        if (!fs::exists(resPath))
            throw std::runtime_error("Cannot open solution file " + resPath);

        CurrResultsDoc *oldDoc = s_resultsDoc;
        s_resultsDoc = new CurrResultsDoc();
        if (!s_resultsDoc->OpenDocument(resPath)) {
            delete s_resultsDoc;
            s_resultsDoc = nullptr;
            throw std::runtime_error("Failed to load solution: " + resPath);
        }
        register_curr_postproc_py(s_resultsDoc);
        doc_registry_add(s_resultsDoc);
        auto cb = get_results_opened_callback();
        if (cb) cb(resPath.c_str(), s_resultsDoc);
        if (oldDoc && doc_registry_contains(oldDoc)) {
            doc_registry_remove(oldDoc);
            delete oldDoc;
        }
    });

    // ---- Edit mode / selection / getmaterial / newdocument ----
    m.def("ci_newdocument", []() {
        check_doc(s_doc, CIERR);
        s_doc->NewDocument();
        s_doc->filePath = "Untitled.fec";
    });

    m.def("ci_seteditmode", [](const std::string &mode) {
        check_doc(s_doc, CIERR);
        std::string md = mode;
        for (auto &ch : md) ch = (char)tolower((unsigned char)ch);
        if (md == "nodes") s_editAction = 0;
        else if (md == "segments") s_editAction = 1;
        else if (md == "blocks") s_editAction = 2;
        else if (md == "arcsegments") s_editAction = 3;
        else if (md == "group") s_editAction = 4;
        auto cb = get_view_command_callback();
        if (cb) { double args[4] = {(double)s_editAction, 0, 0, 0}; cb("ci_seteditmode", 1, args); }
    });

    m.def("ci_selectrectangle", [](double x1, double y1, double x2, double y2, optional<int> eaOpt) {
        check_doc(s_doc, CIERR);
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

    m.def("ci_selectcircle", [](double cx, double cy, double R, optional<int> eaOpt) {
        check_doc(s_doc, CIERR);
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

    m.def("ci_getmaterial", [](const std::string &matname) {
        check_doc(s_doc, CIERR);
        std::string libPath = materialLibPath("condlib.dat");
        FILE *fp = femm_fopen(libPath, "rt");
        if (!fp) throw std::runtime_error("Couldn't open " + libPath);
        char s[1024], q[1024];
        char *v;
        CurrMaterialProp MProp;
        while (fgets(s, 1024, fp) != NULL) {
            if (sscanf(s, "%s", q) == EOF) continue;
            if (_strnicmp(q, "<beginblock>", 12) == 0) MProp = CurrMaterialProp();
            if (_strnicmp(q, "<blockname>", 11) == 0) MProp.BlockName = ci_ExtractQuoted(ci_StripKey(s));
            if (_strnicmp(q, "<ox>", 4) == 0) { v = ci_StripKey(s); sscanf(v, "%lf", &MProp.ox); }
            if (_strnicmp(q, "<oy>", 4) == 0) { v = ci_StripKey(s); sscanf(v, "%lf", &MProp.oy); }
            if (_strnicmp(q, "<ex>", 4) == 0) { v = ci_StripKey(s); sscanf(v, "%lf", &MProp.ex); }
            if (_strnicmp(q, "<ey>", 4) == 0) { v = ci_StripKey(s); sscanf(v, "%lf", &MProp.ey); }
            if (_strnicmp(q, "<ltx>", 5) == 0) { v = ci_StripKey(s); sscanf(v, "%lf", &MProp.ltx); }
            if (_strnicmp(q, "<lty>", 5) == 0) { v = ci_StripKey(s); sscanf(v, "%lf", &MProp.lty); }
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
}
