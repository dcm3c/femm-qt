% SPDX-License-Identifier: MIT
% Copyright (C) 2026 David Meeker and contributors
% ActiveFEMM (C)2006 David Meeker, dmeeker@ieee.org

function z=hi_selectsegment(x,y)
if (nargin==2)
	z=callfemm(['hi_selectsegment(' , numc(x) , num(y) , ')']);
elseif (nargin==1)
	z=callfemm(['hi_selectsegment(' , numc(x(1)) , num(x(2)) , ')']);
end

