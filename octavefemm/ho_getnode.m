% SPDX-License-Identifier: MIT
% Copyright (C) 2026 David Meeker and contributors
% ActiveFEMM (C)2006 David Meeker, dmeeker@ieee.org

function z=ho_getnode(n)
z=callfemm(['ho_getnode(' , num(n) , ')' ]);

