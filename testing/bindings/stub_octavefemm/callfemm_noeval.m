% Recording stub for the no-reply path. See callfemm.m in this directory.
function callfemm_noeval(x)
    global EMITTED
    EMITTED{end+1} = x;
end
