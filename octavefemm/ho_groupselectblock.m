% SPDX-License-Identifier: MIT
% Copyright (C) 2026 David Meeker and contributors
% ActiveFEMM (C)2006 David Meeker, dmeeker@ieee.org

function ho_groupselectblock(n)
if(nargin==1)
callfemm(['ho_groupselectblock(' , num(n) , ')' ]);
else
callfemm('ho_groupselectblock()');
end

