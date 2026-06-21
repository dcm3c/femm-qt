import femm

# Electrostatics Example
# David Meeker
# dmeeker@ieee.org
#  
# The objective of this program is to find the capacitance
# matrix associated with a set of four microstrip lines on
# top of a dielectric substrate. We will consider lines
# that are 20 um wide and 2 um thick, separated by a 4 um
# distance. The traces are laying centered atop of a 20 um
# by 200 um slab with a relative permeability of 4. The
# slab rests on an infinite ground plane. We will consider
# a 1m depth in the into-the-page direction.
#  
# This program sets up the problem and solves two different
# cases with different voltages applied to the conductors
# Becuase of symmetry, this yields enough information to
# deduce the 4x4 capacitance matrix

# Start up and connect to FEMM
femm.openfemm()

# Create a new electrostatics problem
femm.newdocument(1)

# Draw the geometry
femm.ei_probdef('micrometers','planar',10**(-8),10**6,30);
femm.ei_drawrectangle(2,0,22,2);
femm.ei_drawrectangle(2+24,0,22+24,2);
femm.ei_drawrectangle(-2,0,-22,2);
femm.ei_drawrectangle(-2-24,0,-22-24,2);
femm.ei_drawrectangle(-100,-20,100,0);
femm.ei_drawline(-120,-20,120,-20);
femm.ei_drawarc(120,-20,-120,-20,180,2.5);
femm.ei_drawarc(100,100,120,100,180,2.5);
femm.ei_drawline(100,100,120,100);

# Create and assign a "periodic" boundary condition to 
# model an unbounded problem via the Kelvin Transformation
femm.ei_addboundprop('periodic',0,0,0,0,3);
femm.ei_selectarcsegment(0,100);
femm.ei_selectarcsegment(110,80);
femm.ei_setarcsegmentprop(2.5,'periodic',0,0,'<none>');
femm.ei_clearselected();

# Define the ground plane in both the geometry and the exterior region
femm.ei_addboundprop('ground',0,0,0,0,0);
femm.ei_selectsegment(0,-20);
femm.ei_selectsegment(110,-20);
femm.ei_selectsegment(-110,-20);
femm.ei_selectsegment(110,100);
femm.ei_setsegmentprop('ground',0,1,0,0,'<none>');
femm.ei_clearselected();

# Add block labels for each strip and mark them with "No Mesh"
for k in range(0,4):
	femm.ei_addblocklabel(-36+k*24,1)
for k in range(0,4):
	femm.ei_selectlabel(-36+k*24,1)
femm.ei_setblockprop('<No Mesh>',0,1,0);
femm.ei_clearselected();

# Add and assign the block labels for the air and dielectric regions
femm.ei_addmaterial('air',1,1,0);
femm.ei_addmaterial('dielectric',4,4,0);
femm.ei_addblocklabel(0,-10);
femm.ei_addblocklabel(0,50);
femm.ei_addblocklabel(110,95);
femm.ei_selectlabel(0,-10);
femm.ei_setblockprop('dielectric',0,1,0);
femm.ei_clearselected();
femm.ei_selectlabel(0,50);
femm.ei_selectlabel(110,95);
femm.ei_setblockprop('air',0,1,0);
femm.ei_clearselected();

# Add a "Conductor Property" for each of the strips
femm.ei_addconductorprop('v0',0,0,1);
femm.ei_addconductorprop('v1',0,0,1);
femm.ei_addconductorprop('v2',0,0,1);
femm.ei_addconductorprop('v3',0,0,1);

# Assign the "v0" properties to all sides of the first strip
femm.ei_selectsegment(-46,1);
femm.ei_selectsegment(-26,1);
femm.ei_selectsegment(-36,2);
femm.ei_selectsegment(-36,0);
femm.ei_setsegmentprop('<None>',0.25,0,0,0,'v0');
femm.ei_clearselected()

# Assign the "v1" properties to all sides of the second strip
femm.ei_selectsegment(-46+24,1);
femm.ei_selectsegment(-26+24,1);
femm.ei_selectsegment(-36+24,2);
femm.ei_selectsegment(-36+24,0);
femm.ei_setsegmentprop('<None>',0.25,0,0,0,'v1');
femm.ei_clearselected()

# Assign the "v2" properties to all sides of the third strip
femm.ei_selectsegment(-46+2*24,1);
femm.ei_selectsegment(-26+2*24,1);
femm.ei_selectsegment(-36+2*24,2);
femm.ei_selectsegment(-36+2*24,0);
femm.ei_setsegmentprop('<None>',0.25,0,0,0,'v2');
femm.ei_clearselected()

# Assign the "v3" properties to all sides of the fourth strip
femm.ei_selectsegment(-46+3*24,1);
femm.ei_selectsegment(-26+3*24,1);
femm.ei_selectsegment(-36+3*24,2);
femm.ei_selectsegment(-36+3*24,0);
femm.ei_setsegmentprop('<None>',0.25,0,0,0,'v3');
femm.ei_clearselected()

femm.ei_zoomnatural();

# Save the geometry to disk so we can analyze it
femm.ei_saveas('strips.fee');

# Create a placeholder matrix which we will fill with capacitance values
c=[]
 
for k in range(0,4):
	femm.ei_modifyconductorprop('v0',1,1 if (k==0) else 0);
	femm.ei_modifyconductorprop('v1',1,1 if (k==1) else 0);
	femm.ei_modifyconductorprop('v2',1,1 if (k==2) else 0);
	femm.ei_modifyconductorprop('v3',1,1 if (k==3) else 0);
	femm.ei_analyze();
	femm.ei_loadsolution()
	c.append([femm.eo_getconductorproperties('v0')[1],femm.eo_getconductorproperties('v1')[1],femm.eo_getconductorproperties('v2')[1], femm.eo_getconductorproperties('v3')[1]])

femm.closefemm()

print('The capacitance matrix (pF/m):')
for j in range(0,4):
	for k in range(0,4): 
		c[j][k]*=10**12
	print(c[j])
	



