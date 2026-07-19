-- HOPointValueTest.lua  (FEMM 4.2 heat flow -- run on Original FEMM)
-- Companion to HOPointValueTest.py. ho_getpointvalues (T,Fx,Fy,Gx,Gy,kx,ky) at
-- interior points on heatFlow1/2. "re im" pairs keyed by label. Run from testing.
showconsole(); clearconsole()
function fmt(z) return re(z) .. " " .. im(z) end
function pt(label,x,y)
  T,Fx,Fy,Gx,Gy,kx,ky = ho_getpointvalues(x,y)
  print("  "..label.." = "..fmt(T).." | "..fmt(Fx).." | "..fmt(Fy).." | "..fmt(Gx)..
        " | "..fmt(Gy).." | "..fmt(kx).." | "..fmt(ky))
end
function run(feh,tag) open(feh); hi_saveas("temp.feh"); hi_analyze(1); hi_loadsolution(); print("===  "..tag.."  ===") end
run("heatFlow1.feh","heatFlow1 (axi)")
pt("near",40,20); pt("mid",60,92); pt("far",100,40); ho_close()
run("heatFlow2.feh","heatFlow2 (planar)")
pt("near",40,20); pt("mid",60,92); pt("far",100,40); ho_close()
print(""); print("Done.")
