% SPDX-License-Identifier: MIT
% Copyright (C) 2026 David Meeker and contributors
% numc(x) -- num(x) followed by the argument separator. See num.m for why
% this must go through complex2str rather than num2str.
function y = numc(x)
    y = [complex2str(x), ','];
end
