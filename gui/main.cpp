// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors

// main.cpp — FEMM Qt GUI entry point
//
// Initializes all three layers:
//   Layer 1: libfemm (Python state)
//   Layer 2: TCP IPC server (embedded in GUI event loop)
//   Layer 3: Qt GUI (MainWindow with MDI)

#include <QApplication>
#include <QEventLoop>
#include <QProcess>
#include <QFileInfo>
#include <QMdiArea>
#include <QMdiSubWindow>
#include <QIcon>
#include <QPixmap>
#include <cstring>
#include <cstdlib>
#include <string>
#include "mainwindow.h"
#include "plot_widget.h"
#include "editor_widget.h"
#include "results_widget.h"
#include "results_doc.h"
#include <QMessageBox>
#include "mag_document.h"
#include "elec_document.h"
#include "document.h"
#include "py_state.h"
#include "script_callbacks.h"
#include "py_mag_editor.h"
#include "py_elec_editor.h"
#include "py_heat_editor.h"
#include "py_curr_editor.h"
#include "heat_document.h"
#include "curr_document.h"
#include "py_mag_postproc.h"
#include "py_elec_postproc.h"
#include "py_heat_postproc.h"
#include "py_curr_postproc.h"
#include "scalar_results_widget.h"
#include "tcp_server.h"
#include "solver_progress_dialog.h"

// Global pointer so the script callback can reach the main window
static MainWindow *s_mainWindow = nullptr;

// Find the MDI subwindow whose document matches the script-active document
// for the given command prefix (mi_, ei_, hi_, ci_, mo_, eo_, ho_, co_).
// Falls back to activeSubWindow, then the most recently opened subwindow.
static QMdiSubWindow *findSubWindowForCommand(const QString &cmd)
{
    QMdiArea *mdi = s_mainWindow->findChild<QMdiArea*>();
    if (!mdi) return nullptr;

    // Determine which Lua-active document this command targets
    DocumentBase *targetDoc = nullptr;
    ResultsDoc *targetRDoc = nullptr;
    if (cmd.startsWith("mi_"))      targetDoc  = active_mag_document();
    else if (cmd.startsWith("ei_")) targetDoc  = active_elec_document();
    else if (cmd.startsWith("hi_")) targetDoc  = active_heat_document();
    else if (cmd.startsWith("ci_")) targetDoc  = active_curr_document();
    else if (cmd.startsWith("mo_")) targetRDoc = active_results_document();

    // Search for matching subwindow
    if (targetDoc) {
        for (auto *sub : mdi->subWindowList()) {
            auto *ew = qobject_cast<EditorWidget*>(sub->widget());
            if (ew && ew->document() == targetDoc)
                return sub;
        }
    }
    if (targetRDoc) {
        for (auto *sub : mdi->subWindowList()) {
            auto *rw = qobject_cast<ResultsWidget*>(sub->widget());
            if (rw && rw->document() == targetRDoc)
                return sub;
        }
    }
    // Scalar results: eo_, ho_, co_
    if (cmd.startsWith("eo_") || cmd.startsWith("ho_") || cmd.startsWith("co_")) {
        for (auto *sub : mdi->subWindowList()) {
            auto *sw = qobject_cast<ScalarResultsWidget*>(sub->widget());
            if (!sw) continue;
            if (cmd.startsWith("eo_") && sw->document() == active_elec_results())
                return sub;
            if (cmd.startsWith("ho_") && sw->heatDocument() == active_heat_results())
                return sub;
            if (cmd.startsWith("co_") && sw->currDocument() == active_curr_results())
                return sub;
        }
    }

    // Fallback: active subwindow, then last in list
    auto *sub = mdi->activeSubWindow();
    if (!sub && !mdi->subWindowList().isEmpty())
        sub = mdi->subWindowList().last();
    return sub;
}

static void onDocOpened(DocumentBase *doc, const char *title)
{
    if (!s_mainWindow) return;
    // EditorWidget now takes DocumentBase* — works for all physics types
    s_mainWindow->addEditorTab(doc, QString::fromUtf8(title));
    // Add to recent files list (catches scripted open, scripted opendocument, etc.)
    // Canonicalize to absolute path so it works regardless of working directory.
    if (!doc->filePath.empty())
        s_mainWindow->addToRecentFiles(QFileInfo(QString::fromStdString(doc->filePath)).absoluteFilePath());
}

static void onMeshLoaded(const MeshDisplayData *data)
{
    if (!s_mainWindow) return;
    // Find the active editor and pass mesh data directly
    auto *e = s_mainWindow->activeEditorWidget();
    if (e) {
        e->loadMeshFromData(data->numNodes, data->nodeX, data->nodeY,
                            data->numTris, data->triNodes, data->triRegion);
    }
}

static void onResultsOpened(const char *ansPath, void *doc)
{
    if (s_mainWindow)
        s_mainWindow->openResultsFile(QString::fromUtf8(ansPath), doc);
}

static const char *onViewCommand(const char *cmd, int nargs, double *args)
{
    if (!s_mainWindow) return nullptr;
    static std::string resultBuf;
    QString c = QString::fromUtf8(cmd);

    auto *e = s_mainWindow->activeEditorWidget();
    // Find active results widget via the public accessor
    // For now, use the approach: if it starts with mo_, it's postprocessor
    bool isPre = c.startsWith("mi_");

    // Python console show/hide
    if (c == "showconsole") { s_mainWindow->showPyConsole(); return nullptr; }
    if (c == "hideconsole") { s_mainWindow->hidePyConsole(); return nullptr; }

    // Script X-Y plot of arbitrary data (the global plot() function).
    //   cmd  = "plot\n<title>\n<xlabel>\n<ylabel>\n<label0>\n<label1>..."
    //   args = [ncurves, npoints, x..., y0..., y1..., ...]
    if (c.startsWith("plot\n")) {
        if (nargs < 2) return nullptr;
        int nc = (int)args[0];
        int np = (int)args[1];
        if (nc < 1 || np < 1 || (long)nargs < 2 + (long)np + (long)nc * np)
            return nullptr;
        QStringList meta = c.split('\n');
        QString title = meta.value(1);
        const double *xd = args + 2;
        auto *pdlg = new PlotDialog(title.isEmpty() ? "Plot" : title, s_mainWindow);
        pdlg->setAttribute(Qt::WA_DeleteOnClose);
        for (int ci = 0; ci < nc; ci++) {
            PlotSeries s;
            s.label = meta.value(4 + ci);
            const double *yd = args + 2 + np + (long)ci * np;
            s.x.assign(xd, xd + np);
            s.y.assign(yd, yd + np);
            pdlg->plot()->addSeries(s);
        }
        pdlg->plot()->setAxisLabels(meta.value(2), meta.value(3));
        pdlg->show();
        return nullptr;
    }

    // saveas: update tab title after mi_saveas/ei_saveas/etc.
    if (c.contains("saveas:")) {
        QString fname = c.mid(c.indexOf(':') + 1);
        QMdiSubWindow *sub = findSubWindowForCommand(c);
        if (sub) sub->setWindowTitle(QFileInfo(fname).fileName());
        return nullptr;
    }

    // savebitmap: format is "mi_savebitmap:filename" or "mo_savebitmap:filename"
    if (c.contains("savebitmap:")) {
        QString fname = c.mid(c.indexOf(':') + 1);
        QWidget *w = isPre ? (QWidget*)e : (QWidget*)s_mainWindow->activeEditorWidget();
        // For postprocessor, we need the results widget — get it via MDI
        QMdiSubWindow *sub = s_mainWindow->findChild<QMdiArea*>()->activeSubWindow();
        if (sub && sub->widget()) {
            QPixmap pix = sub->widget()->grab();
            // Determine format from extension
            if (fname.endsWith(".png", Qt::CaseInsensitive)) pix.save(fname, "PNG");
            else if (fname.endsWith(".jpg", Qt::CaseInsensitive) || fname.endsWith(".jpeg", Qt::CaseInsensitive))
                pix.save(fname, "JPEG");
            else pix.save(fname, "PNG");  // default to PNG
        }
        return nullptr;
    }

    // gettitle
    if (c.endsWith("gettitle")) {
        if (e && e->document()) {
            resultBuf = e->document()->filePath;
            return resultBuf.c_str();
        }
        return "";
    }

    // Find the subwindow matching the script-active document for this command.
    // Handles multiple open documents and out-of-focus scenarios (e.g. TCP).
    QMdiSubWindow *sub = findSubWindowForCommand(c);
    QWidget *widget = sub ? sub->widget() : nullptr;

    // close
    if (c.endsWith("close")) {
        if (sub) sub->close();
        return nullptr;
    }

    // Main window management (main_restore, main_maximize, etc.)
    if (c.startsWith("main_")) {
        if (c == "main_restore")  s_mainWindow->showNormal();
        else if (c == "main_maximize") s_mainWindow->showMaximized();
        else if (c == "main_minimize") s_mainWindow->showMinimized();
        else if (c == "main_resize" && nargs >= 2)
            s_mainWindow->resize((int)args[0], (int)args[1]);
        return nullptr;
    }

    // MDI subwindow management (mi_minimize, mo_restore, etc.)
    if (c.endsWith("minimize")) { if (sub) sub->showMinimized(); return nullptr; }
    if (c.endsWith("maximize")) { if (sub) sub->showMaximized(); return nullptr; }
    if (c.endsWith("restore"))  { if (sub) sub->showNormal(); return nullptr; }
    if (c.endsWith("resize") && nargs >= 2) {
        if (sub) sub->resize((int)args[0], (int)args[1]);
        return nullptr;
    }
    if (c.contains("setfocus")) {
        // Focus the subwindow whose document matches the given filename.
        // The filename is encoded after ":" in the command string.
        // Script pointer updates are handled by the document registry
        // (doc_registry_activate_input) before this callback is invoked.
        QMdiArea *mdi = s_mainWindow->findChild<QMdiArea*>();
        if (!mdi) return nullptr;
        int colonIdx = c.indexOf(':');
        if (colonIdx >= 0) {
            QString docname = c.mid(colonIdx + 1).trimmed();
            if (!docname.isEmpty()) {
                // Search all subwindows for a matching document filePath
                for (auto *sw : mdi->subWindowList()) {
                    QString fp;
                    if (auto *ew = qobject_cast<EditorWidget*>(sw->widget()))
                        fp = QString::fromStdString(ew->document()->filePath);
                    else if (auto *rw = qobject_cast<ResultsWidget*>(sw->widget()))
                        fp = QString::fromStdString(rw->document()->filePath);
                    else if (auto *srw = qobject_cast<ScalarResultsWidget*>(sw->widget())) {
                        if (srw->view().filePath)
                            fp = QString::fromStdString(*srw->view().filePath);
                    }
                    if (!fp.isEmpty() && (fp.endsWith(docname, Qt::CaseInsensitive) ||
                        QFileInfo(fp).fileName().compare(docname, Qt::CaseInsensitive) == 0)) {
                        mdi->setActiveSubWindow(sw);
                        sw->setFocus();
                        return nullptr;
                    }
                }
            }
        }
        // Fallback: no name given or no match — activate the current sub
        if (sub) {
            mdi->setActiveSubWindow(sub);
            sub->setFocus();
        }
        return nullptr;
    }

    // Zoom commands
    if (c.endsWith("zoomnatural")) {
        auto *ew = qobject_cast<EditorWidget*>(widget);
        auto *rw = qobject_cast<ResultsWidget*>(widget);
        if (ew) ew->zoomFit(); else if (rw) rw->zoomFit();
        return nullptr;
    }
    if (c.endsWith("zoomin")) {
        auto *ew = qobject_cast<EditorWidget*>(widget);
        auto *rw = qobject_cast<ResultsWidget*>(widget);
        if (ew) ew->zoomIn(); else if (rw) rw->zoomIn();
        return nullptr;
    }
    if (c.endsWith("zoomout")) {
        auto *ew = qobject_cast<EditorWidget*>(widget);
        auto *rw = qobject_cast<ResultsWidget*>(widget);
        if (ew) ew->zoomOut(); else if (rw) rw->zoomOut();
        return nullptr;
    }
    if (c.endsWith("_zoom") && nargs >= 4) {
        auto *ew = qobject_cast<EditorWidget*>(widget);
        auto *rw = qobject_cast<ResultsWidget*>(widget);
        auto *sw = qobject_cast<ScalarResultsWidget*>(widget);
        if (ew) ew->zoomTo(args[0], args[1], args[2], args[3]);
        else if (rw) rw->zoomTo(args[0], args[1], args[2], args[3]);
        else if (sw) sw->zoomTo(args[0], args[1], args[2], args[3]);
        return nullptr;
    }
    if (c.endsWith("refreshview")) {
        if (widget) widget->update();
        return nullptr;
    }

    // Grid commands
    if (c.endsWith("showgrid")) {
        auto *ew = qobject_cast<EditorWidget*>(widget);
        auto *rw = qobject_cast<ResultsWidget*>(widget);
        auto *sw = qobject_cast<ScalarResultsWidget*>(widget);
        if (ew) { ew->setShowGrid(true); s_mainWindow->syncPreprocessorMenus(ew); }
        else if (rw) { rw->setShowGrid(true); s_mainWindow->syncPostprocessorMenus(rw); }
        else if (sw) { sw->setShowGrid(true); s_mainWindow->syncPostprocessorMenus(sw); }
        return nullptr;
    }
    if (c.endsWith("hidegrid")) {
        auto *ew = qobject_cast<EditorWidget*>(widget);
        auto *rw = qobject_cast<ResultsWidget*>(widget);
        auto *sw = qobject_cast<ScalarResultsWidget*>(widget);
        if (ew) { ew->setShowGrid(false); s_mainWindow->syncPreprocessorMenus(ew); }
        else if (rw) { rw->setShowGrid(false); s_mainWindow->syncPostprocessorMenus(rw); }
        else if (sw) { sw->setShowGrid(false); s_mainWindow->syncPostprocessorMenus(sw); }
        return nullptr;
    }
    if (c.endsWith("gridsnap")) {
        auto *ew = qobject_cast<EditorWidget*>(widget);
        auto *rw = qobject_cast<ResultsWidget*>(widget);
        auto *sw = qobject_cast<ScalarResultsWidget*>(widget);
        bool on = (nargs > 0 && args[0] != 0);
        if (ew) { ew->setSnapToGrid(on); s_mainWindow->syncPreprocessorMenus(ew); }
        else if (rw) { rw->setSnapToGrid(on); s_mainWindow->syncPostprocessorMenus(rw); }
        else if (sw) { sw->setSnapToGrid(on); s_mainWindow->syncPostprocessorMenus(sw); }
        return nullptr;
    }
    if (c.endsWith("setgrid")) {
        auto *ew = qobject_cast<EditorWidget*>(widget);
        auto *rw = qobject_cast<ResultsWidget*>(widget);
        auto *sw = qobject_cast<ScalarResultsWidget*>(widget);
        if (nargs > 0) {
            if (ew) ew->setGridSize(args[0]);
            else if (rw) rw->setGridSize(args[0]);
            else if (sw) sw->setGridSize(args[0]);
        }
        return nullptr;
    }

    // Preprocessor-specific
    if (c == "mi_showmesh") {
        auto *ew = qobject_cast<EditorWidget*>(widget);
        if (ew && ew->meshEdgeCount() == 0) ew->loadMesh();
        if (ew) ew->setShowMesh(true);
        return nullptr;
    }
    if (c == "mi_purgemesh") {
        auto *ew = qobject_cast<EditorWidget*>(widget);
        if (ew) { ew->clearMesh(); ew->setShowMesh(false); }
        return nullptr;
    }
    if (c == "mi_shownames") {
        auto *ew = qobject_cast<EditorWidget*>(widget);
        if (ew) { ew->setShowNames(nargs == 0 || args[0] != 0); s_mainWindow->syncPreprocessorMenus(ew); }
        return nullptr;
    }

    if (c.endsWith("seteditmode")) {
        int mode = (nargs > 0) ? (int)args[0] : 0;
        if (c[1] == 'o')  // postprocessor: *o_seteditmode
            s_mainWindow->setPostprocessorEditMode(mode);
        else              // preprocessor: *i_seteditmode
            s_mainWindow->setPreprocessorEditMode(mode);
        return nullptr;
    }

    // ----------------------------------------------------------------
    // Postprocessor display commands (*o_show/hide density/contour/mesh/etc.)
    // ----------------------------------------------------------------
    auto *rw = qobject_cast<ResultsWidget*>(widget);
    auto *sw = qobject_cast<ScalarResultsWidget*>(widget);

    if (c.endsWith("showmesh")) {
        if (rw) rw->setShowMesh(true);
        else if (sw) sw->setShowMesh(true);
        if (rw || sw) s_mainWindow->syncPostprocessorMenus(widget);
        return nullptr;
    }
    if (c.endsWith("hidemesh")) {
        if (rw) rw->setShowMesh(false);
        else if (sw) sw->setShowMesh(false);
        if (rw || sw) s_mainWindow->syncPostprocessorMenus(widget);
        return nullptr;
    }
    if (c.endsWith("showpoints")) {
        if (rw) { rw->m_showPoints = true; rw->update(); }
        else if (sw) { sw->m_showPoints = true; sw->update(); }
        if (rw || sw) s_mainWindow->syncPostprocessorMenus(widget);
        return nullptr;
    }
    if (c.endsWith("hidepoints")) {
        if (rw) { rw->m_showPoints = false; rw->update(); }
        else if (sw) { sw->m_showPoints = false; sw->update(); }
        if (rw || sw) s_mainWindow->syncPostprocessorMenus(widget);
        return nullptr;
    }
    if (c.endsWith("shownames")) {
        bool on = (nargs == 0 || args[0] != 0);
        if (rw) { rw->m_showNames = on; rw->update(); }
        else if (sw) { sw->m_showNames = on; sw->update(); }
        if (rw || sw) s_mainWindow->syncPostprocessorMenus(widget);
        return nullptr;
    }

    if (c.endsWith("showdensity")) {
        if (nargs >= 1 && (int)args[0] == -1) {
            // legend == -1: restore defaults
            if (rw) {
                auto *doc = rw->document();
                int pt = rw->m_densityPlotType > 0 ? rw->m_densityPlotType : 1;
                rw->m_densityPlotType = pt;
                rw->m_showLegend = true;
                rw->m_greyScale = false;
                doc->PlotBounds[pt - 1][0] = doc->d_PlotBounds[pt - 1][0];
                doc->PlotBounds[pt - 1][1] = doc->d_PlotBounds[pt - 1][1];
            } else if (sw) {
                auto &v = sw->view();
                int pt = sw->m_densityPlotType > 0 ? sw->m_densityPlotType : 1;
                sw->m_densityPlotType = pt;
                sw->m_showLegend = true;
                sw->m_greyScale = false;
                if (v.PlotBounds) {
                    v.PlotBounds[pt - 1][0] = v.d_PlotBounds[pt - 1][0];
                    v.PlotBounds[pt - 1][1] = v.d_PlotBounds[pt - 1][1];
                }
            }
        } else if (nargs >= 3) {
            int legend = (int)args[0];
            int gscale = (int)args[1];
            int plotType = (int)args[2];
            if (rw) {
                rw->m_densityPlotType = plotType;
                rw->m_showLegend = (legend != 0);
                rw->m_greyScale = (gscale != 0);
                if (nargs >= 5 && plotType > 0) {
                    double ub = args[3], lb = args[4];
                    if (lb > ub) { double t = lb; lb = ub; ub = t; }
                    rw->document()->PlotBounds[plotType - 1][0] = lb;
                    rw->document()->PlotBounds[plotType - 1][1] = ub;
                }
            } else if (sw) {
                sw->m_densityPlotType = plotType;
                sw->m_showLegend = (legend != 0);
                sw->m_greyScale = (gscale != 0);
                if (nargs >= 5 && plotType > 0 && sw->view().PlotBounds) {
                    double ub = args[3], lb = args[4];
                    if (lb > ub) { double t = lb; lb = ub; ub = t; }
                    sw->view().PlotBounds[plotType - 1][0] = lb;
                    sw->view().PlotBounds[plotType - 1][1] = ub;
                }
            }
        }
        if (rw) { rw->m_showVectors = false; rw->setShowDensity(true); }
        else if (sw) { sw->m_showVectors = false; sw->setShowDensity(true); }
        if (rw || sw) s_mainWindow->syncPostprocessorMenus(widget);
        return nullptr;
    }

    if (c.endsWith("hidedensity")) {
        if (rw) { rw->m_densityPlotType = 0; rw->setShowDensity(false); }
        else if (sw) { sw->m_densityPlotType = 0; sw->setShowDensity(false); }
        if (rw || sw) s_mainWindow->syncPostprocessorMenus(widget);
        return nullptr;
    }

    if (c.endsWith("showcontour")) {
        if (nargs >= 1) {
            int nc = (int)args[0];
            if (nc == -1) {
                // Restore defaults
                if (rw) {
                    auto *doc = rw->document();
                    doc->A_low = doc->A_lb;
                    doc->A_high = doc->A_ub;
                    rw->setNumContours(19);
                } else if (sw) {
                    auto &v = sw->view();
                    if (v.A_Low) *v.A_Low = *v.A_lb;
                    if (v.A_High) *v.A_High = *v.A_ub;
                    sw->setNumContours(19);
                }
            } else if (nargs >= 3) {
                double alow = args[1], ahigh = args[2];
                if (rw) {
                    rw->setNumContours(nc);
                    rw->document()->A_low = alow;
                    rw->document()->A_high = ahigh;
                    // Contour type for magnetics (AC)
                    if (nargs >= 4) {
                        int ct = (int)args[3];
                        if (ct == 0) { rw->m_showAr = true; rw->m_showAi = false; }
                        else if (ct == 1) { rw->m_showAr = false; rw->m_showAi = true; }
                        else { rw->m_showAr = true; rw->m_showAi = true; }
                    }
                } else if (sw) {
                    sw->setNumContours(nc);
                    auto &v = sw->view();
                    if (v.A_Low) *v.A_Low = alow;
                    if (v.A_High) *v.A_High = ahigh;
                }
            }
        }
        if (rw) rw->setShowContours(true);
        else if (sw) sw->setShowContours(true);
        if (rw || sw) s_mainWindow->syncPostprocessorMenus(widget);
        return nullptr;
    }

    if (c.endsWith("hidecontour")) {
        if (rw) rw->setShowContours(false);
        else if (sw) sw->setShowContours(false);
        if (rw || sw) s_mainWindow->syncPostprocessorMenus(widget);
        return nullptr;
    }

    if (c.endsWith("vectorplot")) {
        if (nargs >= 1) {
            int type = (int)args[0];
            if (type == 0) {
                if (rw) { rw->m_showVectors = false; rw->update(); }
                else if (sw) { sw->m_showVectors = false; sw->update(); }
            } else {
                if (rw) {
                    rw->m_showVectors = true;
                    rw->m_vectorType = type - 1;
                    rw->setShowDensity(false);
                    rw->update();
                } else if (sw) {
                    sw->m_showVectors = true;
                    sw->m_vectorType = type - 1;
                    sw->setShowDensity(false);
                    sw->update();
                }
            }
        }
        if (rw || sw) s_mainWindow->syncPostprocessorMenus(widget);
        return nullptr;
    }

    return nullptr;
}

static int onRunSolver(const char *exePath, const char *basePath, int hide)
{
    if (hide) {
        // Silent mode (e.g. mi_analyze(1)) — no UI, just run and wait.
        QProcess proc;
        proc.setProgram(QString::fromUtf8(exePath));
        proc.setArguments({ QString::fromUtf8(basePath) });
        proc.setProcessChannelMode(QProcess::ForwardedChannels);

        QEventLoop loop;
        int  exitCode    = 0;
        bool finishedOk  = false;
        QObject::connect(&proc,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            &loop, [&loop, &exitCode, &finishedOk]
                   (int code, QProcess::ExitStatus) {
                exitCode   = code;
                finishedOk = true;
                loop.quit();
            });
        QObject::connect(&proc, &QProcess::errorOccurred,
            &loop, [&loop](QProcess::ProcessError) { loop.quit(); });

        proc.start();
        if (proc.state() != QProcess::NotRunning)
            loop.exec();
        return finishedOk ? exitCode : -1;
    }

    // Show progress dialog with live output and abort button
    SolverProgressDialog dlg(s_mainWindow);
    return dlg.runSolver(QString::fromUtf8(exePath),
                         QString::fromUtf8(basePath));
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("FEMM");
    app.setApplicationVersion("5.0.0");
    app.setOrganizationName("FEMM");
    app.setWindowIcon(QIcon(":/icons/femm.ico"));

    // Parse command line for --port and script arguments
    int port = 48015;
    const char *script = nullptr;
    for (int i = 1; i < argc; i++) {
        if ((strcmp(argv[i], "-port") == 0 || strcmp(argv[i], "--port") == 0) && i + 1 < argc) {
            port = atoi(argv[++i]);
        } else if (argv[i][0] != '-' && !script) {
            script = argv[i];
        }
    }

    // Layer 1: Python engine
    init_py();
    set_gui_mode(true);

    // Layer 2: IPC server (runs inside Qt event loop via QTimer)
    TcpServer server;
    server.listen(port);

    // Layer 3: GUI
    MainWindow w(&server);
    s_mainWindow = &w;
    set_doc_opened_callback(onDocOpened);
    set_results_opened_callback(onResultsOpened);
    set_run_solver_callback(onRunSolver);
    set_mesh_loaded_callback(onMeshLoaded);
    set_view_command_callback(onViewCommand);
    server.setPostCommandCallback([]() {
        if (!s_mainWindow) return;
        // Throttle the MDI subwindow repaint via MainWindow's ticker.
        // First command after a quiet period repaints immediately;
        // subsequent commands within the same burst are coalesced into
        // ~10 FPS updates so a script drawing a motor with thousands of
        // mi_addnode/mi_addsegment calls doesn't pay ~25ms of repaint
        // per call. See MainWindow::scheduleDelayedRepaint for details.
        s_mainWindow->scheduleDelayedRepaint();
    });
    w.show();

    // Run any startup script passed on command line
    if (script) {
        char errmsg[1024] = {};
        std::string cmd = std::string("dofile(\"") + script + "\")";
        if (exec_python(cmd.c_str(), errmsg, sizeof(errmsg)) != 0)
            QMessageBox::warning(&w, "Script Error", QString::fromUtf8(errmsg));
    }

    int result = app.exec();

    s_mainWindow = nullptr;
    set_doc_opened_callback(nullptr);
    set_results_opened_callback(nullptr);
    set_run_solver_callback(nullptr);
    set_mesh_loaded_callback(nullptr);
    server.stop();
    close_py();
    return result;
}
