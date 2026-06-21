% SPDX-License-Identifier: MIT
% Copyright (C) 2026 David Meeker and contributors
function ei_setcomment(fn)

if (nargin==1)
	callfemm(['ei_setcomment(' , quote(fn) , ')' ]);
end
