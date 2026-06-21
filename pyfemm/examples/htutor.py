##########################################################
# 
# Heat Flow Scripting Example
# 
# David Meeker
# dmeeker@ieee.org
# March 1, 2005
# 
##########################################################
import femm
femm.openfemm()
femm.newdocument(2);

# define problem parameters
femm.hi_probdef('meters','planar',1e-8,20,30);

# add in materials and boundary conditions
femm.hi_addmaterial('Brick',0.7,0.7,0);
femm.hi_addboundprop('Outer Boundary',2,0,0,300,5,0);
femm.hi_addboundprop('Inner Boundary',2,0,0,800,10,0);

# draw the geometry
femm.hi_drawline(0,1,0,2)
femm.hi_drawline(0,2,2,2)
femm.hi_drawline(2,2,2,0)
femm.hi_drawline(2,0,1,0)
femm.hi_drawline(1,0,1,1)
femm.hi_drawline(1,1,0,1)
femm.hi_addblocklabel(1.5,1.5)

# apply the defined material to a block label
femm.hi_selectlabel(1.5,1.5);
femm.hi_setblockprop('Brick',0,0.05,0);
femm.hi_clearselected()
femm.hi_zoomnatural()

# apply the boundary conditions
femm.hi_selectsegment(1,0.5);
femm.hi_selectsegment(0.5,1);
femm.hi_setsegmentprop('Inner Boundary',0,1,0,0,'<None>');
femm.hi_clearselected()
femm.hi_selectsegment(2,0.5);
femm.hi_selectsegment(0.5,2);
femm.hi_setsegmentprop('Outer Boundary',0,1,0,0,'<None>');
femm.hi_clearselected()

# the file has to be saved before it can be analyzed.
femm.hi_saveas('auto-htutor.feh');

femm.hi_analyze()

# view the results
femm.hi_loadsolution()

# we desire to obtain the heat flux, just like in the
# tutorial example. first, define an integration contour
femm.ho_seteditmode('contour');
femm.ho_addcontour(0,1.5);
femm.ho_addcontour(1.5,1.5);
femm.ho_addcontour(1.5,0);
heatflux=femm.ho_lineintegral(1);
print('The total heat flux is %g Watts' % (4*heatflux[0]));

# if desired, the following line could be uncommented to
# shut down mirage:
femm.closefemm()

