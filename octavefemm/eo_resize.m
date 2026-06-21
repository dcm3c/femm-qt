% SPDX-License-Identifier: MIT
% Copyright (C) 2026 David Meeker and contributors
% ActiveFEMM (C)2006 David Meeker, dmeeker@ieee.org

function eo_resize(nWidth,nHeight)
	callfemm(['eo_resize(', numc(nWidth), num(nHeight), ')' ]);


