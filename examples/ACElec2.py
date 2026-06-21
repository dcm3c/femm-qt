# ACElec2.py - Current flow example (port of ACElec2.lua)
# Creates geometry, assigns materials/boundaries, solves, and loads results.

# Create a new current-flow document
create(3)

# Define some parameters
ri = 1
ro = 1.5
z = 1.5

# Draw the geometry
ci_drawline(ri, -z, ri, z)
ci_drawline(ri, z, ro, z)
ci_drawline(ro, z, ro, -z)
ci_drawline(ri, -z, ro, -z)
ci_addnode(ri, z - 0.1)
ci_addnode(ro, -z + 0.15)
ci_addnode(ro, -z + 0.25)

ci_drawarc(0, -4, 0, 4, 180, 2)
ci_drawline(0, -4, 0, 4)

# Add some materials
ci_addmaterial('Air', 0, 0, 1, 1, 0, 0)
ci_addmaterial('Ceramic', 1e-8, 1e-8, 6, 6, 0, 0)

# And assign them to some block labels
ci_addblocklabel((ri + ro) / 2, 0)
ci_selectlabel((ri + ro) / 2, 0)
ci_setblockprop('Ceramic', 0, 0.05, 0)
ci_clearselected()

ci_addblocklabel(ri / 2, 0)
ci_selectlabel(ri / 2, 0)
ci_setblockprop('Air', 0, 0.05, 0)
ci_clearselected()

# Add some boundary properties
ci_addboundprop('U+', 5, 0, 0, 0, 0)
ci_addboundprop('U-', -5, 0, 0, 0, 0)

ci_selectsegment(ro, -z + 0.1)
ci_selectsegment((ro + ri) / 2, -z)
ci_selectsegment(ri, 0)
ci_setsegmentprop('U+', 0, 1, 0, 0, '<None>')
ci_clearselected()

ci_selectsegment(ro, 0)
ci_setsegmentprop('U-', 0, 1, 0, 0, '<None>')
ci_clearselected()

ci_zoomnatural()

# Save, analyze, and view results
ci_saveas('ACElec2.fec')
ci_analyze()
ci_loadsolution()
