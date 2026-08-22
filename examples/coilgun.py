opendocument("coilgun.fem");
mi_saveas("temp.fem")
mi_seteditmode("group")
z=[];
f=[];
for n in range(0,16):
	mi_analyze()
	mi_loadsolution()
	mo_groupselectblock(1)
	fz=mo_blockintegral(19)
	z.append(n*0.1)
	f.append(fz)
	mi_selectgroup(1)
	mi_movetranslate(0, -0.1)
plot(z,'Force, N',f,'Offset, in')