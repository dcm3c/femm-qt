% Layer 2, octavefemm: MOCircuitPropTest driven through the binding.
% A line-for-line translation of testing/MOCircuitPropTest.py, checked by
% MOCircuitPropCompare.py against the same Original FEMM golden.
%
% Paths are absolute: the in-process test relies on -run having chdir'd into
% testing/, which a binding does not do.

here = fileparts(mfilename('fullpath'));
addpath(fullfile(here, '..', '..', 'octavefemm'));
tests = fullfile(here, '..');
tmp = getenv('TEMP'); if isempty(tmp), tmp = 'C:/temp'; end

openfemm(1);
cleaner = onCleanup(@() closefemm());

RMBC = {'i1', 'i2', 'i3', 'i4'};
TQC = {'i1', 'i2'};
ROTC = {'Coil Current'};

% ---- RadialMagneticBearing (i1-i4) ----
for k = 1:4
    switch k
        case 1, cur = [12 6 0 6]; f = 0;  tag = 'RMB (x, DC) i=(12,6,0,6)';
        case 2, cur = [12 6 0 6]; f = 60; tag = 'RMB (x, AC 60Hz) i=(12,6,0,6)';
        case 3, cur = [6 12 6 0]; f = 0;  tag = 'RMB (y, DC) i=(6,12,6,0)';
        case 4, cur = [6 12 6 0]; f = 60; tag = 'RMB (y, AC 60Hz) i=(6,12,6,0)';
    end
    opendocument(T(tests, 'RadialMagneticBearing.fem'));
    mi_saveas(W(tmp, 'temp.fem'));
    setrmb(cur); mi_probdef(f, 'inches', 'planar');
    report(RMBC, tag);
end
opendocument(T(tests, 'RadialMagneticBearing.fem')); mi_saveas(W(tmp, 'bias.fem'));
setrmb([6 6 6 6]); mi_probdef(0, 'inches', 'planar'); mi_analyze();
mi_saveas(W(tmp, 'incr.fem')); mi_setprevious(W(tmp, 'bias.ans'), 1);
setrmb([1 0 -1 0]); mi_probdef(60, 'inches', 'planar');
report(RMBC, 'RMB (x-incremental) bias 6A; AC i=(1,0,-1,0)');

% ---- roters1b (Coil Current) ----
opendocument(T(tests, 'roters1b.fem')); mi_saveas(W(tmp, 'rtemp.fem'));
mi_modifycircprop('Coil Current', 1, 0.8); mi_probdef(0, 'inches', 'axi');
report(ROTC, 'roters1b (DC) 0.8A');
opendocument(T(tests, 'roters1b.fem')); mi_saveas(W(tmp, 'rtemp.fem'));
mi_modifycircprop('Coil Current', 1, 0.8); mi_probdef(60, 'inches', 'axi');
report(ROTC, 'roters1b (AC 60Hz) 0.8A');
opendocument(T(tests, 'roters1b.fem')); mi_saveas(W(tmp, 'rbias.fem'));
mi_modifycircprop('Coil Current', 1, 0.8); mi_probdef(0, 'inches', 'axi'); mi_analyze();
mi_saveas(W(tmp, 'rincr.fem')); mi_setprevious(W(tmp, 'rbias.ans'), 1);
mi_modifycircprop('Coil Current', 1, 0.1); mi_probdef(60, 'inches', 'axi');
report(ROTC, 'roters1b (incremental) bias 0.8A; AC 0.1A');

% ---- torque (i1, i2) ----
opendocument(T(tests, 'torque.fem')); mi_saveas(W(tmp, 'temp.fem'));
settorq(2, 0); mi_probdef(0, 'inches', 'planar');
report(TQC, 'torque (DC) i=(2,0)');
opendocument(T(tests, 'torque.fem')); mi_saveas(W(tmp, 'temp.fem'));
settorq(2, 0); mi_probdef(60, 'inches', 'planar');
report(TQC, 'torque (AC 60Hz) i=(2,0)');
opendocument(T(tests, 'torque.fem')); mi_saveas(W(tmp, 'bias.fem'));
settorq(1, 1); mi_probdef(0, 'inches', 'planar'); mi_analyze();
mi_saveas(W(tmp, 'incr.fem')); mi_setprevious(W(tmp, 'bias.ans'), 1);
settorq(1, -1); mi_probdef(60, 'inches', 'planar');
report(TQC, 'torque (incremental) bias i=(1,1); AC i=(1,-1)');

% ---- Lorentz coils (i1) ----
lor = {{'AxisymmetricLorentzForce.fem', 'axi', 'AxiLorentz'}, ...
       {'PlanarLorentzForce.fem', 'planar', 'PlanarLorentz-y'}, ...
       {'PlanarLorentzForceAlt.fem', 'planar', 'PlanarLorentz-x'}};
for k = 1:numel(lor)
    fem = lor{k}{1}; ptype = lor{k}{2}; tag = lor{k}{3};
    opendocument(T(tests, fem)); mi_saveas(W(tmp, 'temp.fem'));
    mi_modifycircprop('i1', 1, 16); mi_probdef(0, 'inches', ptype);
    report({'i1'}, [tag ' (DC) 16A']);
    opendocument(T(tests, fem)); mi_saveas(W(tmp, 'temp.fem'));
    mi_modifycircprop('i1', 1, 16); mi_probdef(60, 'inches', ptype);
    report({'i1'}, [tag ' (AC 60Hz) 16A']);
end

fprintf('\nDone.\n');

function p = T(tests, name)
    p = strrep(fullfile(tests, name), '\', '/');
end

function p = W(tmp, name)
    p = strrep(fullfile(tmp, name), '\', '/');
end

function s = fmt(z)
    s = sprintf('%.10g %.10g', real(z), imag(z));
end

function setrmb(cur)
    for j = 1:4
        mi_modifycircprop(sprintf('i%d', j), 1, cur(j));
    end
end

function settorq(a, b)
    mi_modifycircprop('i1', 1, a);
    mi_modifycircprop('i2', 1, b);
end

function report(circuits, tag)
    mi_analyze(); mi_loadsolution();
    fprintf('===  %s  ===\n', tag);
    for j = 1:numel(circuits)
        c = circuits{j};
        z = mo_getcircuitproperties(c);
        fprintf('  %-14s = %s | %s | %s\n', c, fmt(z(1)), fmt(z(2)), fmt(z(3)));
    end
    mo_close();
end
