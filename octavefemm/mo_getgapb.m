% SPDX-License-Identifier: MIT
% Copyright (C) 2026 David Meeker and contributors
function z=mo_getgapb(bdryname,angle)
	z=callfemm(['mo_getgapb(' , quotec(bdryname) , num(angle) , ')']);

