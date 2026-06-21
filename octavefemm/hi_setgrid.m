% SPDX-License-Identifier: MIT
% Copyright (C) 2026 David Meeker and contributors
% ActiveFEMM (C)2006 David Meeker, dmeeker@ieee.org

function hi_setgrid(density,ptype)
callfemm(['hi_setgrid(' , numc(density) , quote(ptype) , ')' ]);

