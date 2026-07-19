-- COCondPropTest.lua  (FEMM 4.2 current flow -- run on Original FEMM)
-- Companion to COCondPropTest.py. co_getconductorproperties (voltage, current)
-- on currentFlow1-6. Each conductor prints "V | I" as two "re im" pairs, keyed by
-- name (compare with CondPropCompare.py). Run from the testing directory.
-- setcurrentdirectory("C:/Claude/uPython/femm-qt/testing")

showconsole(); clearconsole()
function fmt(z) return re(z) .. " " .. im(z) end
function cond(name)
  V, I = co_getconductorproperties(name)
  print("  " .. name .. " = " .. fmt(V) .. " | " .. fmt(I))
end
function co_run(fec, tag)
  open(fec); ci_saveas("temp.fec"); ci_analyze(1); ci_loadsolution()
  print("===  " .. tag .. "  ===")
end

co_run("currentFlow1.fec", "cf1 (planar, DC)");        cond("Gnd"); cond("InputCurrent"); ci_close()
co_run("currentFlow2.fec", "cf2 (axi, DC)");           cond("Gnd"); cond("Isrc"); ci_close()
co_run("currentFlow3.fec", "cf3 (axi, AC 60Hz)");      cond("+10000V"); ci_close()
co_run("currentFlow4.fec", "cf4 (planar, AC 60Hz)");   cond("+10000V"); ci_close()
co_run("currentFlow5.fec", "cf5 (axi, lossy 100kHz)"); cond("Vhi"); cond("Vlo"); ci_close()
co_run("currentFlow6.fec", "cf6 (planar, lossy 100kHz)"); cond("Vhi"); cond("Vlo"); ci_close()
print(""); print("Done.")
