(* emit_mathfemm.m -- record what every mathfemm wrapper emits, driven by
   contract.tsv. No engine is started.

       math -script emit_mathfemm.m

   Two things make mathfemm the awkward one.

   1. Its symbols are sealed. mathfemm.m ends with ~640 lines of
      SetAttributes[X, {Locked, Protected, ReadProtected}], inherited from
      Original FEMM. Locked cannot be removed, so the transport cannot be
      replaced the way it is in the other four bindings -- and the real
      transport Abort[]s when no connection is open, which kills the whole run.
      So the harness loads a COPY with only those attribute statements removed.
      The wrapper definitions are untouched; the count of removed lines is
      printed and the transformation is asserted to have changed nothing else,
      so "we tested a modified file" stays a checkable claim rather than a hope.

   2. Its function names are CamelCase -- MIAddNode, not mi_addnode. Contract
      names are matched by lowercasing and dropping underscores, which is
      exactly the transformation between the two spellings.

   Writes emitted_mathfemm.tsv:  wrapper <TAB> pass <TAB> emission *)

here = DirectoryName[$InputFileName];
pkg = FileNameJoin[{here, "..", "..", "mathfemm", "mathfemm.m"}];
tmp = FileNameJoin[{here, "mathfemm_unlocked.m"}];

(* ReadString is v10+; this harness has to run on Mathematica 9, where it
   silently stays unevaluated and every downstream string op fails. *)
txt = StringJoin[Riffle[ReadList[pkg, String], FromCharacterCode[10]]];
sealPat = RegularExpression[
   "SetAttributes\\[[A-Za-z0-9]+,\\{Locked,Protected,ReadProtected\\}\\];"];
nseal = Length[StringCases[txt, sealPat]];
txt2 = StringReplace[txt, sealPat -> ""];
(* The only difference must be the seal statements themselves. *)
If[StringLength[txt] - StringLength[txt2] =!=
   Total[StringLength /@ StringCases[txt, sealPat]],
  Print["unexpected edit to the package copy -- aborting"]; Quit[2]];
Print["unsealed ", nseal, " SetAttributes statements (definitions untouched)"];
Export[tmp, txt2, "Text"];
Get[tmp];

emit = {};
(* MLPut is public (it has a ::usage) but MLPutWait is defined inside
   `Private` and has none, so Names["*MLPut*"] -- which only sees what is
   on $ContextPath -- misses it. Wrappers that call MLPutWait directly then
   reach the REAL transport, which prints "not connected!" and returns
   without emitting: 78 wrappers silently unmeasured. Search both. *)
transports = Union[Join[Names["*MLPut*"],
                        Names["MathFEMM`Private`*MLPut*"]]];
Print["transports overridden: ", transports];
If[transports === {}, Print["no MLPut transport found -- aborting"]; Quit[2]];
(* The stub must record what the transport SENDS, not what it is handed.
   MLPutWait strips an mlput(...) wrapper before writing to the socket -- OF's
   Lua had a real mlput and femm-qt's port neutralises it here instead of
   editing 70 wrappers -- so a stub hooked at MLPut captures a string that never
   travels. Recording it raw made ~100 mathfemm functions look broken; they are
   not. Hook at the last point before the bytes leave, or replicate whatever is
   downstream of the hook, as this does. *)
stripMLPut[s_] := If[! StringQ[s],
   (* At least one wrapper hands MLPut something that is not a string. That is
      a finding, not a harness fault, so record it rather than throwing. *)
   "NONSTRING",
   If[StringMatchQ[s, "mlput(*)"], StringTake[s, {7, StringLength[s] - 1}], s]];
Do[With[{sym = Symbol[t]},
    Unprotect[sym];
    ClearAll[sym];
    sym[s_] := (AppendTo[emit, stripMLPut[s]]; {})],
   {t, transports}];

(* contract name -> actual package symbol, matched case- and underscore-blind.
   Downvalues rather than an Association: Association and KeyExistsQ are v10,
   and on v9 they stay UNEVALUATED, so the If below picks neither branch and
   the whole run silently records nothing. Same trap as ReadString above. *)
Do[lookup[ToLowerCase[StringReplace[s, "_" -> ""]]] = s, {s, Names["MathFEMM`*"]}];

arg[i_, kind_, which_] := Which[
   kind == "s", "s" <> ToString[i],
   which == "int", i,
   which == "real", -1.5 i + 0.25,
   True, i - 0.25 i I];

lines = Select[ReadList[FileNameJoin[{here, "contract.tsv"}], String],
   # =!= "" && StringTake[#, 1] =!= "#" &];

out = OpenWrite[FileNameJoin[{here, "emitted_mathfemm.tsv"}]];
nok = 0; nmiss = 0; nerr = 0;
Do[
  Module[{parts, name, kinds, key, symname, f, gotany = False, args},
   parts = StringSplit[line, "\t"];
   name = parts[[1]];
   kinds = If[Length[parts] >= 3, parts[[3]], ""];
   key = ToLowerCase[StringReplace[name, "_" -> ""]];
   If[! StringQ[lookup[key]],
    WriteString[out, name <> "\tint\tMISSING\n"]; nmiss = nmiss + 1,
    symname = lookup[key];
    f = Symbol[symname];
    Do[
     emit = {};
     args = Table[arg[i, StringTake[kinds, {i}], which],
                  {i, StringLength[kinds]}];
     Quiet@Check[CheckAbort[Apply[f, args], $Aborted], $Failed];
     If[emit === {},
      WriteString[out, name <> "\t" <> which <>
                       "\tERROR:no emission (symbol " <> symname <> ")\n"];
      nerr = nerr + 1,
      Do[WriteString[out, name <> "\t" <> which <> "\t" <>
                     StringReplace[e, "\t" -> " "] <> "\n"], {e, emit}];
      gotany = True],
     {which, {"int", "real", "cplx"}}];
    If[gotany, nok = nok + 1]]],
  {line, lines}];
Close[out];
DeleteFile[tmp];
Print["mathfemm: ", nok, " wrappers recorded, ", nmiss, " missing, ",
      nerr, " refused the contract"];
