// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 David Meeker

#include "StdAfx.h"
#ifdef _OPENMP
#include <omp.h>
#endif
#include "fkn.h"
#include "fknDlg.h"
#include "complex.h"
#include "spars.h"
#include "mesh.h"
#include "femmedoccore.h"
#include "tangle_mesh.h"
#include "utf8_io.h"
extern int LoadMeshFromTangle(CFemmeDocCore& Doc, const Mesh& tmesh);
int main(int argc, char* argv[]) {
    std::vector<std::string> args = femm_utf8_args(argc, argv);
#ifdef _OPENMP
    int max_t = omp_get_max_threads();
    if (max_t > 4) omp_set_num_threads(4);
    fprintf(stderr, "OpenMP: %d threads\n", omp_get_max_threads());
#endif
    CFknDlg theView; CFemmeDocCore Doc; char PathName[4096];
    if ((int)args.size() < 2) { fprintf(stderr, "Usage: csolv <filename>\n"); return 1; }
    snprintf(PathName, sizeof(PathName), "%s", args[1].c_str());
    Doc.PathName = PathName; Doc.TheView = &theView;
    if (Doc.OnOpenDocument() != TRUE) { fprintf(stderr, "Problem loading .fec file\n"); return 7; }
    // "<None>" (unassigned material) -> file index 0 -> BlockType -1; the mesher reads
    // it as a hole and voids the region (empty-mesh crash if it is the only one).
    // Always reject: a default block label only covers wholly-unlabeled regions.
    for (int i = 0; i < Doc.NumBlockLabels; i++)
        if (Doc.labellist[i].BlockType < 0) { fprintf(stderr, "A block label has no material assigned.\n"); return 10; }
    fprintf(stderr, "Meshing...\n");
    Mesh tmesh;
    // Full .fec name -> mesh the current-flow geometry, not a same-named .fem.
    { int trc = tangle_mesh_fem(std::string(PathName) + ".fec", tmesh);   // distinct code per tangle failure category
      if (trc == TANGLE_ERR_NO_FILE) { fprintf(stderr, "Input file not found\n"); return 11; }
      if (trc == TANGLE_ERR_PARSE)   { fprintf(stderr, "Could not parse input file\n"); return 12; }
      if (trc != 0) { fprintf(stderr, "Meshing failed\n"); return 13; } }
    { int meshret = LoadMeshFromTangle(Doc, tmesh);
      if (meshret != TRUE) { if (meshret == -2) return 10; fprintf(stderr, "Problem loading mesh\n"); return 2; } }
    // Axisymmetric geometry must lie at r >= 0; a region (or arc belly) left of the
    // r = 0 axis otherwise "solves" silently with negative 2*pi*r element volumes.
    // Tolerance matches the solver's on-axis test (c[LengthUnits]*1e-6). Guards the
    // direct command-line path, which bypasses the GUI/headless front-end check.
    if (Doc.ProblemType == 1) {
        double c[] = {25.4, 1., 10., 1000., 0.0254, 0.001};
        double tol = c[Doc.LengthUnits] * 1.e-6;
        double minR = 0.0;
        for (int i = 0; i < Doc.NumNodes; i++)
            if (Doc.meshnode[i].x < minR) minR = Doc.meshnode[i].x;
        if (minR < -tol) {
            fprintf(stderr, "Axisymmetric geometry extends to r = %g; move it onto "
                    "or to the right of the r = 0 axis.\n", minR / c[Doc.LengthUnits]);
            return 9;
        }
    }
    char *p = PathName; for (int i=0;i<(int)strlen(PathName);i++) if(PathName[i]=='\\'||PathName[i]=='/') p=PathName+i+1;
    fprintf(stderr, "%s - csolve\n", p);
    // Cuthill-McKee renumbering is now done by tangle at mesh time.
    fprintf(stderr, "%i nodes, %i elements, Precision: %3.2e\n", Doc.NumNodes, Doc.NumEls, Doc.Precision);
    CBigComplexLinProb L; L.TheView = &theView; L.Precision = Doc.Precision;
    if (L.Create(Doc.NumNodes+Doc.NumCircProps, Doc.BandWidth, Doc.NumNodes) == FALSE) { fprintf(stderr, "Alloc failed\n"); return 4; }
    if (Doc.AnalyzeProblem(L) == FALSE) { fprintf(stderr, "Solve failed\n"); return 5; }
    theView.m_prog1.SetPos(100); theView.m_prog2.SetPos(100); theView.UpdateWindow();
    fprintf(stderr, "\nProblem solved\n");
    if (Doc.WriteResults(L) == FALSE) { fprintf(stderr, "Write failed\n"); return 6; }
    fprintf(stderr, "Results written to disk\n");
    Doc.CleanUp(); return 0;
}
