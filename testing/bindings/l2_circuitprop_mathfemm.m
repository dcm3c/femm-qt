(* Layer 2, mathfemm: MOCircuitPropTest driven through the binding.
   A line-for-line translation of testing/MOCircuitPropTest.py, checked by
   MOCircuitPropCompare.py against the same Original FEMM golden.

   Mathematica 9 constraints, all of which bit during this work: exponents are
   written *^ and must be forced to a C-style e for the comparer's float();
   StringPadRight is v10.1+ and is hand-rolled. *)

here = DirectoryName[$InputFileName];
Get[FileNameJoin[{here, "..", "..", "mathfemm", "mathfemm.m"}]];
tests = ParentDirectory[here];
(* $TemporaryDirectory is the platform's own temp directory: honours TMPDIR on POSIX and TEMP on Windows, so no per-platform branch. *)
tmp = $TemporaryDirectory;

slash[p_] := StringReplace[p, "\\" -> "/"];
T[name_] := slash[FileNameJoin[{tests, name}]];
W[name_] := slash[FileNameJoin[{tmp, name}]];
pad[s_, n_] := s <> StringJoin[Table[" ", {Max[0, n - StringLength[s]]}]];
num[x_] := ToString[NumberForm[N[x], 10,
   NumberFormat -> (Row[{#1, "e", #3}] &), ExponentFunction -> (# &)]];
fmt[z_] := num[Re[z]] <> " " <> num[Im[z]];

report[circuits_, tag_] := Module[{z},
   MIAnalyze[]; MILoadSolution[];
   WriteString["stdout", "===  " <> tag <> "  ===\n"];
   Do[z = MOGetCircuitProperties[c];
      WriteString["stdout", "  " <> pad[c, 14] <> " = " <> fmt[z[[1]]] <>
                            " | " <> fmt[z[[2]]] <> " | " <> fmt[z[[3]]] <> "\n"],
      {c, circuits}];
   MOClose[];
];

setrmb[cur_] := Do[MIModifyCircProp["i" <> ToString[j], 1, cur[[j]]], {j, 4}];
settorq[a_, b_] := (MIModifyCircProp["i1", 1, a]; MIModifyCircProp["i2", 1, b]);

RMBC = {"i1", "i2", "i3", "i4"};
TQC = {"i1", "i2"};
ROTC = {"Coil Current"};

OpenFEMM[1];

(* ---- RadialMagneticBearing ---- *)
Do[Module[{cur = c[[1]], f = c[[2]], tag = c[[3]]},
   OpenDocument[T["RadialMagneticBearing.fem"]]; MISaveAs[W["temp.fem"]];
   setrmb[cur]; MIProbDef[f, "inches", "planar"];
   report[RMBC, tag]],
  {c, {{{12, 6, 0, 6}, 0, "RMB (x, DC) i=(12,6,0,6)"},
       {{12, 6, 0, 6}, 60, "RMB (x, AC 60Hz) i=(12,6,0,6)"},
       {{6, 12, 6, 0}, 0, "RMB (y, DC) i=(6,12,6,0)"},
       {{6, 12, 6, 0}, 60, "RMB (y, AC 60Hz) i=(6,12,6,0)"}}}];
OpenDocument[T["RadialMagneticBearing.fem"]]; MISaveAs[W["bias.fem"]];
setrmb[{6, 6, 6, 6}]; MIProbDef[0, "inches", "planar"]; MIAnalyze[];
MISaveAs[W["incr.fem"]]; MISetPrevious[W["bias.ans"], 1];
setrmb[{1, 0, -1, 0}]; MIProbDef[60, "inches", "planar"];
report[RMBC, "RMB (x-incremental) bias 6A; AC i=(1,0,-1,0)"];

(* ---- roters1b ---- *)
OpenDocument[T["roters1b.fem"]]; MISaveAs[W["rtemp.fem"]];
MIModifyCircProp["Coil Current", 1, 0.8]; MIProbDef[0, "inches", "axi"];
report[ROTC, "roters1b (DC) 0.8A"];
OpenDocument[T["roters1b.fem"]]; MISaveAs[W["rtemp.fem"]];
MIModifyCircProp["Coil Current", 1, 0.8]; MIProbDef[60, "inches", "axi"];
report[ROTC, "roters1b (AC 60Hz) 0.8A"];
OpenDocument[T["roters1b.fem"]]; MISaveAs[W["rbias.fem"]];
MIModifyCircProp["Coil Current", 1, 0.8]; MIProbDef[0, "inches", "axi"]; MIAnalyze[];
MISaveAs[W["rincr.fem"]]; MISetPrevious[W["rbias.ans"], 1];
MIModifyCircProp["Coil Current", 1, 0.1]; MIProbDef[60, "inches", "axi"];
report[ROTC, "roters1b (incremental) bias 0.8A; AC 0.1A"];

(* ---- torque ---- *)
OpenDocument[T["torque.fem"]]; MISaveAs[W["temp.fem"]];
settorq[2, 0]; MIProbDef[0, "inches", "planar"];
report[TQC, "torque (DC) i=(2,0)"];
OpenDocument[T["torque.fem"]]; MISaveAs[W["temp.fem"]];
settorq[2, 0]; MIProbDef[60, "inches", "planar"];
report[TQC, "torque (AC 60Hz) i=(2,0)"];
OpenDocument[T["torque.fem"]]; MISaveAs[W["bias.fem"]];
settorq[1, 1]; MIProbDef[0, "inches", "planar"]; MIAnalyze[];
MISaveAs[W["incr.fem"]]; MISetPrevious[W["bias.ans"], 1];
settorq[1, -1]; MIProbDef[60, "inches", "planar"];
report[TQC, "torque (incremental) bias i=(1,1); AC i=(1,-1)"];

(* ---- Lorentz coils ---- *)
Do[Module[{fem = L[[1]], ptype = L[[2]], tag = L[[3]]},
   OpenDocument[T[fem]]; MISaveAs[W["temp.fem"]];
   MIModifyCircProp["i1", 1, 16]; MIProbDef[0, "inches", ptype];
   report[{"i1"}, tag <> " (DC) 16A"];
   OpenDocument[T[fem]]; MISaveAs[W["temp.fem"]];
   MIModifyCircProp["i1", 1, 16]; MIProbDef[60, "inches", ptype];
   report[{"i1"}, tag <> " (AC 60Hz) 16A"]],
  {L, {{"AxisymmetricLorentzForce.fem", "axi", "AxiLorentz"},
       {"PlanarLorentzForce.fem", "planar", "PlanarLorentz-y"},
       {"PlanarLorentzForceAlt.fem", "planar", "PlanarLorentz-x"}}}];

WriteString["stdout", "\nDone.\n"];
CloseFEMM[];
