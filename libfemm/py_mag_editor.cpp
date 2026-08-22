// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors
//
// py_mag_editor.cpp - mi_* function implementations.
// Operates on MagDocument via femm_py::Module.
#include "py_mag_editor.h"
#include "utf8_io.h"
#include "material_lib_path.h"
#include "py_mag_postproc.h"
#include "py_editor_common.h"
#include "mag_document.h"
#include "results_doc.h"
#include "dxf_io.h"
#include "femm_makeabc.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
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

static const char *MAGERR = "No current magnetics input in focus";

// Active document pointer.
static MagDocument *s_doc = nullptr;

// Active results document - managed by mi_loadsolution. File scope so mo_close
// (py_mag_postproc.cpp) can null it via clear_mag_results_doc().
static ResultsDoc *s_resultsDoc = nullptr;

MagDocument *active_mag_document() { return s_doc; }
void deactivate_mag_editor_py() { s_doc = nullptr; }
void clear_mag_results_doc() { s_resultsDoc = nullptr; }

// Current edit action for selectrectangle/selectcircle (0=nodes..4=group).
static int s_editAction = 0;

// Material-library key parsing helpers.
static char *mi_StripKey(char *s) {
    char *p = strchr(s, '=');
    return p ? p + 1 : s;
}
static std::string mi_ExtractQuoted(char *v) {
    char *start = strchr(v, '"');
    if (!start) return std::string(v);
    start++;
    char *end = strrchr(start, '"');
    if (end) *end = 0;
    return std::string(start);
}

void register_mag_editor_py(MagDocument *doc) {
    s_doc = doc;
    femm_py::Module &m = femm_py::femm_module;

    // Common functions (geometry, selection, transforms, mesh, view, ...).
    register_common_editor(m, "mi", s_doc, MAGERR, s_editAction);

    // Improvised Asymptotic Boundary Conditions (open boundary). Native C++
    // (femm_makeabc.cpp), shared with the Lua tree; replaces the former Python
    // _make_abc helper in the init script. All five args optional.
    m.def("mi_makeABC", [](optional<int> n, optional<double> R, optional<double> x,
                           optional<double> y, optional<int> bc) {
        check_doc(s_doc, MAGERR);
        MakeABCParams p;
        p.n = n; p.R = R; p.x = x; p.y = y; p.bctype = bc;
        mag_makeABC(*s_doc, p);
    });

    // ---- Problem definition ----
    m.def("mi_probdef", [](double freq, optional<std::string> units, optional<std::string> ptype,
                           optional<double> prec, optional<double> depth,
                           optional<double> minangle, optional<double> acsolver) {
        check_doc(s_doc, MAGERR);
        s_doc->Frequency = fabs(freq);
        if (!units) return;
        int lu = -1;
        const std::string &u = *units;
        if (u == "inches") lu = 0;
        else if (u == "millimeters") lu = 1;
        else if (u == "centimeters") lu = 2;
        else if (u == "meters") lu = 3;
        else if (u == "mills" || u == "mils") lu = 4;
        else if (u == "micrometers") lu = 5;
        if (lu == -1) throw std::runtime_error("Unknown length unit " + u);
        s_doc->LengthUnits = lu;
        if (!ptype) return;
        if (*ptype == "planar") s_doc->ProblemType = 0;
        else if (*ptype == "axi") s_doc->ProblemType = 1;
        else throw std::runtime_error("Unknown problem type " + *ptype);
        if (!prec) return;
        double p = *prec;
        if (p == 0 || p > 1.e-8) p = 1.e-8;
        if (p < 1.e-16) p = 1.e-16;
        s_doc->Precision = p;
        if (!depth) return;
        s_doc->Depth = fabs(*depth);
        if (s_doc->Depth <= 0) s_doc->Depth = 1;
        if (!minangle) return;
        double ma = *minangle;
        if (ma >= 1.0 && ma <= MINANGLE_MAX) s_doc->MinAngle = ma;
        if (!acsolver) return;
        int solver = (int)*acsolver;
        if (solver == 0 || solver == 1) s_doc->ACSolver = solver;
    });

    // ---- File I/O (physics-specific) ----
    m.def("mi_setprevious", [](optional<std::string> path, optional<double> ptype) {
        check_doc(s_doc, MAGERR);
        if (path) s_doc->PrevSoln = fixpath(*path);
        if (ptype) s_doc->PrevType = (int)*ptype;
        else s_doc->PrevType = 0;
    });

    // ---- Set properties on selected ----
    m.def("mi_setnodeprop", [](const std::string &nodeprop, int groupno, optional<double> lfs) {
        check_doc(s_doc, MAGERR);
        for (auto &nd : s_doc->nodelist) {
            if (nd.IsSelected) {
                nd.InGroup = groupno;
                nd.BoundaryMarker = nodeprop;
                if (lfs) nd.lfs = *lfs;
            }
        }
    });

    m.def("mi_setblockprop", [](optional<std::string> blocktype, optional<double> automesh,
                                optional<double> meshsize, optional<std::string> incircuit,
                                optional<mp_obj_t> magdir, optional<double> group,
                                optional<double> turns) {
        check_doc(s_doc, MAGERR);
        std::string bt = "<None>";
        int am = 1;
        double ms = 0;
        std::string inc = "<None>";
        double magdirection = 0;
        std::string magdirfctn;
        int grp = 0;
        int tn = 1;

        if (blocktype) bt = *blocktype;
        if (automesh) am = (int)*automesh;
        if (meshsize) ms = *meshsize;
        if (incircuit) inc = *incircuit;
        if (magdir) {
            mp_obj_t o = *magdir;
            if (mp_obj_is_str(o)) magdirfctn = caster<std::string>::load(o);
            else magdirection = caster<double>::load(o);
        }
        if (group) grp = (int)*group;
        if (turns) { tn = (int)*turns; if (tn == 0) tn = 1; }

        for (auto &b : s_doc->blocklist) {
            if (b.IsSelected) {
                b.MaxArea = PI * ms * ms / 4.0;
                b.MagDir = magdirection;
                b.BlockType = bt;
                b.InCircuit = inc;
                b.InGroup = grp;
                b.Turns = tn;
                b.MagDirFctn = magdirfctn;
                if (am == 1) b.MaxArea = 0;
            }
        }
    });

    m.def("mi_setsegmentprop", [](const std::string &prop, double elesize, int automesh,
                                  int hide, int group) {
        check_doc(s_doc, MAGERR);
        for (auto &seg : s_doc->linelist) {
            if (seg.IsSelected) {
                if (automesh == 1) seg.MaxSideLength = -1;
                else if (elesize > 0) seg.MaxSideLength = elesize;
                else seg.MaxSideLength = -1;
                seg.BoundaryMarker = prop;
                seg.Hidden = (hide != 0);
                seg.InGroup = group;
            }
        }
    });

    m.def("mi_setarcsegmentprop", [](double maxsegdeg, const std::string &boundprop,
                                     int hide, int group) {
        check_doc(s_doc, MAGERR);
        if (maxsegdeg > 10) maxsegdeg = 10;
        if (maxsegdeg <= 0) maxsegdeg = 1;
        for (auto &a : s_doc->arclist) {
            if (a.IsSelected) {
                a.BoundaryMarker = boundprop;
                a.MaxSideLength = maxsegdeg;
                a.Hidden = (hide != 0);
                a.InGroup = group;
            }
        }
    });

    // ---- Material properties ----
    m.def("mi_addmaterial", [](const std::string &name, optional<double> mu_x, optional<double> mu_y,
                               optional<double> H_c, optional<CComplex> Jsrc, optional<double> Cduct,
                               optional<double> Lam_d, optional<double> Theta_hn, optional<double> LamFill,
                               optional<double> LamType, optional<double> Theta_hx, optional<double> Theta_hy,
                               optional<double> NStrands, optional<double> WireD) {
        check_doc(s_doc, MAGERR);
        FMaterialProp mt;
        mt.BlockName = name;
        if (mu_x) { mt.mu_x = *mu_x; mt.mu_y = *mu_x; }
        if (mu_y) mt.mu_y = *mu_y;
        if (H_c) mt.H_c = *H_c;
        if (Jsrc) mt.Jsrc = *Jsrc;
        if (Cduct) mt.Cduct = *Cduct;
        if (Lam_d) mt.Lam_d = *Lam_d;
        if (Theta_hn) mt.Theta_hn = *Theta_hn;
        if (LamFill) {
            mt.LamFill = *LamFill;
            if (mt.LamFill <= 0) mt.LamFill = 1;
            if (mt.LamFill > 1) mt.LamFill = 1;
        }
        if (LamType) {
            mt.LamType = (int)*LamType;
            if (mt.LamType < 0) mt.LamType = 0;
        }
        if (Theta_hx) {
            mt.Theta_hx = *Theta_hx;
            mt.Theta_hy = Theta_hy.value_or(*Theta_hx);
        } else {
            mt.Theta_hx = mt.Theta_hn;
            mt.Theta_hy = mt.Theta_hn;
        }
        if (NStrands) {
            mt.NStrands = (int)*NStrands;
            mt.WireD = WireD.value_or(0.0);
        }
        s_doc->blockproplist.push_back(mt);
    });

    m.def("mi_addbhpoint", [](const std::string &name, double b, double h) {
        check_doc(s_doc, MAGERR);
        int k = -1;
        for (int i = 0; i < (int)s_doc->blockproplist.size(); i++)
            if (s_doc->blockproplist[i].BlockName == name) { k = i; break; }
        if (k < 0) return;

        auto &mat = s_doc->blockproplist[k];
        CComplex v = CComplex(b, h);
        if (mat.BHpoints == 0) {
            mat.BHdata.push_back(CComplex(0, 0));
            mat.BHpoints = 1;
        }
        for (int j = 0; j < mat.BHpoints; j++)
            if (mat.BHdata[j].re == v.re && mat.BHdata[j].im == v.im) return;
        mat.BHdata.push_back(v);
        mat.BHpoints++;
        std::sort(mat.BHdata.begin(), mat.BHdata.end(),
                  [](const CComplex &a, const CComplex &b) { return a.re < b.re; });
    });

    m.def("mi_clearbhpoints", [](const std::string &name) {
        check_doc(s_doc, MAGERR);
        for (auto &mat : s_doc->blockproplist) {
            if (mat.BlockName == name) {
                mat.BHpoints = 0;
                mat.BHdata.clear();
                break;
            }
        }
    });

    m.def("mi_modifymaterial", [](const std::string &name, int prop, mp_obj_t value) {
        check_doc(s_doc, MAGERR);
        int k = -1;
        for (int i = 0; i < (int)s_doc->blockproplist.size(); i++)
            if (s_doc->blockproplist[i].BlockName == name) { k = i; break; }
        if (k < 0) return;
        auto &mt = s_doc->blockproplist[k];
        switch (prop) {
            case 0:  mt.BlockName = caster<std::string>::load(value); break;
            case 1:  mt.mu_x = caster<double>::load(value); break;
            case 2:  mt.mu_y = caster<double>::load(value); break;
            case 3:  mt.H_c = caster<double>::load(value); break;
            case 4:  mt.Jsrc = caster<CComplex>::load(value); break;
            case 5:  mt.Cduct = caster<double>::load(value); break;
            case 6:  mt.Lam_d = caster<double>::load(value); break;
            case 7:  mt.Theta_hn = caster<double>::load(value); break;
            case 8:  mt.LamFill = caster<double>::load(value); break;
            case 9:  mt.LamType = caster<int>::load(value); break;
            case 10: mt.Theta_hx = caster<double>::load(value); break;
            case 11: mt.Theta_hy = caster<double>::load(value); break;
            case 12: mt.NStrands = caster<int>::load(value); break;
            case 13: mt.WireD = caster<double>::load(value); break;
        }
    });

    // ---- Boundary properties ----
    m.def("mi_addboundprop", [](const std::string &name, optional<double> A0, optional<double> A1,
                                optional<double> A2, optional<double> phi, optional<double> Mu,
                                optional<double> Sig, optional<CComplex> c0, optional<CComplex> c1,
                                optional<double> BdryFormat, optional<double> InnerAngle,
                                optional<double> OuterAngle) {
        check_doc(s_doc, MAGERR);
        FBoundaryProp b;
        b.BdryName = name;
        if (A0) b.A0 = *A0;
        if (A1) b.A1 = *A1;
        if (A2) b.A2 = *A2;
        if (phi) b.phi = *phi;
        if (Mu) b.Mu = *Mu;
        if (Sig) b.Sig = *Sig;
        if (c0) b.c0 = *c0;
        if (c1) b.c1 = *c1;
        if (BdryFormat) b.BdryFormat = (int)*BdryFormat;
        if (InnerAngle) b.InnerAngle = *InnerAngle;
        if (OuterAngle) b.OuterAngle = *OuterAngle;
        s_doc->lineproplist.push_back(b);
    });

    m.def("mi_modifyboundprop", [](const std::string &name, int prop, mp_obj_t value) {
        check_doc(s_doc, MAGERR);
        int k = -1;
        for (int i = 0; i < (int)s_doc->lineproplist.size(); i++)
            if (s_doc->lineproplist[i].BdryName == name) { k = i; break; }
        if (k < 0) return;
        auto &b = s_doc->lineproplist[k];
        switch (prop) {
            case 0:  b.BdryName = caster<std::string>::load(value); break;
            case 1:  b.A0 = caster<double>::load(value); break;
            case 2:  b.A1 = caster<double>::load(value); break;
            case 3:  b.A2 = caster<double>::load(value); break;
            case 4:  b.phi = caster<double>::load(value); break;
            case 5:  b.Mu = caster<double>::load(value); break;
            case 6:  b.Sig = caster<double>::load(value); break;
            case 7:  b.c0 = caster<CComplex>::load(value); break;
            case 8:  b.c1 = caster<CComplex>::load(value); break;
            case 9:  b.BdryFormat = caster<int>::load(value); break;
            case 10: b.InnerAngle = caster<double>::load(value); break;
            case 11: b.OuterAngle = caster<double>::load(value); break;
        }
    });

    // ---- Point properties ----
    m.def("mi_addpointprop", [](const std::string &name, optional<CComplex> Ap, optional<CComplex> Jp) {
        check_doc(s_doc, MAGERR);
        FPointProp p;
        p.PointName = name;
        if (Ap) p.Ap = *Ap;
        if (Jp) p.Jp = *Jp;
        s_doc->nodeproplist.push_back(p);
    });

    m.def("mi_modifypointprop", [](const std::string &name, int prop, mp_obj_t value) {
        check_doc(s_doc, MAGERR);
        int k = -1;
        for (int i = 0; i < (int)s_doc->nodeproplist.size(); i++)
            if (s_doc->nodeproplist[i].PointName == name) { k = i; break; }
        if (k < 0) return;
        auto &p = s_doc->nodeproplist[k];
        switch (prop) {
            case 0: p.PointName = caster<std::string>::load(value); break;
            case 1: p.Ap = caster<CComplex>::load(value); break;
            case 2: p.Jp = caster<CComplex>::load(value); break;
        }
    });

    // ---- Circuit properties ----
    m.def("mi_addcircprop", [](const std::string &name, optional<CComplex> Amps, optional<int> CircType) {
        check_doc(s_doc, MAGERR);
        FCircuit c;
        c.CircName = name;
        if (Amps) c.Amps = *Amps;
        if (CircType) c.CircType = *CircType;
        for (auto &existing : s_doc->circproplist) {
            if (existing.CircName == c.CircName)
                throw std::runtime_error("Duplicate circuit name (" + c.CircName + ") not allowed");
        }
        s_doc->circproplist.push_back(c);
    });

    m.def("mi_modifycircprop", [](const std::string &name, int prop, mp_obj_t value) {
        check_doc(s_doc, MAGERR);
        int k = -1;
        for (int i = 0; i < (int)s_doc->circproplist.size(); i++)
            if (s_doc->circproplist[i].CircName == name) { k = i; break; }
        if (k < 0) return;
        auto &c = s_doc->circproplist[k];
        switch (prop) {
            case 0: c.CircName = caster<std::string>::load(value); break;
            case 1: c.Amps = caster<CComplex>::load(value); break;
            case 2: c.CircType = caster<int>::load(value); break;
        }
    });

    m.def("mi_deletecircuit", [](const std::string &name) {
        check_doc(s_doc, MAGERR);
        auto &list = s_doc->circproplist;
        for (auto it = list.begin(); it != list.end(); ++it)
            if (it->CircName == name) { list.erase(it); break; }
    });

    // ---- Problem info ----
    m.def("mi_getprobleminfo", []() -> std::tuple<double, double, double, double> {
        check_doc(s_doc, MAGERR);
        return std::make_tuple((double)s_doc->ProblemType, s_doc->Frequency,
                               s_doc->Depth, s_doc->lengthScale());
    });

    // ---- Load solution ----
    m.def("mi_loadsolution", []() {
        check_doc(s_doc, MAGERR);
        if (s_doc->filePath.empty())
            throw std::runtime_error("No file path - save before loading solution");

        // Replace whatever extension the problem file has (any case, e.g. .FEM)
        // with .ans -- strip after the last dot like the elec/heat/curr editors,
        // rather than matching ".fem" case-sensitively.
        std::string ansPath = s_doc->filePath;
        auto dot = ansPath.rfind('.');
        if (dot != std::string::npos) ansPath = ansPath.substr(0, dot);
        ansPath += ".ans";

        namespace fs = std::filesystem;
        ansPath = femm_u8string(fs::absolute(femm_path(ansPath)));
        std::replace(ansPath.begin(), ansPath.end(), '\\', '/');

        if (!fs::exists(ansPath))
            throw std::runtime_error("No results file: " + ansPath);

        ResultsDoc *oldDoc = s_resultsDoc;
        s_resultsDoc = new ResultsDoc();
        if (!s_resultsDoc->OpenDocument(ansPath)) {
            delete s_resultsDoc;
            s_resultsDoc = nullptr;
            throw std::runtime_error("Failed to load solution: " + ansPath);
        }
        register_mag_postproc_py(s_resultsDoc);
        doc_registry_add(s_resultsDoc);

        auto cb = get_results_opened_callback();
        if (cb) cb(ansPath.c_str(), s_resultsDoc);

        if (oldDoc && doc_registry_contains(oldDoc)) {
            doc_registry_remove(oldDoc);
            delete oldDoc;
        }
    });

    // ---- Edit mode / selection / getmaterial / newdocument ----
    m.def("mi_seteditmode", [](const std::string &mode) {
        check_doc(s_doc, MAGERR);
        std::string md = mode;
        for (auto &ch : md) ch = (char)tolower((unsigned char)ch);
        if (md == "nodes") s_editAction = 0;
        else if (md == "segments") s_editAction = 1;
        else if (md == "blocks") s_editAction = 2;
        else if (md == "arcsegments") s_editAction = 3;
        else if (md == "group") s_editAction = 4;
        auto cb = get_view_command_callback();
        if (cb) { double args[4] = {(double)s_editAction, 0, 0, 0}; cb("mi_seteditmode", 1, args); }
    });

    m.def("mi_selectrectangle", [](double x1, double y1, double x2, double y2, optional<int> eaOpt) {
        check_doc(s_doc, MAGERR);
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

    m.def("mi_selectcircle", [](double cx, double cy, double R, optional<int> eaOpt) {
        check_doc(s_doc, MAGERR);
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

    m.def("mi_getmaterial", [](const std::string &matname) {
        check_doc(s_doc, MAGERR);
        std::string libPath = materialLibPath("matlib.dat");
        FILE *fp = femm_fopen(libPath, "rt");
        if (!fp) throw std::runtime_error("Couldn't open " + libPath);

        char s[1024], q[1024];
        char *v;
        FMaterialProp MProp;
        while (fgets(s, 1024, fp) != NULL) {
            if (sscanf(s, "%s", q) == EOF) continue;
            if (_strnicmp(q, "<beginblock>", 12) == 0) MProp = FMaterialProp();
            if (_strnicmp(q, "<blockname>", 11) == 0) { v = mi_StripKey(s); MProp.BlockName = mi_ExtractQuoted(v); }
            if (_strnicmp(q, "<Mu_x>", 6) == 0) { v = mi_StripKey(s); sscanf(v, "%lf", &MProp.mu_x); }
            if (_strnicmp(q, "<Mu_y>", 6) == 0) { v = mi_StripKey(s); sscanf(v, "%lf", &MProp.mu_y); }
            if (_strnicmp(q, "<H_c>", 5) == 0 && _strnicmp(q, "<H_cAngle>", 10) != 0) { v = mi_StripKey(s); sscanf(v, "%lf", &MProp.H_c); }
            if (_strnicmp(q, "<H_cAngle>", 10) == 0) { v = mi_StripKey(s); sscanf(v, "%lf", &MProp.Theta_m); }
            if (_strnicmp(q, "<J_re>", 6) == 0) { v = mi_StripKey(s); sscanf(v, "%lf", &MProp.Jsrc.re); }
            if (_strnicmp(q, "<J_im>", 6) == 0) { v = mi_StripKey(s); sscanf(v, "%lf", &MProp.Jsrc.im); }
            if (_strnicmp(q, "<Sigma>", 7) == 0) { v = mi_StripKey(s); sscanf(v, "%lf", &MProp.Cduct); }
            if (_strnicmp(q, "<d_lam>", 7) == 0) { v = mi_StripKey(s); sscanf(v, "%lf", &MProp.Lam_d); }
            if (_strnicmp(q, "<Phi_h>", 7) == 0 && _strnicmp(q, "<Phi_hx>", 8) != 0 && _strnicmp(q, "<Phi_hy>", 8) != 0) { v = mi_StripKey(s); sscanf(v, "%lf", &MProp.Theta_hn); }
            if (_strnicmp(q, "<Phi_hx>", 8) == 0) { v = mi_StripKey(s); sscanf(v, "%lf", &MProp.Theta_hx); }
            if (_strnicmp(q, "<Phi_hy>", 8) == 0) { v = mi_StripKey(s); sscanf(v, "%lf", &MProp.Theta_hy); }
            if (_strnicmp(q, "<LamType>", 9) == 0) { v = mi_StripKey(s); sscanf(v, "%i", &MProp.LamType); }
            if (_strnicmp(q, "<LamFill>", 9) == 0) { v = mi_StripKey(s); sscanf(v, "%lf", &MProp.LamFill); }
            if (_strnicmp(q, "<NStrands>", 10) == 0) { v = mi_StripKey(s); sscanf(v, "%i", &MProp.NStrands); }
            if (_strnicmp(q, "<WireD>", 7) == 0) { v = mi_StripKey(s); sscanf(v, "%lf", &MProp.WireD); }
            if (_strnicmp(q, "<BHPoints>", 10) == 0) {
                v = mi_StripKey(s);
                sscanf(v, "%i", &MProp.BHpoints);
                MProp.BHdata.resize(MProp.BHpoints);
                for (int j = 0; j < MProp.BHpoints; j++) {
                    fgets(s, 1024, fp);
                    sscanf(s, "%lf %lf", &MProp.BHdata[j].re, &MProp.BHdata[j].im);
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

    m.def("mi_newdocument", []() {
        check_doc(s_doc, MAGERR);
        s_doc->NewDocument();
        s_doc->filePath = "Untitled.fem";
    });
}
