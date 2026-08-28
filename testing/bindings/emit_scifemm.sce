// emit_scifemm.sce -- record what every scifemm wrapper emits, driven by
// contract.tsv. No engine is started: callfemm is redefined after the library
// loads, so nothing opens a socket (and no JDK is needed).
//
//     WScilex-cli -quit -f emit_scifemm.sce
//
// Writes emitted_scifemm.tsv:  wrapper <TAB> pass <TAB> emission

HERE = get_absolute_file_path('emit_scifemm.sce');

// Snapshot the namespace around the exec, so we can tell what scifemm actually
// defines from what Scilab already had. isdef() alone matches BUILTINS, and
// scifemm has no messagebox wrapper -- so driving the contract by isdef called
// Scilab's own GUI messagebox(), which popped a modal dialog and sat waiting
// for someone to click OK. That looked exactly like a hung interpreter, and I
// spent a while blaming quit() for it.
before = who('get');
exec(fullfile(HERE, '..', '..', 'scifemm', 'scifemm.sci'), -1);
SCIFEMM_NAMES = setdiff(who('get'), before);

global EMITTED
EMITTED = [];

// Redefined AFTER the exec above, so these shadow the real transport.
// scifemm builds a command as a string MATRIX -- ['mi_addarc(' , num(x) , ')']
// is 1x3 in Scilab, not a concatenation -- and the real transport joins it with
// strcat before sending. The stub must do the same or it records three
// fragments instead of one command.
function z = callfemm(x)
    global EMITTED
    EMITTED = [EMITTED; strcat(x)];
    z = [];
endfunction

function z = callfemm_noeval(x)
    global EMITTED
    EMITTED = [EMITTED; strcat(x)];
    z = [];
endfunction

Q = ascii(34);                       // a double quote, without quoting games
TAB = ascii(9);

function lit = arglit(i, kind, which)
    Q = ascii(34);
    if kind == 's' then
        lit = Q + 's' + string(i) + Q;
    elseif which == 'int' then
        lit = msprintf('%.17g', i);
    elseif which == 'real' then
        lit = msprintf('%.17g', -1.5 * i + 0.25);
    else
        lit = 'complex(' + msprintf('%.17g', i) + ',' + ..
              msprintf('%.17g', -0.25 * i) + ')';
    end
endfunction

lines = mgetl(fullfile(HERE, 'contract.tsv'));
fd = mopen(fullfile(HERE, 'emitted_scifemm.tsv'), 'w');
nok = 0; nmiss = 0; nerr = 0;
passes = ['int', 'real', 'cplx'];

for k = 1:size(lines, '*')
    line = lines(k);
    if line == '' | part(line, 1) == '#' then continue, end
    parts = strsplit(line, TAB);
    name = parts(1);
    kinds = '';
    if size(parts, '*') >= 3 then kinds = parts(3); end
    if find(SCIFEMM_NAMES == name) == [] then
        mfprintf(fd, '%s\tint\tMISSING\n', name);
        nmiss = nmiss + 1;
        continue
    end
    gotany = %f;
    for p = 1:3
        which = passes(p);
        lits = [];
        for i = 1:length(kinds)
            lits = [lits; arglit(i, part(kinds, i), which)];
        end
        if size(lits, '*') == 0 then
            callstr = name + '()';
        else
            callstr = name + '(' + strcat(lits', ',') + ')';
        end
        EMITTED = [];
        ierr = execstr(callstr, 'errcatch');
        if ierr <> 0 & size(EMITTED, '*') == 0 then
            msg = strsubst(lasterror(), TAB, ' ');
            if size(msg, '*') > 1 then msg = msg(1); end
            mfprintf(fd, '%s\t%s\tERROR:%s\n', name, which, msg);
            nerr = nerr + 1;
            continue
        end
        for j = 1:size(EMITTED, '*')
            mfprintf(fd, '%s\t%s\t%s\n', name, which, ..
                     strsubst(EMITTED(j), TAB, ' '));
            gotany = %t;
        end
    end
    if gotany then nok = nok + 1; end
end
mclose(fd);
mprintf('scifemm: %d wrappers recorded, %d missing, %d refused the contract\n', ..
        nok, nmiss, nerr);

// Belt and braces. The apparent nine-minute hang here was NOT a shutdown
// problem -- it was the modal dialog described above -- but an explicit quit
// costs nothing.
quit();
