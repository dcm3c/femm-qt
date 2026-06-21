% SPDX-License-Identifier: MIT
% Copyright (C) 2026 David Meeker and contributors
% ActiveFEMM (C)2006 David Meeker, dmeeker@ieee.org

function mi_createradius(x,y,r)
callfemm(['mi_createradius(' , numc(x) , numc(y), num(r) , ')' ]);


