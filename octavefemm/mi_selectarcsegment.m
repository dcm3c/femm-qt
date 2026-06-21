% SPDX-License-Identifier: MIT
% Copyright (C) 2026 David Meeker and contributors
% ActiveFEMM (C)2006 David Meeker, dmeeker@ieee.org

function z=mi_selectarcsegment(x,y)
if (nargin==2)
	z=callfemm(['mi_selectarcsegment(' , numc(x) , num(y) , ')']);
elseif (nargin==1)
	z=callfemm(['mi_selectarcsegment(' , numc(x(1)) , num(x(2)) , ')']);
end

