% Layer 2, octavefemm: MOCircuitPropTest driven through the binding.
%
%   matlab     -batch "l2_circuitprop_octave"
%   octave-cli --no-gui -q l2_circuitprop_octave.m
%
% A translation of testing/MOCircuitPropTest.py, checked by
% MOCircuitPropCompare.py against the same Original FEMM golden.
%
% No local functions: MATLAB wants them last in a script, Octave wants them
% defined before use, and those cannot both be satisfied. So the 17 cases are a
% data table and the body is written once. That is arguably clearer than the
% original's helper calls anyway -- the incremental cases differ only by having
% a bias solve, which is now visible as a field rather than as extra statements.

here = fileparts(mfilename('fullpath'));
addpath(fullfile(here, '..', '..', 'octavefemm'));
tests = fullfile(here, '..');
% tempdir is the platform's own temp directory: honours TMPDIR on POSIX and TEMP on Windows, so no per-platform branch is needed.
tmp = tempdir;

fmt = @(z) sprintf('%.10g %.10g', real(z), imag(z));
T = @(n) strrep(fullfile(tests, n), '\', '/');
W = @(n) strrep(fullfile(tmp, n), '\', '/');

RMBC = {'i1', 'i2', 'i3', 'i4'};
TQC = {'i1', 'i2'};
ROTC = {'Coil Current'};

% {model, bias currents ({} = none), currents, freq, ptype, circuits, tag}
% A non-empty bias set means: solve it at DC first, then set it as the previous
% solution -- the incremental-permeability cases.
C = {
 {'RadialMagneticBearing.fem', {}, {12,6,0,6}, 0, 'planar', RMBC, 'RMB (x, DC) i=(12,6,0,6)'}
 {'RadialMagneticBearing.fem', {}, {12,6,0,6}, 60, 'planar', RMBC, 'RMB (x, AC 60Hz) i=(12,6,0,6)'}
 {'RadialMagneticBearing.fem', {}, {6,12,6,0}, 0, 'planar', RMBC, 'RMB (y, DC) i=(6,12,6,0)'}
 {'RadialMagneticBearing.fem', {}, {6,12,6,0}, 60, 'planar', RMBC, 'RMB (y, AC 60Hz) i=(6,12,6,0)'}
 {'RadialMagneticBearing.fem', {6,6,6,6}, {1,0,-1,0}, 60, 'planar', RMBC, 'RMB (x-incremental) bias 6A; AC i=(1,0,-1,0)'}
 {'roters1b.fem', {}, {0.8}, 0, 'axi', ROTC, 'roters1b (DC) 0.8A'}
 {'roters1b.fem', {}, {0.8}, 60, 'axi', ROTC, 'roters1b (AC 60Hz) 0.8A'}
 {'roters1b.fem', {0.8}, {0.1}, 60, 'axi', ROTC, 'roters1b (incremental) bias 0.8A; AC 0.1A'}
 {'torque.fem', {}, {2,0}, 0, 'planar', TQC, 'torque (DC) i=(2,0)'}
 {'torque.fem', {}, {2,0}, 60, 'planar', TQC, 'torque (AC 60Hz) i=(2,0)'}
 {'torque.fem', {1,1}, {1,-1}, 60, 'planar', TQC, 'torque (incremental) bias i=(1,1); AC i=(1,-1)'}
 {'AxisymmetricLorentzForce.fem', {}, {16}, 0, 'axi', {'i1'}, 'AxiLorentz (DC) 16A'}
 {'AxisymmetricLorentzForce.fem', {}, {16}, 60, 'axi', {'i1'}, 'AxiLorentz (AC 60Hz) 16A'}
 {'PlanarLorentzForce.fem', {}, {16}, 0, 'planar', {'i1'}, 'PlanarLorentz-y (DC) 16A'}
 {'PlanarLorentzForce.fem', {}, {16}, 60, 'planar', {'i1'}, 'PlanarLorentz-y (AC 60Hz) 16A'}
 {'PlanarLorentzForceAlt.fem', {}, {16}, 0, 'planar', {'i1'}, 'PlanarLorentz-x (DC) 16A'}
 {'PlanarLorentzForceAlt.fem', {}, {16}, 60, 'planar', {'i1'}, 'PlanarLorentz-x (AC 60Hz) 16A'}
};

openfemm(1);
for k = 1:numel(C)
    model = C{k}{1}; bias = C{k}{2}; cur = C{k}{3};
    freq = C{k}{4}; ptype = C{k}{5}; circs = C{k}{6}; tag = C{k}{7};

    opendocument(T(model));
    if isempty(bias)
        mi_saveas(W('temp.fem'));
    else
        mi_saveas(W('bias.fem'));
        for j = 1:numel(bias)
            mi_modifycircprop(circs{j}, 1, bias{j});
        end
        mi_probdef(0, 'inches', ptype);
        mi_analyze();
        mi_saveas(W('incr.fem'));
        mi_setprevious(W('bias.ans'), 1);
    end
    for j = 1:numel(cur)
        mi_modifycircprop(circs{j}, 1, cur{j});
    end
    mi_probdef(freq, 'inches', ptype);

    mi_analyze();
    mi_loadsolution();
    fprintf('===  %s  ===\n', tag);
    for j = 1:numel(circs)
        z = mo_getcircuitproperties(circs{j});
        fprintf('  %-14s = %s | %s | %s\n', circs{j}, ...
                fmt(z(1)), fmt(z(2)), fmt(z(3)));
    end
    mo_close();
end
fprintf('\nDone.\n');
closefemm();
