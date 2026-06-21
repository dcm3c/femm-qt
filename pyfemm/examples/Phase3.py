# Phase3.py

import femm
import math
import cmath

femm.openfemm();
femm.newdocument(0);

# define some parameters.  These can then
# be used to draw the geometry parametrically
r=1/2;
R=2;
h=1/0.0254;

# define some materials
femm.mi_addmaterial("Air");
femm.mi_addmaterial("Cu",1,1,0,0,58);

# draw geometry of interest
for k in range(3):
	tta = k * 2 * math.pi / 3
	femm.mi_drawarc((R+r)*math.cos(tta),(R+r)*math.sin(tta),(R-r)*math.cos(tta),(R-r)*math.sin(tta),180,1);
	femm.mi_drawarc((R-r)*math.cos(tta),(R-r)*math.sin(tta),(R+r)*math.cos(tta),(R+r)*math.sin(tta),180,1);
	femm.mi_addcircprop('i'+str(k),1000*cmath.exp(2j*math.pi*k/3),1);
	femm.mi_addblocklabel(R*math.cos(tta),R*math.sin(tta));
	femm.mi_selectlabel(R*math.cos(tta),R*math.sin(tta));
	femm.mi_setblockprop("Cu",1,0,'i'+str(k),0,0,1);
	femm.mi_clearselected()

femm.mi_addblocklabel(0,0);
femm.mi_selectlabel(0,0);
femm.mi_setblockprop("Air",1,0,'<None>',0,0,1);
femm.mi_clearselected()
	
# draw boundary
femm.mi_makeABC(3,2*R,0,0,0);

# Save, analyze, and view results
femm.mi_probdef(60,"inches","planar",1e-8,h,30);
femm.mi_saveas('Phase3.fem');
femm.mi_analyze()
femm.mi_loadsolution()

z = femm.mo_getcircuitproperties('i0');
z = z[1]/z[0]
print('The per-meter 60Hz impedance of each line is {:.4g} Ohms'.format(z))

input('Press <ENTER> to continue')
femm.closefemm()
