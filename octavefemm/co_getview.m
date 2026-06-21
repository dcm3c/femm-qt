% SPDX-License-Identifier: MIT
% Copyright (C) 2026 David Meeker and contributors
% ActiveFEMM (C)2006 David Meeker, dmeeker@ieee.org

function vu=co_getview()
    fname=tempname;
    callfemm(['co_savebitmap(', quote(fname), ')']);
    vu=imread(fname,'bmp');
    system(['del ',rawquote(fname)]);

