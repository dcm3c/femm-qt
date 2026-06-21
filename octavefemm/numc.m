% SPDX-License-Identifier: MIT
% Copyright (C) 2026 David Meeker and contributors
function y = numc(x)
    y = [num2str(x, '%.17g'), ','];
end
