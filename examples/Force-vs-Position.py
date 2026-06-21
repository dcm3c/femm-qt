# Force-vs-Position.py - Magnetics example (port of Force-vs-Position.lua)
# Opens a .fem file, sweeps armature position, and plots force vs position.

showconsole()
clearconsole()
print("position in inches | force in lbf")
open("Roters-Ch9Fig6.fem")
mi_saveas("temp.fem")

z = []    # position, inches
fz = []   # force, lbf
dz = 0.05
zmax = 2
lbf = 4.4482216153
nz = round(zmax/dz)+1
for n in range(0, nz):
    mi_analyze()
    mi_loadsolution()
    mo_groupselectblock(1)
    f = mo_blockintegral(19) / lbf
    zn = dz * n
    print(zn, f)
    z.append(zn)
    fz.append(f)
    mo_close()
    mi_seteditmode("group")
    mi_selectgroup(1)
    mi_movetranslate(0, -dz)

plot("Force vs. Position",z, "position (in)", fz, "force (lbf)")
