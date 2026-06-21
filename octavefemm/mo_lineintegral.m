% SPDX-License-Identifier: MIT
% Copyright (C) 2026 David Meeker and contributors
% ActiveFEMM (C)2006 David Meeker, dmeeker@ieee.org

function z=mo_lineintegral(ptype)
	z=callfemm(['mo_lineintegral(' , num(ptype) , ')']);

