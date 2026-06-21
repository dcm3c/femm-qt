# ACElec2
# Modeled after Quickfield's ACElec2 example

import femm

femm.openfemm();
femm.newdocument(3);

# define some parameters.  These can then
# be used to draw the geometry parametrically
ri=1;
ro=1.5;
z=1.5;

# draw geometry of interest
femm.ci_drawline(ri,-z,ri,z);
femm.ci_drawline(ri,z,ro,z);
femm.ci_drawline(ro,z,ro,-z);
femm.ci_drawline(ri,-z,ro,-z);
femm.ci_addnode(ri,z-0.1);
femm.ci_addnode(ro,-z+0.15);
femm.ci_addnode(ro,-z+0.25);

# draw boundary
femm.ci_drawarc(0,-4,0,4,180,2);
femm.ci_drawline(0,-4,0,4);

# add material definitions
femm.ci_addmaterial('Air',0,0,1,1,0,0);
femm.ci_addmaterial('Ceramic',1e-8,1e-8,6,6,0,0);

# add some block labels
femm.ci_addblocklabel((ri+ro)/2,0);
femm.ci_selectlabel((ri+ro)/2,0);
femm.ci_setblockprop('Ceramic',0,0.05,0)
femm.ci_clearselected();

femm.ci_addblocklabel(ri/2,0);
femm.ci_selectlabel(ri/2,0);
femm.ci_setblockprop('Air',0,0.05,0)
femm.ci_clearselected();

# Add some boundary properties
femm.ci_addboundprop('U+', 5,0,0,0,0)
femm.ci_addboundprop('U-',-5,0,0,0,0)

femm.ci_selectsegment(ro,-z+0.1);
femm.ci_selectsegment((ro+ri)/2,-z);
femm.ci_selectsegment(ri,0);
femm.ci_setsegmentprop('U+',0,1,0,0,'<None>');
femm.ci_clearselected();

femm.ci_selectsegment((ro+ri)/2,z);
femm.ci_selectsegment(ro,0);
femm.ci_setsegmentprop('U-',0,1,0,0,'<None>');
femm.ci_clearselected();
femm.ci_zoomnatural();

# Save, analyze, and view results
femm.ci_saveas('ACElec2.fec');
femm.ci_analyze()
femm.ci_loadsolution()

input('Press <ENTER> to continue')
