% SPDX-License-Identifier: MIT
% Copyright (C) 2026 David Meeker and contributors
% ActiveFEMM (C)2006 David Meeker, dmeeker@ieee.org

function eo_groupselectblock(n)
if(nargin==1)
callfemm(['eo_groupselectblock(' , num(n) , ')' ]);
else
callfemm('eo_groupselectblock()');
end

