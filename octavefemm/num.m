% SPDX-License-Identifier: MIT
% Copyright (C) 2026 David Meeker and contributors
% num(x) -- format one numeric argument for the wire.
%
% MUST go through complex2str. The interpreter's imaginary unit is I, so a
% complex argument has to travel as 1+I*2; num2str produces MATLAB's own
% 1+2i, which the wire rejects with "invalid syntax for integer with base
% 10: '2i'". Reals format identically either way, which is why this went
% unnoticed -- only complex arguments reach it, e.g. mi_modifycircprop at a
% nonzero frequency. scifemm and juliafemm always called complex2str.
function y = num(x)
    y = complex2str(x);
end
