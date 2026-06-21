% SPDX-License-Identifier: MIT
% Copyright (C) 2026 David Meeker and contributors
% ActiveFEMM (C)2006 David Meeker, dmeeker@ieee.org

function ci_setnodeprop(nodeprop,groupno,inconductor)
callfemm(['ci_setnodeprop(' , quotec(nodeprop) , numc(groupno) , quote(inconductor) , ')']);

