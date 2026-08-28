(* Layer 2, mathfemm: EOCondPropTest driven through the binding.

     math -script l2_condprop_mathfemm.m      (with FEMM_DIR set)

   A line-for-line translation of testing/EOCondPropTest.py, so the existing
   CondPropCompare.py checks it against the same Original FEMM golden.

   Mathematica writes exponents as *^, which the comparer's float() cannot
   parse, so numbers go through NumberForm with an explicit NumberFormat that
   emits a C-style e. ExponentFunction -> (# &) keeps the exponent rather than
   letting Mathematica suppress it, so 10000 comes out as 1.e4 -- still a
   perfectly good float literal. *)

here = DirectoryName[$InputFileName];
Get[FileNameJoin[{here, "..", "..", "mathfemm", "mathfemm.m"}]];
tests = ParentDirectory[here];
tmp = Environment["TEMP"];
If[tmp === $Failed, tmp = "C:/temp"];

fmt[z_] := ToString[NumberForm[N[Re[z]], 10,
              NumberFormat -> (Row[{#1, "e", #3}] &), ExponentFunction -> (# &)]] <>
           " " <>
           ToString[NumberForm[N[Im[z]], 10,
              NumberFormat -> (Row[{#1, "e", #3}] &), ExponentFunction -> (# &)]];

(* StringPadRight is v10.1+; on Mathematica 9 it stays unevaluated and
   silently poisons the StringJoin around it. *)
pad[s_, n_] := s <> StringJoin[Table[" ", {Max[0, n - StringLength[s]]}]];

slash[p_] := StringReplace[p, "\\" -> "/"];

runcase[fee_, cond_, tag_] := Module[{z},
   OpenDocument[slash[FileNameJoin[{tests, fee}]]];
   EISaveAs[slash[FileNameJoin[{tmp, "l2_mma_temp.fee"}]]];
   EIAnalyze[];
   EILoadSolution[];
   WriteString["stdout", "===  " <> tag <> "  ===\n"];
   z = EOGetConductorProperties[cond];
   WriteString["stdout", "  " <> pad[cond, 14] <> " = " <>
                         fmt[z[[1]]] <> " | " <> fmt[z[[2]]] <> "\n"];
   EOClose[];
];

OpenFEMM[1];
runcase["electrostatic1.fee", "+10000V", "electrostatic1 (axi)"];
runcase["electrostatic2.fee", "+10000V", "electrostatic2 (planar)"];
WriteString["stdout", "\nDone.\n"];
CloseFEMM[];
