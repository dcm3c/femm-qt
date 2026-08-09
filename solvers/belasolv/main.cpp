// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 David Meeker

// main.cpp — CLI entry point for belasolv electrostatics solver
// Reads .fee file, meshes with tangle, solves, writes results.

#include "StdAfx.h"
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

    // A block label with no material assigned ("<None>") flattens to file index 0,
    // i.e. BlockType -1 here; the mesher reads index 0 as a hole and voids the
    // region -- an empty-mesh crash if it is the only one, else a silently dropped
    // region. Always an error: a default block label only covers regions with no
    // label at all, not a label left without a material. libfemm rejects it too.
    for (int i = 0; i < Doc.NumBlockLabels; i++)
        if (Doc.labellist[i].BlockType < 0) {
            fprintf(stderr, "A block label has no material assigned.\n");
            return 10;
        }

    // Mesh and load directly into solver arrays
    fprintf(stderr, "Meshing...\n");
    Mesh tmesh;
    // Pass the full .fee name so the mesher loads the electrostatics geometry,
    // not a same-named .fem (PathName is the base, extension stripped).
    int trc = tangle_mesh_fem(std::string(PathName) + ".fee", tmesh);
    if (trc != 0) {
        // Distinct exit code per tangle failure category; tangle also prints the
        // specific reason to stderr (captured in the GUI solver log).
        if (trc == TANGLE_ERR_NO_FILE) { fprintf(stderr, "Input file not found\n"); return 11; }
        if (trc == TANGLE_ERR_PARSE)   { fprintf(stderr, "Could not parse input file\n"); return 12; }
        fprintf(stderr, "Meshing failed\n"); return 13;   // TANGLE_ERR_MESH
    }
    int meshret = LoadMeshFromTangle(Doc, tmesh);
    if (meshret != TRUE) {
        if (meshret == -2) return 10;   // no material assigned (reason already printed)
        fprintf(stderr, "Problem loading mesh\n");
        return 2;
    }

    // Axisymmetric geometry must lie at r >= 0. A region drawn left of the r = 0
    // axis otherwise meshes and "solves" silently with negative element volumes
    // (each carries a 2*pi*r weight). Checking the meshed nodes also catches an
    // arc whose belly dips below the axis even though its endpoints do not, since
    // meshing lands nodes on the arc. Tolerance matches the solver's on-axis test
    // (c[LengthUnits]*1e-6). The GUI/headless front ends guard this too, but a
    // solver invoked directly from the command line would not otherwise.
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
