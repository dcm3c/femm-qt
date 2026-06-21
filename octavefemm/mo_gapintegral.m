% SPDX-License-Identifier: MIT
% Copyright (C) 2026 David Meeker and contributors
function z=mo_gapintegral(bdryname,inttype)
	z=callfemm(['mo_gapintegral(' , quotec(bdryname) , num(inttype) , ')']);
