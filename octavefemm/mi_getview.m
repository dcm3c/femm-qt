% SPDX-License-Identifier: MIT
% Copyright (C) 2026 David Meeker and contributors
% ActiveFEMM (C)2006 David Meeker, dmeeker@ieee.org

function vu=mi_getview()
    % femm-qt writes PNG whatever the extension, so ask for .png.
    fname=[tempname '.png'];
    callfemm(['mi_savebitmap(', quote(fname), ')']);
    % No format argument. imread(fname,'bmp') FORCED the BMP reader and
    % failed on the PNG the engine writes: "Unrecognized BMP format
    % signature 'P'".
    vu=imread(fname);
    % delete() rather than a shell "del": the toolbox also runs on
    % Linux and macOS.
    delete(fname);
