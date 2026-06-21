% SPDX-License-Identifier: MIT
% Copyright (C) 2026 David Meeker and contributors
function mi_setcomment(fn)

if (nargin==1)
	callfemm(['mi_setcomment(' , quote(fn) , ')' ]);
end
