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
#include "hsolvdoc.h"
#include "tangle_mesh.h"
extern int LoadMeshFromTangle(Chsolvdoc& Doc, const Mesh& tmesh);
int main(int argc, char* argv[]) {
#ifdef _OPENMP
    int max_t = omp_get_max_threads();
    if (max_t > 4) omp_set_num_threads(4);
    fprintf(stderr, "OpenMP: %d threads\n", omp_get_max_threads());
#endif
    CFknDlg theView; Chsolvdoc Doc; char PathName[4096];
    if (argc < 2) { fprintf(stderr, "Usage: hsolv <filename>\n"); return 1; }
    snprintf(PathName, sizeof(PathName), "%s", argv[1]);
    Doc.PathName = PathName; Doc.TheView = &theView;
    fprintf(stderr, "step1\n"); fflush(stderr);
    if (Doc.OnOpenDocument() != TRUE) { fprintf(stderr, "FAIL open\n"); return 7; }
    fprintf(stderr, "step2\n"); fflush(stderr);
    Mesh tmesh;
    // Full .feh name -> mesh the heat-flow geometry, not a same-named .fem.
    if (tangle_mesh_fem(std::string(PathName) + ".feh", tmesh) != 0) { fprintf(stderr, "FAIL mesh\n"); return 8; }
    fprintf(stderr, "step3\n"); fflush(stderr);
    if (LoadMeshFromTangle(Doc, tmesh) != TRUE) { fprintf(stderr, "FAIL load\n"); return 2; }
    fprintf(stderr, "step4\n"); fflush(stderr);
    if (Doc.LoadPrev() != TRUE) { return 3; }
    fprintf(stderr, "step5\n"); fflush(stderr);
    // Cuthill-McKee renumbering is now done by tangle at mesh time.
    fprintf(stderr, "step6\n"); fflush(stderr);
    CBigLinProb L; L.TheView = &theView; L.Precision = Doc.Precision;
    if (L.Create(Doc.NumNodes+Doc.NumCircProps, Doc.BandWidth) == FALSE) { return 5; }
    fprintf(stderr, "step7\n"); fflush(stderr);
    if (Doc.AnalyzeProblem(L) == FALSE) { fprintf(stderr, "FAIL solve\n"); return 6; }
    theView.m_prog1.SetPos(100); theView.m_prog2.SetPos(100); theView.UpdateWindow();
    fprintf(stderr, "\nProblem solved\n");
    if (Doc.WriteResults(L) == FALSE) { fprintf(stderr, "FAIL write\n"); return 7; }
    fprintf(stderr, "Results written\n");
    Doc.CleanUp(); return 0;
}
