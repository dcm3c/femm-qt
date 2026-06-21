import femm
import matplotlib.pyplot as plt

femm.openfemm()
femm.opendocument("coilgun.fem");
femm.mi_saveas("temp.fem")
femm.mi_seteditmode("group")
z=[];
f=[];
for n in range(0,16):
	femm.mi_analyze()
	femm.mi_loadsolution()
	femm.mo_groupselectblock(1)
	fz=femm.mo_blockintegral(19)
	z.append(n*0.1)
	f.append(fz)
	femm.mi_selectgroup(1)
	femm.mi_movetranslate(0, -0.1)
femm.closefemm()
plt.plot(z,f)
plt.ylabel('Force, N')
plt.xlabel('Offset, in')
plt.show()
