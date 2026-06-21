% SPDX-License-Identifier: MIT
% Copyright (C) 2026 David Meeker and contributors
% ActiveFEMM (C)2006 David Meeker, dmeeker@ieee.org

function mo_addcontour(x,y)
if (nargin==2)
	callfemm(['mo_addcontour(' , numc(x) , num(y) , ')' ]);
elseif (nargin==1)
	callfemm(['mo_addcontour(' , numc(x(1)) , num(x(2)) , ')' ]);
end

