% SPDX-License-Identifier: MIT
% Copyright (C) 2026 David Meeker and contributors
% eo_getallelements() — retrieve all mesh element data in one call.
% Returns an N×7 matrix: [p1 p2 p3 ctr_x ctr_y area group]
% Node indices are 1-based.
%
% This used to build the result client-side, looping eo_getelement through the
% straccum/strappend accumulator, even though the engine has had a C++
% eo_getallelements since the first MicroPython commit. That was three round
% trips and the slowest loop shape available, to reproduce what one call already
% returns. See git history if the generic client-side technique is ever wanted
% for something the engine genuinely cannot do.

function z = eo_getallelements()
    raw = callfemm('eo_getallelements()');
    z = reshape(raw, 7, [])';
