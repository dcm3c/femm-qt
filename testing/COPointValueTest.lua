-- COPointValueTest.lua  (FEMM 4.2 current flow -- run on Original FEMM)
-- Companion to COPointValueTest.py. co_getpointvalues (V,Jx,Jy,Kx,Ky,Ex,Ey,epsx,
-- epsy,Jdx,Jdy,sigx,sigy,Jcx,Jcy; complex for AC/lossy). Run from testing.
showconsole(); clearconsole()
function fmt(z) return re(z) .. " " .. im(z) end
function pt(label,x,y)
  V,Jx,Jy,Kx,Ky,Ex,Ey,ex,ey,Jdx,Jdy,ox,oy,Jcx,Jcy = co_getpointvalues(x,y)
  print("  "..label.." = "..fmt(V).." | "..fmt(Jx).." | "..fmt(Jy).." | "..fmt(Kx)..
        " | "..fmt(Ky).." | "..fmt(Ex).." | "..fmt(Ey).." | "..fmt(ex).." | "..fmt(ey)..
        " | "..fmt(Jdx).." | "..fmt(Jdy).." | "..fmt(ox).." | "..fmt(oy).." | "..fmt(Jcx)..
        " | "..fmt(Jcy))
end
function run(fec,tag) open(fec); ci_saveas("temp.fec"); ci_analyze(1); ci_loadsolution(); print("===  "..tag.."  ===") end
run("currentFlow1.fec","cf1 (planar, DC)"); pt("srcSide",-2.5,-1.5); pt("gndSide",0.5,-1.5); ci_close()
run("currentFlow3.fec","cf3 (axi, AC 60Hz)"); pt("near",40,20); pt("mid",60,92); pt("shell",148.17,29.47); ci_close()
run("currentFlow5.fec","cf5 (axi, lossy 100kHz)"); pt("diel",1,0); pt("diel2",0.5,0); ci_close()
run("currentFlow6.fec","cf6 (planar, lossy 100kHz)"); pt("diel",-4,0); pt("diel2",-4.5,0); ci_close()
print(""); print("Done.")
