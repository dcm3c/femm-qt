// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 David Meeker

// main.cpp — CLI entry point for belasolv electrostatics solver
// Reads .fee file, meshes with tangle, solves, writes results.

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

int main(int argc, char* argv[])
{
    std::vector<std::string> args = femm_utf8_args(argc, argv);
#ifdef _OPENMP
    int max_t = omp_get_max_threads();
    if (max_t > 4) omp_set_num_threads(4);
    fprintf(stderr, "OpenMP: %d threads\n", omp_get_max_threads());
#endif

    CFknDlg theView;
    CFemmeDocCore Doc;
    char PathName[4096];

    if ((int)args.size() < 2) {
        fprintf(stderr, "Usage: belasolv <filename>\n");
        fprintf(stderr, "  filename: path to .fee file (without extension)\n");
        return 1;
    }
    snprintf(PathName, sizeof(PathName), "%s", args[1].c_str());

    Doc.PathName = PathName;
    Doc.TheView = &theView;

    if (Doc.OnOpenDocument() != TRUE) {
        fprintf(stderr, "Problem loading .fee file\n");
        return 7;
    }

    // Mesh and load directly into solver arrays
    fprintf(stderr, "Meshing...\n");
    Mesh tmesh;
    // Pass the full .fee name so the mesher loads the electrostatics geometry,
    // not a same-named .fem (PathName is the base, extension stripped).
    if (tangle_mesh_fem(std::string(PathName) + ".fee", tmesh) != 0) {
        fprintf(stderr, "Meshing failed\n");
        return 8;
    }
    if (LoadMeshFromTangle(Doc, tmesh) != TRUE) {
        fprintf(stderr, "Problem loading mesh\n");
        return 2;
    }

    // Print problem name
    char *ProbName = PathName;
    for (int i = 0; i < (int)strlen(PathName); i++)
        if (PathName[i] == '\\' || PathName[i] == '/')
            ProbName = PathName + i + 1;
    fprintf(stderr, "%s - belasolve\n", ProbName);

    // Cuthill-McKee renumbering is now done by tangle at mesh time.

    fprintf(stderr, "Problem Statistics:\n%i nodes\n%i elements\nPrecision: %3.2e\n",
            Doc.NumNodes, Doc.NumEls, Doc.Precision);

    CBigLinProb L;
    L.TheView = &theView;
    L.Precision = Doc.Precision;

    if (L.Create(Doc.NumNodes + Doc.NumCircProps, Doc.BandWidth) == FALSE) {
        fprintf(stderr, "Couldn't allocate enough space for matrices\n");
        return 4;
    }

    if (Doc.AnalyzeProblem(L) == FALSE) {
        fprintf(stderr, "Couldn't solve the problem\n");
        return 5;
    }
    theView.m_prog1.SetPos(100); theView.m_prog2.SetPos(100); theView.UpdateWindow();
    fprintf(stderr, "\nProblem solved\n");

    if (Doc.WriteResults(L) == FALSE) {
        fprintf(stderr, "Couldn't write results to disk\n");
        return 6;
    }
    fprintf(stderr, "Results written to disk\n");

    Doc.CleanUp();
    return 0;
}
