-- MOBHCurveTest.lua  (FEMM 4.2 magnetics -- run on Original FEMM)
--
-- Mesh-independent B-H curve check. block.fem is a 1" cube of M-19 Steel with a
-- prescribed-A gradient (Alo=0, Ahi=0.0254*Bdes), so B == Bdes uniformly and
-- mo_getpointvalues(0,0) isolates the B-H curve / permeability evaluation with
-- zero mesh sensitivity -- femm-qt and OF should be identical for every
-- (frequency, Bdes). The material is laminated so B stays uniform even at
-- frequency (no bulk eddy / skin effect); the AC rows exercise the complex GetMu
-- (effective permeability + hysteresis loss). Each row prints the 14 point values
-- as "re im" pairs (compare with MOBHCurveCompare.py). Run from the testing dir.
-- setcurrentdirectory("C:/Claude/uPython/femm-qt/testing")

showconsole(); clearconsole()
function fmt(z) return re(z) .. " " .. im(z) end

open("block.fem"); mi_saveas("temp.fem")
print("=== block.fem uniform-B sweep @ (0,0):  A|B1|B2|sig|E|H1|H2|Je|Js|mu1|mu2|Pe|Ph|ff ===")
Fset = {0, 60, 400}
Bset = {0.1, 0.5, 1.0, 1.3, 1.5, 1.7, 1.9, 2.1, 2.3}
fi = 1
while Fset[fi] ~= nil do
  mi_probdef(Fset[fi], "inches", "planar")
  i = 1
  while Bset[i] ~= nil do
    mi_modifyboundprop("Ahi", 1, 0.0254 * Bset[i])
    mi_analyze(1); mi_loadsolution()
    A, B1, B2, sig, E, H1, H2, Je, Js, mu1, mu2, Pe, Ph, ff = mo_getpointvalues(0, 0)
    print("  f=" .. Fset[fi] .. " Bdes=" .. format("%.3f", Bset[i]) .. " = " ..
          fmt(A) .. " | " .. fmt(B1) .. " | " .. fmt(B2) .. " | " .. fmt(sig) .. " | " ..
          fmt(E) .. " | " .. fmt(H1) .. " | " .. fmt(H2) .. " | " .. fmt(Je) .. " | " ..
          fmt(Js) .. " | " .. fmt(mu1) .. " | " .. fmt(mu2) .. " | " .. fmt(Pe) .. " | " ..
          fmt(Ph) .. " | " .. fmt(ff))
    mo_close()
    i = i + 1
  end
  fi = fi + 1
end
print("Done.")
