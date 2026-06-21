% SPDX-License-Identifier: MIT
% Copyright (C) 2026 David Meeker and contributors
function z=mo_getgapa(bdryname,angle)
	z=callfemm(['mo_getgapa(' , quotec(bdryname) , num(angle) , ')']);

