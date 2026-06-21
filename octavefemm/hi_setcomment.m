% SPDX-License-Identifier: MIT
% Copyright (C) 2026 David Meeker and contributors
function hi_setcomment(fn)

if (nargin==1)
	callfemm(['hi_setcomment(' , quote(fn) , ')' ]);
end
