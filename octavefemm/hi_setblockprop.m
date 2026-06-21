% SPDX-License-Identifier: MIT
% Copyright (C) 2026 David Meeker and contributors
% ActiveFEMM (C)2006 David Meeker, dmeeker@ieee.org

function hi_setblockprop(blockname,automesh,meshsize,group)
callfemm(['hi_setblockprop(' , quotec(blockname) , numc(automesh) , numc(meshsize) , num(group) , ')']);

