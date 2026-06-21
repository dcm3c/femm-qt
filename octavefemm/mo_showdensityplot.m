% SPDX-License-Identifier: MIT
% Copyright (C) 2026 David Meeker and contributors
% ActiveFEMM (C)2006 David Meeker, dmeeker@ieee.org

function mo_showdensityplot(legend,gscale,bu,bl,ptype)
callfemm(['mo_showdensityplot(' , numc(legend) , numc(gscale) , numc(bu) , numc(bl) , quote(ptype) , ')' ]);

