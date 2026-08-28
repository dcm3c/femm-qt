(* Layer 2 phasecurrent, mathfemm: sends COMPLEX arguments.
   See l2_phase_reference.py for why this scenario exists.

   Num[n_] here is ToString[N[n], CForm], which renders a complex as
   Complex(1000.,0.) -- accepted because femm-qt binds a script-visible
   Complex(re, im). That is why mathfemm has always been able to send complex
   arguments where octavefemm could not. *)

here = DirectoryName[$InputFileName];
Get[FileNameJoin[{here, "..", "..", "mathfemm", "mathfemm.m"}]];
ex = FileNameJoin[{ParentDirectory[ParentDirectory[here]], "examples"}];
tmp = Environment["TEMP"];
If[tmp === $Failed, tmp = "C:/temp"];

slash[p_] := StringReplace[p, "\\" -> "/"];
pad[s_, n_] := s <> StringJoin[Table[" ", {Max[0, n - StringLength[s]]}]];
num[x_] := ToString[NumberForm[N[x], 10,
   NumberFormat -> (Row[{#1, "e", #3}] &), ExponentFunction -> (# &)]];
fmt[z_] := num[Re[z]] <> " " <> num[Im[z]];

names = {"A", "B", "C"};
curs = {1000 + 0 I, -500 - 866.0254038 I, -500 + 866.0254038 I};

OpenFEMM[1];
OpenDocument[slash[FileNameJoin[{ex, "three-phase-line.FEM"}]]];
MISaveAs[slash[FileNameJoin[{tmp, "l2_phase_mma.fem"}]]];
MIProbDef[60, "inches", "planar"];
Do[MIModifyCircProp[names[[k]], 1, curs[[k]]], {k, 3}];
MIAnalyze[];
MILoadSolution[];
WriteString["stdout", "===  three-phase-line balanced 1000A 60Hz  ===\n"];
Do[Module[{z = MOGetCircuitProperties[names[[k]]]},
   WriteString["stdout", "  " <> pad[names[[k]], 14] <> " = " <> fmt[z[[1]]] <>
                         " | " <> fmt[z[[2]]] <> " | " <> fmt[z[[3]]] <> "\n"]],
  {k, 3}];
MOClose[];
WriteString["stdout", "\nDone.\n"];
CloseFEMM[];
