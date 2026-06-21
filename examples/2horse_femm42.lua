-- open up the base geometry file
open("2horse.fem")

-- save in a temporary file for purposes of
-- performing the analysis
mi_saveas("temp.fem")

for k = 1,12,1 do

	freq = k/4

	-- change the frequency to the desired one
	-- for this particular iteration
	mi_probdef(freq,"millimeters","planar",1e-8,100,20)

	-- run the analysis and load the solution
	mi_analyze(1)
	mi_loadsolution()

	-- look at the flux linkage for phase A.
	i,v,flux = mo_getcircuitproperties("A")

	-- get the torque on the rotor
	mo_groupselectblock(4)
	torque = mo_blockintegral(22)
	
	-- write results to a data file, multiplying by 4 to get
	-- the results for all 4 poles of the machine.
	handle=openfile("results.txt","a");	
	write(handle,freq,"	",4*flux,"	",4*torque,"\n")
	closefile(handle)

end
	
