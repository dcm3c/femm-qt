% SPDX-License-Identifier: MIT
% Copyright (C) 2026 David Meeker and contributors
% ActiveFEMM (C)2006 David Meeker, dmeeker@ieee.org

function hi_moverotate2(p1,p2,p3,p4)
if (nargin==4)
	bx=p1; by=p2; shiftangle=p3; editaction=p4;
elseif (nargin==3)
	bx=p1(1); by=p1(2); shiftangle=p2; editaction=p3;
end
callfemm(['hi_moverotate(' , numc(bx) , numc(by) , numc(shiftangle) , num(editaction) , ')' ]);
