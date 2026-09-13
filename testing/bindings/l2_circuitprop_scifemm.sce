// Layer 2, scifemm: MOCircuitPropTest driven through the binding.
// A line-for-line translation of testing/MOCircuitPropTest.py, checked by
// MOCircuitPropCompare.py against the same Original FEMM golden.

HERE = get_absolute_file_path('l2_circuitprop_scifemm.sce');
exec(fullfile(HERE, '..', '..', 'scifemm', 'scifemm.sci'), -1);
TESTS = fullfile(HERE, '..');
// TMPDIR is Scilab's own per-session scratch directory, created for us
// on every platform. Scratch files here are not read back after the run.
TMP = TMPDIR;

function p = T(name)
    global G_TESTS
    p = strsubst(fullfile(G_TESTS, name), ascii(92), '/');
endfunction

function p = W(name)
    global G_TMP
    p = strsubst(fullfile(G_TMP, name), ascii(92), '/');
endfunction

function s = fmt(z)
    s = msprintf('%.10g %.10g', real(z), imag(z));
endfunction

function report(circuits, tag)
    mi_analyze(); mi_loadsolution();
    mprintf('===  %s  ===\n', tag);
    for j = 1:size(circuits, '*')
        c = circuits(j);
        z = mo_getcircuitproperties(c);
        mprintf('  %-14s = %s | %s | %s\n', c, fmt(z(1)), fmt(z(2)), fmt(z(3)));
    end
    mo_close();
endfunction

function setrmb(cur)
    for j = 1:4
        mi_modifycircprop(msprintf('i%d', j), 1, cur(j));
    end
endfunction

function settorq(a, b)
    mi_modifycircprop('i1', 1, a);
    mi_modifycircprop('i2', 1, b);
endfunction

global G_TESTS G_TMP
G_TESTS = TESTS; G_TMP = TMP;

RMBC = ['i1', 'i2', 'i3', 'i4'];
TQC = ['i1', 'i2'];
ROTC = ['Coil Current'];

openfemm(1);

// ---- RadialMagneticBearing ----
cases = list(list([12 6 0 6], 0, 'RMB (x, DC) i=(12,6,0,6)'), ..
             list([12 6 0 6], 60, 'RMB (x, AC 60Hz) i=(12,6,0,6)'), ..
             list([6 12 6 0], 0, 'RMB (y, DC) i=(6,12,6,0)'), ..
             list([6 12 6 0], 60, 'RMB (y, AC 60Hz) i=(6,12,6,0)'));
for k = 1:length(cases)
    cur = cases(k)(1); f = cases(k)(2); tag = cases(k)(3);
    opendocument(T('RadialMagneticBearing.fem')); mi_saveas(W('temp.fem'));
    setrmb(cur); mi_probdef(f, 'inches', 'planar');
    report(RMBC, tag);
end
opendocument(T('RadialMagneticBearing.fem')); mi_saveas(W('bias.fem'));
setrmb([6 6 6 6]); mi_probdef(0, 'inches', 'planar'); mi_analyze();
mi_saveas(W('incr.fem')); mi_setprevious(W('bias.ans'), 1);
setrmb([1 0 -1 0]); mi_probdef(60, 'inches', 'planar');
report(RMBC, 'RMB (x-incremental) bias 6A; AC i=(1,0,-1,0)');

// ---- roters1b ----
opendocument(T('roters1b.fem')); mi_saveas(W('rtemp.fem'));
mi_modifycircprop('Coil Current', 1, 0.8); mi_probdef(0, 'inches', 'axi');
report(ROTC, 'roters1b (DC) 0.8A');
opendocument(T('roters1b.fem')); mi_saveas(W('rtemp.fem'));
mi_modifycircprop('Coil Current', 1, 0.8); mi_probdef(60, 'inches', 'axi');
report(ROTC, 'roters1b (AC 60Hz) 0.8A');
opendocument(T('roters1b.fem')); mi_saveas(W('rbias.fem'));
mi_modifycircprop('Coil Current', 1, 0.8); mi_probdef(0, 'inches', 'axi'); mi_analyze();
mi_saveas(W('rincr.fem')); mi_setprevious(W('rbias.ans'), 1);
mi_modifycircprop('Coil Current', 1, 0.1); mi_probdef(60, 'inches', 'axi');
report(ROTC, 'roters1b (incremental) bias 0.8A; AC 0.1A');

// ---- torque ----
opendocument(T('torque.fem')); mi_saveas(W('temp.fem'));
settorq(2, 0); mi_probdef(0, 'inches', 'planar');
report(TQC, 'torque (DC) i=(2,0)');
opendocument(T('torque.fem')); mi_saveas(W('temp.fem'));
settorq(2, 0); mi_probdef(60, 'inches', 'planar');
report(TQC, 'torque (AC 60Hz) i=(2,0)');
opendocument(T('torque.fem')); mi_saveas(W('bias.fem'));
settorq(1, 1); mi_probdef(0, 'inches', 'planar'); mi_analyze();
mi_saveas(W('incr.fem')); mi_setprevious(W('bias.ans'), 1);
settorq(1, -1); mi_probdef(60, 'inches', 'planar');
report(TQC, 'torque (incremental) bias i=(1,1); AC i=(1,-1)');

// ---- Lorentz coils ----
lorentz = list(list('AxisymmetricLorentzForce.fem', 'axi', 'AxiLorentz'), ..
               list('PlanarLorentzForce.fem', 'planar', 'PlanarLorentz-y'), ..
               list('PlanarLorentzForceAlt.fem', 'planar', 'PlanarLorentz-x'));
for k = 1:length(lorentz)
    fem = lorentz(k)(1); ptype = lorentz(k)(2); tag = lorentz(k)(3);
    opendocument(T(fem)); mi_saveas(W('temp.fem'));
    mi_modifycircprop('i1', 1, 16); mi_probdef(0, 'inches', ptype);
    report(['i1'], tag + ' (DC) 16A');
    opendocument(T(fem)); mi_saveas(W('temp.fem'));
    mi_modifycircprop('i1', 1, 16); mi_probdef(60, 'inches', ptype);
    report(['i1'], tag + ' (AC 60Hz) 16A');
end

mprintf('\nDone.\n');
closefemm();
quit();
