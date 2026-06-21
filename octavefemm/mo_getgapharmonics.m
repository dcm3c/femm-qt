% SPDX-License-Identifier: MIT
% Copyright (C) 2026 David Meeker and contributors
function z=mo_getgapharmonics(bdryname,n)
    if (nargin==1)
        z=callfemm(['mo_getgapharmonics(' , quote(bdryname) ')']);
    else
        nh=length(n);
        z=zeros(nh,6);
        for k=1:nh
            z(k,:)=callfemm(['mo_getgapharmonics(' , quotec(bdryname) , num(n(k)) , ')']);
        end
    end
