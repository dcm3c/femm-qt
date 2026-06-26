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
extern int LoadMeshFromTangle(CFemmeDocCore& Doc, const Mesh& tmesh);
int main(int argc, char* argv[]) {
#ifdef _OPENMP
    int max_t = omp_get_max_threads();
    if (max_t > 4) omp_set_num_threads(4);
    fprintf(stderr, "OpenMP: %d threads\n", omp_get_max_threads());
#endif
    CFknDlg theView; CFemmeDocCore Doc; char PathName[4096];
    if (argc < 2) { fprintf(stderr, "Usage: csolv <filename>\n"); return 1; }
    snprintf(PathName, sizeof(PathName), "%s", argv[1]);
    Doc.PathName = PathName; Doc.TheView = &theView;
    if (Doc.OnOpenDocument() != TRUE) { fprintf(stderr, "Problem loading .fec file\n"); return 7; }
    fprintf(stderr, "Meshing...\n");
    Mesh tmesh;
    // Full .fec name -> mesh the current-flow geometry, not a same-named .fem.
    if (tangle_mesh_fem(std::string(PathName) + ".fec", tmesh) != 0) { fprintf(stderr, "Meshing failed\n"); return 8; }
    if (LoadMeshFromTangle(Doc, tmesh) != TRUE) { fprintf(stderr, "Problem loading mesh\n"); return 2; }
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
