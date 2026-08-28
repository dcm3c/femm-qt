// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors
//
// py_editor_common.h - shared editor commands for all physics types,
// A single template registers every
// common editor command under a prefix (mi, ei, hi, ci) via femm_py::Module.
//
// Patterns used:
//   m.def(P + "_name", [...])              — register under prefixed name
//   std::optional<T>                       — omitted trailing args
//   std::optional<std::tuple<...>>         — returns None or a tuple of values
//   sol::variadic_args (up to 4 numbers)   -> up to 4 std::optional<double>
// Argument lists, counts, and behaviour are otherwise identical.
#ifndef PY_EDITOR_COMMON_H
#define PY_EDITOR_COMMON_H

#include "py_bind.h"
#include "script_callbacks.h"
#include "coord_mode.h"
#include "document.h"
#include "dxf_io.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <optional>
#include <stdexcept>
#include <string>
#include <tuple>

#ifndef PI
#define PI 3.141592653589793238462643383279
#endif

// Throw if the active document pointer is null (bridged to a Python
// RuntimeError by femm_fn_call).
template<typename T>
inline void check_doc(T *doc, const char *msg) {
    if (!doc) throw std::runtime_error(msg);
}

// fixpath is defined in script_callbacks.h (included above).

// Dispatch a view command to the GUI callback. Returns the result string (or
// empty if there is no callback / no result).
inline std::string viewcmd(const std::string &cmd, int nargs = 0, double *args = nullptr) {
    auto cb = get_view_command_callback();
    if (!cb) return {};
    double a[4] = {};
    if (args) for (int i = 0; i < nargs && i < 4; i++) a[i] = args[i];
    const char *result = cb(cmd.c_str(), nargs, a);
    return result ? std::string(result) : std::string();
}

// Forward up-to-4 optional numeric args to a view command, passing the count
// of leading present args (replacement for sol::variadic_args forwarding).
// *_gridsnap("on"/"off") -> the numeric flag the view callback carries.
// The manual documents the STRING form for every physics, editor and
// postprocessor alike (magnpy.tex:639 and :1166 and the equivalents), and every
// binding sends it -- octavefemm's *_snapgridon.m emits mi_gridsnap("on").
// Only mo_gridsnap parsed it; the other seven took doubles and rejected the
// documented call outright. Shared here so all eight agree.
inline double gridSnapFlag(const std::string &flag) {
    std::string s = flag;
    for (auto &c : s) if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
    if (s == "on") return 1;
    if (s == "off") return 0;
    throw std::runtime_error("Unknown option for grid snap");
}

inline void viewcmd_fwd(const std::string &cmd,
                        std::optional<double> a0 = std::nullopt,
                        std::optional<double> a1 = std::nullopt,
                        std::optional<double> a2 = std::nullopt,
                        std::optional<double> a3 = std::nullopt) {
    std::optional<double> opts[4] = {a0, a1, a2, a3};
    double a[4] = {};
    int n = 0;
    for (int i = 0; i < 4; i++) {
        if (opts[i]) a[n++] = *opts[i];
        else break;
    }
    viewcmd(cmd, n, a);
}

#define CHK() check_doc(s_doc, errmsg)

template<typename DocType>
void register_common_editor(femm_py::Module &m, const std::string &P,
                            DocType *&s_doc, const char *errmsg, int &s_editAction) {

    // --- Geometry: add ---
    m.def(P + "_addnode", [&s_doc, errmsg](double x, double y) {
        CHK(); s_doc->AddNode(x, y, s_doc->defaultSnapDistance());
    });
    m.def(P + "_addblocklabel", [&s_doc, errmsg](double x, double y) {
        CHK(); s_doc->AddBlockLabel(x, y, s_doc->defaultSnapDistance());
    });
    m.def(P + "_addsegment", [&s_doc, errmsg](double x1, double y1, double x2, double y2) {
        CHK();
        int n0 = s_doc->ClosestNode(x1, y1);
        int n1 = s_doc->ClosestNode(x2, y2);
        if (n0 >= 0 && n1 >= 0 && n0 != n1) s_doc->AddSegment(n0, n1);
    });
    m.def(P + "_addarc", [&s_doc, errmsg](double x1, double y1, double x2, double y2,
                                          double arclen, double maxseg) {
        CHK();
        FArcSegment a;
        a.n0 = s_doc->ClosestNode(x1, y1);
        a.n1 = s_doc->ClosestNode(x2, y2);
        a.ArcLength = arclen;
        a.MaxSideLength = maxseg;
        if (a.n0 >= 0 && a.n1 >= 0 && a.n0 != a.n1) s_doc->AddArcSegment(a);
    });

    // --- Selection ---
    m.def(P + "_clearselected", [&s_doc, errmsg]() { CHK(); s_doc->UnselectAll(); });
    m.def(P + "_selectgroup", [&s_doc, errmsg, &s_editAction](int grp) {
        CHK();
        for (auto &n : s_doc->nodelist)  if (n.InGroup == grp) n.IsSelected = true;
        for (auto &s : s_doc->linelist)  if (s.InGroup == grp) s.IsSelected = true;
        for (auto &a : s_doc->arclist)   if (a.InGroup == grp) a.IsSelected = true;
        for (auto &b : s_doc->blocklist) if (b.InGroup == grp) b.IsSelected = true;
        s_editAction = 4;   // OF leaves edit mode in group(4) after selectgroup
    });
    m.def(P + "_setgroup", [&s_doc, errmsg](int grp) {
        CHK();
        for (auto &n : s_doc->nodelist)  if (n.IsSelected) n.InGroup = grp;
        for (auto &s : s_doc->linelist)  if (s.IsSelected) s.InGroup = grp;
        for (auto &a : s_doc->arclist)   if (a.IsSelected) a.InGroup = grp;
        for (auto &b : s_doc->blocklist) if (b.IsSelected) b.InGroup = grp;
        s_doc->UnselectAll();   // OF deselects everything after grouping
    });

    // --- Select individual entities (return coords, or None if list empty) ---
    using XY   = std::tuple<double, double>;
    using XYXY = std::tuple<double, double, double, double>;
    m.def(P + "_selectnode", [&s_doc, errmsg](double x, double y) -> std::optional<XY> {
        CHK();
        if (s_doc->nodelist.empty()) return std::nullopt;
        int k = s_doc->ClosestNode(x, y);
        s_doc->nodelist[k].ToggleSelect();
        return XY{s_doc->nodelist[k].x, s_doc->nodelist[k].y};
    });
    m.def(P + "_selectlabel", [&s_doc, errmsg](double x, double y) -> std::optional<XY> {
        CHK();
        if (s_doc->blocklist.empty()) return std::nullopt;
        int k = s_doc->ClosestBlockLabel(x, y);
        s_doc->blocklist[k].ToggleSelect();
        return XY{s_doc->blocklist[k].x, s_doc->blocklist[k].y};
    });
    m.def(P + "_selectsegment", [&s_doc, errmsg](double x, double y) -> std::optional<XYXY> {
        CHK();
        if (s_doc->linelist.empty()) return std::nullopt;
        int k = s_doc->ClosestSegment(x, y);
        s_doc->linelist[k].ToggleSelect();
        auto &n0 = s_doc->nodelist[s_doc->linelist[k].n0];
        auto &n1 = s_doc->nodelist[s_doc->linelist[k].n1];
        return XYXY{n0.x, n0.y, n1.x, n1.y};
    });
    m.def(P + "_selectarcsegment", [&s_doc, errmsg](double x, double y) -> std::optional<XYXY> {
        CHK();
        if (s_doc->arclist.empty()) return std::nullopt;
        int k = s_doc->ClosestArcSegment(x, y);
        s_doc->arclist[k].ToggleSelect();
        auto &n0 = s_doc->nodelist[s_doc->arclist[k].n0];
        auto &n1 = s_doc->nodelist[s_doc->arclist[k].n1];
        return XYXY{n0.x, n0.y, n1.x, n1.y};
    });

    // --- Delete ---
    m.def(P + "_deleteselected", [&s_doc, errmsg]() {
        CHK();
        s_doc->DeleteSelectedSegments();
        s_doc->DeleteSelectedArcSegments();
        s_doc->DeleteSelectedNodes();
        s_doc->DeleteSelectedBlockLabels();
    });
    m.def(P + "_deleteselectednodes", [&s_doc, errmsg]() { CHK(); s_doc->DeleteSelectedNodes(); });
    m.def(P + "_deleteselectedlabels", [&s_doc, errmsg]() { CHK(); s_doc->DeleteSelectedBlockLabels(); });
    m.def(P + "_deleteselectedsegments", [&s_doc, errmsg]() { CHK(); s_doc->DeleteSelectedSegments(); });
    m.def(P + "_deleteselectedarcsegments", [&s_doc, errmsg]() { CHK(); s_doc->DeleteSelectedArcSegments(); });

    // --- Delete properties ---
    m.def(P + "_deletematerial", [&s_doc, errmsg](const std::string &name) {
        CHK();
        auto &list = s_doc->blockproplist;
        for (auto it = list.begin(); it != list.end(); ++it)
            if (it->BlockName == name) { list.erase(it); break; }
    });
    m.def(P + "_deleteboundprop", [&s_doc, errmsg](const std::string &name) {
        CHK();
        auto &list = s_doc->lineproplist;
        for (auto it = list.begin(); it != list.end(); ++it)
            if (it->BdryName == name) { list.erase(it); break; }
    });
    m.def(P + "_deletepointprop", [&s_doc, errmsg](const std::string &name) {
        CHK();
        auto &list = s_doc->nodeproplist;
        for (auto it = list.begin(); it != list.end(); ++it)
            if (it->PointName == name) { list.erase(it); break; }
    });

    // --- Transforms ---
    m.def(P + "_movetranslate", [&s_doc, errmsg](double dx, double dy, std::optional<int> ea) {
        CHK(); s_doc->TranslateMove(dx, dy, ea.value_or(4));
    });
    m.def(P + "_moverotate", [&s_doc, errmsg](double cx, double cy, double angle, std::optional<int> ea) {
        CHK(); s_doc->RotateMove(CComplex(cx, cy), angle, ea.value_or(4));
    });
    m.def(P + "_copytranslate", [&s_doc, errmsg](double dx, double dy, int copies, std::optional<int> ea) {
        CHK(); s_doc->TranslateCopy(dx, dy, copies, ea.value_or(4));
    });
    m.def(P + "_copyrotate", [&s_doc, errmsg](double cx, double cy, double angle, int copies, std::optional<int> ea) {
        CHK(); s_doc->RotateCopy(CComplex(cx, cy), angle, copies, ea.value_or(4));
    });
    m.def(P + "_mirror", [&s_doc, errmsg](double x1, double y1, double x2, double y2, std::optional<int> ea) {
        CHK(); s_doc->MirrorSelected(x1, y1, x2, y2, ea.value_or(4));
    });
    m.def(P + "_scale", [&s_doc, errmsg](double cx, double cy, double factor, std::optional<int> ea) {
        CHK(); s_doc->ScaleMove(cx, cy, factor, ea.value_or(4));
    });

    // --- External region ---
    m.def(P + "_attachouterspace", [&s_doc, errmsg]() {
        CHK(); for (auto &b : s_doc->blocklist) if (b.IsSelected) b.IsExternal = true;
    });
    m.def(P + "_detachouterspace", [&s_doc, errmsg]() {
        CHK(); for (auto &b : s_doc->blocklist) if (b.IsSelected) b.IsExternal = false;
    });
    m.def(P + "_attachdefault", [&s_doc, errmsg]() {
        CHK();
        bool found = false;
        for (auto &b : s_doc->blocklist) {
            if (b.IsSelected && !found) { b.IsDefault = true; found = true; }
            else b.IsDefault = false;
        }
    });
    m.def(P + "_detachdefault", [&s_doc, errmsg]() {
        CHK(); for (auto &b : s_doc->blocklist) if (b.IsSelected) b.IsDefault = false;
    });
    m.def(P + "_defineouterspace", [&s_doc, errmsg](double Zo, double Ro, double Ri) {
        CHK();
        s_doc->extZo = fabs(Zo);
        s_doc->extRo = fabs(Ro);
        s_doc->extRi = fabs(Ri);
        if (s_doc->extRo == 0 || s_doc->extRi == 0)
            s_doc->extZo = s_doc->extRo = s_doc->extRi = 0;
    });

    // --- Mesh / solve ---
    m.def(P + "_createmesh", [&s_doc, errmsg]() {
        CHK();
        char msg[256] = {};
        int ret = createMeshForDocument(s_doc, msg, sizeof(msg));
        if (ret != 0) throw std::runtime_error(msg);
    });
    auto analyze_fn = [&s_doc, errmsg](std::optional<int> hide) {
        CHK();
        char msg[256] = {};
        int ret = analyzeDocument(s_doc, hide.value_or(0), msg, sizeof(msg));
        if (ret != 0) throw std::runtime_error(msg);
    };
    m.def(P + "_analyze", analyze_fn);
    m.def(P + "_analyse", analyze_fn);  // British-spelling alias
    auto smartmesh_fn = [&s_doc, errmsg](std::optional<int> val) {
        CHK();
        if (val) s_doc->SmartMesh = (*val != 0) ? 1 : 0;
    };
    m.def(P + "_smartmesh", smartmesh_fn);
    m.def("smartmesh", smartmesh_fn);  // global alias (last physics registered wins)

    // --- File I/O ---
    m.def(P + "_saveas", [&s_doc, errmsg, P](const std::string &path) {
        CHK();
        std::string fp = fixpath(path);
        // Normalize the extension to lowercase so the stored path stays
        // canonical (matching open()), keeping save/analyze/solver naming
        // consistent across platforms.
        auto dot = fp.find_last_of('.');
        auto slash = fp.find_last_of("/\\");
        if (dot != std::string::npos && (slash == std::string::npos || dot > slash))
            for (size_t i = dot; i < fp.size(); ++i)
                fp[i] = (char)tolower((unsigned char)fp[i]);
        if (!s_doc->SaveDocument(fp))
            throw std::runtime_error("Could not save to " + fp);
        s_doc->filePath = fp;
        viewcmd(P + "_saveas:" + fp);
    });
    m.def(P + "_setcomment", [&s_doc, errmsg](std::optional<std::string> note) {
        CHK(); if (note) s_doc->ProblemNote = *note;
    });
    m.def(P + "_getboundingbox", [&s_doc, errmsg]() -> std::optional<XYXY> {
        CHK();
        // Use the document's bounding box, which (like OF's view-level box) accounts
        // for block labels and full arc extents, not just node coordinates.
        double xmin, ymin, xmax, ymax;
        if (!s_doc->getBoundingBox(xmin, ymin, xmax, ymax)) return std::nullopt;
        return XYXY{xmin, xmax, ymin, ymax};
    });
    m.def(P + "_gettitle", [&s_doc, errmsg]() -> std::string {
        CHK(); return s_doc->filePath;
    });

    // --- DXF ---
    m.def(P + "_readdxf", [&s_doc, errmsg](const std::string &fname, std::optional<double> tol) {
        CHK();
        std::string fp = fixpath(fname);
        s_doc->pushUndo();
        if (!ReadDXF(*s_doc, fp.c_str(), tol.value_or(0)))
            throw std::runtime_error("Failed to read DXF file");
        s_doc->EnforcePSLG(tol.value_or(0));
    });
    m.def(P + "_savedxf", [&s_doc, errmsg](const std::string &fname) {
        CHK();
        std::string fp = fixpath(fname);
        if (!WriteDXF(*s_doc, fp.c_str()))
            throw std::runtime_error("Failed to write DXF file");
    });
    m.def(P + "_createradius", [&s_doc, errmsg](double x, double y, double r) {
        CHK();
        r = fabs(r);
        int k = s_doc->ClosestNode(x, y);
        if (k < 0) return;
        if (!s_doc->CanCreateRadius(k))
            throw std::runtime_error("The specified point is not suitable for conversion into a radius");
        s_doc->pushUndo();
        if (!s_doc->CreateRadius(k, r))
            throw std::runtime_error("Could not make a radius of the prescribed dimension");
    });

    // --- Edit mode (P captured by value, used to build the command string) ---
    m.def(P + "_seteditmode", [P](const std::string &mode) {
        std::string md = mode;
        for (auto &ch : md) ch = (char)tolower((unsigned char)ch);
        int modeNum = 4;
        if (md == "nodes") modeNum = 0;
        else if (md == "segments") modeNum = 1;
        else if (md == "blocks") modeNum = 2;
        else if (md == "arcsegments") modeNum = 3;
        else if (md == "group") modeNum = 4;
        auto cb = get_view_command_callback();
        if (cb) { double args[4] = {(double)modeNum, 0, 0, 0}; cb((P + "_seteditmode").c_str(), 1, args); }
    });

    // --- View commands (P captured by value) ---
    m.def(P + "_zoomnatural", [P]() { viewcmd(P + "_zoomnatural"); });
    m.def(P + "_zoomout",     [P]() { viewcmd(P + "_zoomout"); });
    m.def(P + "_zoomin",      [P]() { viewcmd(P + "_zoomin"); });
    m.def(P + "_zoom", [P](std::optional<double> a0, std::optional<double> a1,
                          std::optional<double> a2, std::optional<double> a3) {
        viewcmd_fwd(P + "_zoom", a0, a1, a2, a3);
    });
    m.def(P + "_refreshview", [P]() { viewcmd(P + "_refreshview"); });
    m.def(P + "_shownames", [P](std::optional<double> a0, std::optional<double> a1,
                               std::optional<double> a2, std::optional<double> a3) {
        viewcmd_fwd(P + "_shownames", a0, a1, a2, a3);
    });
    m.def(P + "_showgrid", [P]() { viewcmd(P + "_showgrid"); });
    m.def(P + "_hidegrid", [P]() { viewcmd(P + "_hidegrid"); });
    m.def(P + "_gridsnap", [P](const std::string &flag) {
        viewcmd_fwd(P + "_gridsnap", gridSnapFlag(flag));
    });
    // *_setgrid(density, "cart"|"polar"). The coordinate mode is document state
    // -- it is written to the .fem/.fee/.feh/.fec header and the solver reads it
    // back for Prescribed A -- so it is set on the document here rather than
    // being forwarded to the view. That is what makes it work headless, where
    // there is no view at all. The grid spacing is the opposite: pure view
    // state, forwarded as before.
    m.def(P + "_setgrid", [&s_doc, errmsg, P](std::optional<double> density,
                                              std::optional<std::string> ptype) {
        int coords = -1;
        if (ptype) {
            coords = parseCoordMode(*ptype);
            if (coords < 0) throw std::runtime_error(COORD_MODE_ERR);
            CHK();
            s_doc->Coords = coords;
        }
        // Always two args; a negative slot means "not specified". Grid spacing
        // is positive by construction and Coords is 0 or 1, so -1 is unambiguous
        // and a bare mode change still reaches the view to trigger a repaint.
        double a[2] = {density.value_or(-1.0), (double)coords};
        viewcmd(P + "_setgrid", 2, a);
    });
    m.def(P + "_showmesh",  [P]() { viewcmd(P + "_showmesh"); });
    m.def(P + "_purgemesh", [P]() { viewcmd(P + "_purgemesh"); });
    m.def(P + "_resize", [P](std::optional<double> a0, std::optional<double> a1,
                            std::optional<double> a2, std::optional<double> a3) {
        viewcmd_fwd(P + "_resize", a0, a1, a2, a3);
    });
    m.def(P + "_minimize", [P]() { viewcmd(P + "_minimize"); });
    m.def(P + "_maximize", [P]() { viewcmd(P + "_maximize"); });
    m.def(P + "_restore",  [P]() { viewcmd(P + "_restore"); });
    m.def(P + "_savemetafile", [](std::optional<std::string>) {});  // No metafile output in femm-qt;
    // the filename is accepted and ignored so ported scripts do not raise.
    // Use *_savebitmap to save a picture of the view.

    m.def(P + "_close", [&s_doc, P]() {
        auto cb = get_view_command_callback();
        if (cb) { double args[4] = {}; cb((P + "_close").c_str(), 0, args); return; }
        if (s_doc) { doc_registry_remove(s_doc); delete s_doc; s_doc = nullptr; }
    });

    m.def(P + "_setfocus", [P](std::optional<std::string> docname) {
        // Canonical input extension for this physics: mi->.fem, ei->.fee,
        // hi->.feh, ci->.fec. Normalizing here makes a bare "temp" match the
        // "temp.fem" tab label and keeps the name sent to the GUI focus matcher
        // consistent with the activated document.
        std::string dn;
        if (docname) {
            dn = canonicalDocName(*docname, std::string(".fe") + P[0]);
            doc_registry_activate_input_byname(dn);
        }
        auto cb = get_view_command_callback();
        if (!cb) return;
        double args[4] = {};
        if (docname)
            cb((P + "_setfocus:" + dn).c_str(), 0, args);
        else
            cb((P + "_setfocus").c_str(), 0, args);
    });

    m.def(P + "_savebitmap", [P](const std::string &fname) {
        auto cb = get_view_command_callback();
        if (!cb) return;
        std::string fp = fixpath(fname);
        double args[4] = {};
        cb((P + "_savebitmap:" + fp).c_str(), 0, args);
    });
}

#undef CHK

#endif  // PY_EDITOR_COMMON_H
