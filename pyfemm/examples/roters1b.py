# Roters1b: Simulation of a Tapered Plunger Magnet
# David Meeker
# dmeeker@ieee.org
#
# This geometry comes from Chap. IX, Figure 7 of Herbert Roters
# classic book, Electromagnetic Devices.  The program moves
# the plunger of the magnet over a stroke of 1.5in at 1/10in increments
# solving for the field and evaluating the force on the plunger at
# each position.  When all positions have been evaluated, the program
# plots a curve of the finite element force predictions.

import femm
import matplotlib.pyplot as plt

femm.openfemm()
femm.opendocument('roters1b.fem');
femm.mi_saveas('temp.fem');

n=16;
stroke=1.5;
x=[]
f=[]

for k in range (0,n):
	print('iteration %i of %i' % (k , (n-1)))
	femm.mi_analyze()
	femm.mi_loadsolution()
	femm.mo_groupselectblock(1)
	x.append(stroke*k/(n-1))
	f.append(femm.mo_blockintegral(19)/4.4481)
	femm.mi_selectgroup(1)
	femm.mi_movetranslate(0,-stroke/(n-1));
	femm.mi_clearselected()
femm.closefemm()

plt.plot(x,f)
plt.xlabel('Displacement, Inches');
plt.ylabel('Force, Lbf');
plt.title('Plunger Force vs. Displacement');
plt.show()

