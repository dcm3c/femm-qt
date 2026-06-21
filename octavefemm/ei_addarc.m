% SPDX-License-Identifier: MIT
% Copyright (C) 2026 David Meeker and contributors
% ActiveFEMM (C)2006 David Meeker, dmeeker@ieee.org

function ei_addarc(x1,y1,x2,y2,angle,maxseg)
callfemm(['ei_addarc(' , numc(x1) , numc(y1) , numc(x2) , numc(y2) , numc(angle) , num(maxseg) , ')']);

