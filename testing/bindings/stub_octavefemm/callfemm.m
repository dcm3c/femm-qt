% Recording stub. Shadows octavefemm/callfemm.m when this directory is later on
% the path, so a wrapper's command string is captured instead of sent. Returns
% [] -- wrappers that post-process a reply will then error, which the probe
% catches; the emission has already been recorded by that point.
function z = callfemm(x)
    global EMITTED
    EMITTED{end+1} = x;
    z = [];
end
