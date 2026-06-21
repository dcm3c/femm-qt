// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors

// mainwindow.h — FEMM Qt main window
// Switches menus and toolbars between preprocessor and postprocessor
// depending on which MDI tab is active.
#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QMdiArea>
#include <QTimer>
#include <QLabel>
#include <QActionGroup>
#include <QToolBar>
#include <QMenuBar>

class QSocketNotifier;
class TcpServer;
class DocumentBase;
class EditorWidget;
class ResultsDoc;
class ResultsWidget;
class ScalarResultsWidget;
class OutputDialog;
class PyConsole;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(TcpServer *server, QWidget *parent = nullptr);
    ~MainWindow();

    // Called by script callbacks
    EditorWidget *addEditorTab(DocumentBase *doc, const QString &title);
    void openResultsFile(const QString &ansPath, void *scriptDoc = nullptr);
    EditorWidget *activeEditorWidget() const;
    OutputDialog *outputDialog();
    void addToRecentFiles(const QString &path);
    void removeFromRecentFiles(const QString &path);
    void setPostprocessorEditMode(int mode);
    void setPreprocessorEditMode(int mode);
    void showPyConsole();
    void hidePyConsole();

    // Called from the IPC server's post-command callback after every action
    // command (one that didn't return data via __ipc_capture). Throttles
    // MDI subwindow repaints during long bursts of action commands so that
    // a script drawing a motor with thousands of mi_addnode/mi_addsegment
    // calls doesn't pay ~25ms of repaint per call.
    void scheduleDelayedRepaint();

private slots:
    void onListenSocketReady();
    void onClientSocketReady();
    void onRepaintTick();
    void onNew();
    void onOpen();
    void onSave();
    void onSaveAs();

    // Pre-processor
    void onNodeMode();
    void onSegmentMode();
    void onArcMode();
    void onBlockMode();
    void onGroupMode();
    void onZoomIn();
    void onZoomOut();
    void onZoomFit();
    void onZoomWindow();
    void onMove();
    void onCopy();
    void onScale();
    void onMirror();
    void onDeleteSelected();
    void onCreateRadius();
    void onCreateMesh();
    void onAnalyze();
    void onViewResults();

    // Post-processor
    void onPPPointMode();
    void onPPContourMode();
    void onPPAreaMode();
    void onPPToggleDensity();
    void onPPToggleContours();
    void onPPToggleMesh();

    // Common
    void updateCoordinates(double x, double y);
    void updateStatus(const QString &msg);
    void onEditActionChanged(int action);
    void onSubWindowActivated(QMdiSubWindow *sub);

public:
    // Sync the postprocessor View-menu / toolbar checkmarks to the given
    // active widget (a ResultsWidget or ScalarResultsWidget). Called both
    // on document load, on tab switch, and from script view command handlers.
    void syncPostprocessorMenus(QWidget *active);

    // Sync the preprocessor Grid-menu / toolbar checkmarks to the given
    // active editor. Same rationale: called on load and on tab switch.
    void syncPreprocessorMenus(EditorWidget *editor);

protected:
    // Application-wide event filter: while a script is running, Esc interrupts
    // it (works regardless of focus or whether the console is visible).
    bool eventFilter(QObject *obj, QEvent *event) override;

private:
    void createCommonMenus();
    void createPreprocessorUI();
    void createPostprocessorUI();
    void createLeftToolbars();
    void createStatusBar();
    void showEmptyUI();
    void showPreprocessorUI();
    void showPostprocessorUI();

    EditorWidget *activeEditor() const;
    ResultsWidget *activeResults() const;
    ScalarResultsWidget *activeScalarResults() const;

    // Recent files
    void updateRecentFilesMenu();
    QMenu *m_recentFilesMenu = nullptr;
    static constexpr int MaxRecentFiles = 10;

    void doRepaintNow();

    QMdiArea *m_mdiArea;
    QSocketNotifier *m_listenNotifier = nullptr;
    QSocketNotifier *m_clientNotifier = nullptr;
    QTimer *m_repaintTicker = nullptr;
    bool m_repaintDirty = false;
    TcpServer *m_server;
    QLabel *m_coordLabel;
    QLabel *m_statusLabel;

    // File menu actions that are preprocessor-only
    QAction *m_actSave = nullptr;
    QAction *m_actSaveAs = nullptr;

    // Preprocessor UI
    QToolBar *m_preTopBar1 = nullptr;  // File: New, Open
    QToolBar *m_preTopBar2 = nullptr;  // Editor: modes, mesh, solve, etc.
    QMenu *m_preMenuProblem = nullptr;
    QMenu *m_preMenuView = nullptr;
    QMenu *m_preMenuGrid = nullptr;
    QMenu *m_preMenuOperation = nullptr;
    QMenu *m_preMenuProperties = nullptr;
    QMenu *m_preMenuMesh = nullptr;
    QMenu *m_preMenuAnalysis = nullptr;

    // Edit mode action group
    QActionGroup *m_modeGroup = nullptr;
    QAction *m_actNode = nullptr;
    QAction *m_actSegment = nullptr;
    QAction *m_actArc = nullptr;
    QAction *m_actBlock = nullptr;
    QAction *m_actGroup = nullptr;

    // Postprocessor UI
    QToolBar *m_ppTopBar = nullptr;
    QMenu *m_ppMenuView = nullptr;
    QMenu *m_ppMenuOperation = nullptr;

    // Postprocessor mode group
    QActionGroup *m_ppModeGroup = nullptr;
    QAction *m_ppActPoint = nullptr;
    QAction *m_ppActContour = nullptr;
    QAction *m_ppActArea = nullptr;
    QAction *m_ppActDensity = nullptr;
    QAction *m_ppActContours = nullptr;
    QAction *m_ppActMesh = nullptr;
    QAction *m_ppMeshTB = nullptr;
    QAction *m_ppActPoints = nullptr;   // View | Show Points
    QAction *m_ppActNames = nullptr;    // View | Show Block Names

    // Grid toggle actions (menu + left nav toolbar — kept in sync)
    QAction *m_showGridMenu = nullptr;
    QAction *m_showGridTB = nullptr;
    QAction *m_snapGridMenu = nullptr;
    QAction *m_showNamesMenu = nullptr;
    QAction *m_snapGridTB = nullptr;

    // Left toolbars (shared)
    QToolBar *m_leftScript = nullptr;
    PyConsole *m_pyConsole = nullptr;
    QToolBar *m_leftNav = nullptr;

    // Track which UI is currently shown
    // Track which UI state is shown: -1=empty, 0=preprocessor, 1=postprocessor
    int m_uiState = -1;
    QWidget *m_winButtons = nullptr;

    // Common menus/toolbars that hide when no document is open
    QMenu *m_editMenu = nullptr;
    QAction *m_actUndo = nullptr;
    QAction *m_actRedo = nullptr;
    QAction *m_actDeleteSelected = nullptr;
    QAction *m_actExteriorRegion = nullptr;
    QMenu *m_windowMenu = nullptr;

    // File menu DXF actions (preprocessor only)
    QAction *m_dxfSep = nullptr;
    QAction *m_importDxf = nullptr;
    QAction *m_exportDxf = nullptr;

    // Modeless output dialog for postprocessor results
    OutputDialog *m_outputDlg = nullptr;
};

#endif // MAINWINDOW_H
