-- EOPointValueTest.lua  (FEMM 4.2 electrostatics -- run on Original FEMM)
-- Companion to EOPointValueTest.py. eo_getpointvalues (V,Dx,Dy,Ex,Ey,epsx,epsy,
-- energy) at interior points on electrostatic1/2. "re im" pairs keyed by label
-- (compare with ScalarPointValueCompare.py). Run from the testing directory.
showconsole(); clearconsole()
function fmt(z) return re(z) .. " " .. im(z) end
function pt(label,x,y)
  V,Dx,Dy,Ex,Ey,ex,ey,nrg = eo_getpointvalues(x,y)
  print("  "..label.." = "..fmt(V).." | "..fmt(Dx).." | "..fmt(Dy).." | "..fmt(Ex)..
        " | "..fmt(Ey).." | "..fmt(ex).." | "..fmt(ey).." | "..fmt(nrg))
end
function run(fee,tag) open(fee); ei_saveas("temp.fee"); ei_analyze(1); ei_loadsolution(); print("===  "..tag.."  ===") end
run("electrostatic1.fee","electrostatic1 (axi)")
pt("near",40,20); pt("mid",60,92); pt("far",20,150); pt("shell",148.17,29.47); eo_close()
run("electrostatic2.fee","electrostatic2 (planar)")
pt("near",190,20); pt("mid",210,92); pt("far",170,150); pt("shell",298.17,29.47); eo_close()
print(""); print("Done.")
