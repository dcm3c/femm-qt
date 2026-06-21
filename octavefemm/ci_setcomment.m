% SPDX-License-Identifier: MIT
% Copyright (C) 2026 David Meeker and contributors
function ci_setcomment(fn)

if (nargin==1)
	callfemm(['ci_setcomment(' , quote(fn) , ')' ]);
end
