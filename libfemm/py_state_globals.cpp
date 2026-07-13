// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors
//
// py_state_globals.cpp - Global FEMM document commands for the MicroPython
// scripting layer: newdocument, open, quit, setcurrentdirectory, main_*,
// plus the scripting-layer activate/deactivate hooks for the document registry.
//
// Separated from py_state.cpp because these depend on the per-physics document
// types (MagDocument, etc.) and editor registration functions, which pull in
// the full document model. py_state.cpp remains linkable in the lightweight
// test harness by stubbing register_global_femm_funcs/cleanup_global_femm_state.
#include "py_state.h"
#include "py_bind.h"
#include "script_callbacks.h"

#include "py_mag_editor.h"
#include "py_elec_editor.h"
#include "py_heat_editor.h"
#include "py_curr_editor.h"
#include "py_mag_postproc.h"
#include "py_elec_postproc.h"
#include "py_heat_postproc.h"
#include "py_curr_postproc.h"
#include "mag_document.h"
#include "elec_document.h"
#include "heat_document.h"
#include "curr_document.h"
#include "results_doc.h"
#include "elec_results_doc.h"
#include "heat_results_doc.h"
#include "curr_results_doc.h"
#include "utf8_io.h"      // femm_widen for a Unicode-safe chdir on Windows

#include <cstdio>
#include <cstdlib>
#include <optional>
#include <stdexcept>
#include <string>

#ifdef _WIN32
#include <direct.h>
#else
#include <unistd.h>
#endif

void register_global_femm_funcs()
{
    auto &m = femm_py::femm_module;

    m.def("quit", []() { std::exit(0); });
    m.alias("exit", "quit");

    // plot([title], x, [xlabel], y1, [lbl1], y2, [lbl2], ...) - X-Y line plot
    // of arbitrary script data (up to 7 curves sharing the x axis). An optional
    // leading string is the window title. A string immediately after a data
    // list is that list's label; x's label is the x-axis label, each y's label
    // names the curve in the legend (or, with a single curve, the y-axis).
    // Displays a plot window via the view-command callback; no-op when headless.
    // Max args = title + x + xlabel + 7*(y + label) = 17.
    m.def_raw("plot", 2, 17, [](size_t n, const mp_obj_t *a) -> mp_obj_t {
        // Optional leading title (x must be a list, so a leading string is
        // unambiguously the title).
        size_t i = 0;
        std::string title = "Plot";
        if (mp_obj_is_str(a[i])) { title = mp_obj_str_get_str(a[i]); i++; }

        if (i >= n || mp_obj_is_str(a[i]))
            throw std::runtime_error("plot: x must be a list of numbers");
        std::vector<double> x = femm_py::caster<std::vector<double>>::load(a[i]);
        i++;

        std::string xlabel;
        if (i < n && mp_obj_is_str(a[i])) { xlabel = mp_obj_str_get_str(a[i]); i++; }

        std::vector<std::vector<double>> ys;
        std::vector<std::string> labels;
        while (i < n) {
            if (mp_obj_is_str(a[i]))
                throw std::runtime_error("plot: expected a y data list but got a string");
            std::vector<double> y = femm_py::caster<std::vector<double>>::load(a[i]);
            i++;
            if (y.size() != x.size())
                throw std::runtime_error("plot: every y must have the same length as x");
            std::string lbl;
            if (i < n && mp_obj_is_str(a[i])) { lbl = mp_obj_str_get_str(a[i]); i++; }
            ys.push_back(std::move(y));
            labels.push_back(std::move(lbl));
            if (ys.size() > 7)
                throw std::runtime_error("plot: at most 7 y series are supported");
        }
        if (ys.empty())
            throw std::runtime_error("plot: need at least one y series");

        auto cb = get_view_command_callback();
        if (!cb) return mp_const_none;   // headless: nothing to display

        // Strings -> cmd ("plot\n<title>\n<xlabel>\n<ylabel>\n<lbl0>\n<lbl1>..."),
        // numbers -> args ([ncurves, npoints, x..., y0..., y1..., ...]).
        // With a single curve, also use its label as the y-axis label. Keep it
        // on the series too (it still names the quantity in the point-value
        // readout); the legend is only drawn for multi-curve plots, so a single
        // curve's label is never shown twice.
        std::string ylabel;
        if (ys.size() == 1) ylabel = labels[0];
        std::string cmd = "plot\n" + title + "\n" + xlabel + "\n" + ylabel;
        for (auto &l : labels) cmd += "\n" + l;

        size_t npts = x.size(), nc = ys.size();
        std::vector<double> data;
        data.reserve(2 + npts + nc * npts);
        data.push_back((double)nc);
        data.push_back((double)npts);
        data.insert(data.end(), x.begin(), x.end());
        for (auto &y : ys) data.insert(data.end(), y.begin(), y.end());

        cb(cmd.c_str(), (int)data.size(), data.data());
        return mp_const_none;
    });

    m.def("newdocument", [](std::optional<int> dtype) {
        int doctype = dtype.value_or(0);
        auto docCb = get_doc_opened_callback();
        if (doctype == 0) {
            auto *doc = new MagDocument();
            doc_registry_add(doc);
            register_mag_editor_py(doc);
            if (docCb) docCb(doc, "Untitled.fem");
        } else if (doctype == 1) {
            auto *doc = new ElecDocument();
            doc_registry_add(doc);
            register_elec_editor_py(doc);
            if (docCb) docCb(doc, "Untitled.fee");
        } else if (doctype == 2) {
            auto *doc = new HeatDocument();
            doc_registry_add(doc);
            register_heat_editor_py(doc);
            if (docCb) docCb(doc, "Untitled.feh");
        } else if (doctype == 3) {
            auto *doc = new CurrDocument();
            doc_registry_add(doc);
            register_curr_editor_py(doc);
            if (docCb) docCb(doc, "Untitled.fec");
        } else {
            std::fprintf(stderr, "newdocument(%d): unsupported document type\n", doctype);
        }
    });
    m.alias("create", "newdocument");

    m.def("_femm_open", [](const std::string &path) {
        std::string spath = fixpath(path);
        std::string ext;
        auto dot = spath.rfind('.');
        if (dot != std::string::npos) {
            ext = spath.substr(dot);
            // Dispatch on the extension case-insensitively, so .FEM/.Fee/etc.
            // open like .fem/.fee. Only the type key is lowered; spath keeps its
            // real case so the file still opens on case-sensitive filesystems.
            for (auto &ch : ext) ch = (char)tolower((unsigned char)ch);
        }
        // Canonical path: the same file with its extension forced to lowercase.
        // We OPEN the real on-disk spath (so the read still works on a
        // case-sensitive filesystem) but STORE this normalized path -- so
        // save/analyze and the solvers + tangle (which all rebuild
        // base+lowercase-ext) stay consistent with no per-reader change. On a
        // case-sensitive FS this leaves the original uppercase file as a
        // harmless orphan after the first save; Windows is unaffected.
        std::string canonical = (dot != std::string::npos)
                                    ? spath.substr(0, dot) + ext : spath;

        if (doc_registry_activate_input(canonical)) {
            auto vcb = get_view_command_callback();
            if (vcb) {
                std::string cmd = "setfocus:" + canonical;
                double args[4] = {};
                vcb(cmd.c_str(), 0, args);
            }
            return;
        }

        auto docCb = get_doc_opened_callback();
        auto openOne = [&](auto *doc, auto registerFn) {
            if (doc->OpenDocument(spath)) {
                doc->filePath = canonical;   // store the normalized lowercase-ext path
                doc_registry_add(doc);
                registerFn(doc);
                std::string title = canonical;
                auto slash = title.find_last_of("/\\");
                if (slash != std::string::npos) title = title.substr(slash + 1);
                if (docCb) docCb(doc, title.c_str());
            } else {
                delete doc;
                throw std::runtime_error("Failed to open " + spath);
            }
        };

        if (ext == ".fem")      openOne(new MagDocument(),  register_mag_editor_py);
        else if (ext == ".fee") openOne(new ElecDocument(), register_elec_editor_py);
        else if (ext == ".feh") openOne(new HeatDocument(), register_heat_editor_py);
        else if (ext == ".fec") openOne(new CurrDocument(), register_curr_editor_py);
        else throw std::runtime_error("open(\"" + spath + "\"): unsupported file type");
    });
    m.alias("opendocument", "_femm_open");

    m.def("setcurrentdirectory", [](const std::string &path) {
        std::string fp = fixpath(path);
#ifdef _WIN32
        // fp is UTF-8; the narrow _chdir would decode it as ANSI, so widen and
        // use _wchdir to honor a Unicode (umlaut/CJK) directory name.
        _wchdir(femm_widen(fp).c_str());
#else
        (void)chdir(fp.c_str());
#endif
    });
    m.alias("chdir", "setcurrentdirectory");

    auto viewcb = [](const char *cmd) {
        auto cb = get_view_command_callback();
        if (cb) { double a[4] = {}; cb(cmd, 0, a); }
    };
    m.def("main_restore",  [viewcb]() { viewcb("main_restore"); });
    m.def("main_maximize", [viewcb]() { viewcb("main_maximize"); });
    m.def("main_minimize", [viewcb]() { viewcb("main_minimize"); });
    m.def("main_resize",
        [](std::optional<double> a0, std::optional<double> a1,
           std::optional<double> a2, std::optional<double> a3) {
        auto cb = get_view_command_callback();
        if (!cb) return;
        double a[4] = {};
        int n = 0;
        if (a0) { a[0] = *a0; n = 1; }
        if (a1) { a[1] = *a1; n = 2; }
        if (a2) { a[2] = *a2; n = 3; }
        if (a3) { a[3] = *a3; n = 4; }
        cb("main_resize", n, a);
    });

    // Scripting-layer dispatch hooks for the document registry.
    set_script_doc_activate_callback([](DocumentBase *doc) {
        if (auto *m = dynamic_cast<MagDocument*>(doc))       register_mag_editor_py(m);
        else if (auto *e = dynamic_cast<ElecDocument*>(doc)) register_elec_editor_py(e);
        else if (auto *h = dynamic_cast<HeatDocument*>(doc)) register_heat_editor_py(h);
        else if (auto *c = dynamic_cast<CurrDocument*>(doc)) register_curr_editor_py(c);
    });
    set_script_doc_deactivate_callback([](DocumentBase *doc) {
        if (doc == (DocumentBase*)active_mag_document())       deactivate_mag_editor_py();
        else if (doc == (DocumentBase*)active_elec_document()) deactivate_elec_editor_py();
        else if (doc == (DocumentBase*)active_heat_document()) deactivate_heat_editor_py();
        else if (doc == (DocumentBase*)active_curr_document()) deactivate_curr_editor_py();
    });
    set_script_results_activate_callback([](ResultsDocBase *doc) {
        if (auto *m = dynamic_cast<ResultsDoc*>(doc))           register_mag_postproc_py(m);
        else if (auto *e = dynamic_cast<ElecResultsDoc*>(doc))  register_elec_postproc_py(e);
        else if (auto *h = dynamic_cast<HeatResultsDoc*>(doc))  register_heat_postproc_py(h);
        else if (auto *c = dynamic_cast<CurrResultsDoc*>(doc))  register_curr_postproc_py(c);
    });
    // When a results doc is removed (its window closed), clear BOTH bindings
    // that may still point at it: the postproc active doc (s_doc/s_rdoc) AND the
    // editor's cached results doc (s_resultsDoc). Otherwise a re-load can capture
    // the stale s_resultsDoc as `oldDoc` and, if the freed block is reused at the
    // same address, defeat the doc_registry_contains() guard and delete a live,
    // widget-owned doc. (s_resultsDoc is kept in sync with the postproc doc, so
    // it is either this doc or null here -- safe to clear.)
    set_script_results_deactivate_callback([](ResultsDocBase *doc) {
        if (doc == (ResultsDocBase*)active_results_document()) {
            deactivate_mag_postproc_py();  clear_mag_results_doc();
        } else if (doc == (ResultsDocBase*)active_elec_results()) {
            deactivate_elec_postproc_py(); clear_elec_results_doc();
        } else if (doc == (ResultsDocBase*)active_heat_results()) {
            deactivate_heat_postproc_py(); clear_heat_results_doc();
        } else if (doc == (ResultsDocBase*)active_curr_results()) {
            deactivate_curr_postproc_py(); clear_curr_results_doc();
        }
    });
}

void cleanup_global_femm_state()
{
    doc_registry_cleanup();
    deactivate_mag_editor_py();
    deactivate_elec_editor_py();
    deactivate_heat_editor_py();
    deactivate_curr_editor_py();
}
