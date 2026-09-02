# SPDX-License-Identifier: MIT
# Copyright (C) 2026 David Meeker and contributors
# juliafemm.jl -- Julia interface to femm-qt via TCP
#
# Single instance:
#   using .JuliaFEMM
#   openfemm()
#   mi_addnode(0, 0)
#   closefemm()
#
# Multi-instance:
#   openfemm(port=48015)             # first instance
#   openfemm(port=48016, headless=true)  # second instance
#   femmselect(48015)                # commands go to first
#   mi_addnode(0, 0)
#   femmselect(48016)                # switch to second
#   mi_addnode(1, 1)

module JuliaFEMM

using Sockets

# ---------------------------------------------------------------------------
# Module state — multiple connections keyed by port
# ---------------------------------------------------------------------------
const _sockets = Dict{Int, TCPSocket}()
const _active_port = Ref{Int}(-1)

# ---------------------------------------------------------------------------
# Helper: format numbers for FEMM commands
# ---------------------------------------------------------------------------
function complex2str(x::Number)::String
    if isa(x, Complex)
        re = real(x)
        im_part = imag(x)
        if im_part == 0
            return string(re)
        elseif im_part == 1
            return re == 0 ? "I" : "$(re)+I"
        elseif im_part == -1
            return re == 0 ? "-I" : "$(re)-I"
        elseif im_part < 0
            return re != 0 ? "$(re)-I*$(abs(im_part))" : "-I*$(abs(im_part))"
        else  # im_part > 0
            return re != 0 ? "$(re)+I*$(im_part)" : "I*$(im_part)"
        end
    else
        return string(Float64(x))
    end
end

"""Format a number for a FEMM command argument (last in list)."""
num(n::Number) = complex2str(n)
num(n::AbstractString) = n

"""Format a number followed by a comma (not last in list)."""
numc(n::Number) = complex2str(n) * ","
numc(n::AbstractString) = n * ","

"""Wrap a string in escaped double-quotes for FEMM commands."""
function quote_str(u)
    s = string(u)
    s = replace(s, "\\" => "\\\\")
    return "\"" * s * "\""
end

"""Wrap a string in quotes followed by a comma."""
quotec(u) = quote_str(u) * ","

# ---------------------------------------------------------------------------
# TCP IPC layer
# ---------------------------------------------------------------------------

"""
    openfemm(; headless=false, port=48015)

Connect to a running femm-qt instance, or launch one if none is found.
Multiple instances can be opened on different ports; use `femmselect(port)`
to switch which one receives commands.
"""
function openfemm(; headless::Bool=false, port::Int=48015)
    # On Windows spawn the windowless build (femm_headlessw) so no console
    # window flashes; the console femm_headless is for terminal/batch use.
    # Other platforms have only femm_headless.
    exename = headless ? (Sys.iswindows() ? "femm_headlessw" : "femm_headless") : "femm_gui"

    # Try connecting to an already-running instance
    sock = nothing
    try
        sock = connect("127.0.0.1", port)
        # Disable Nagle's algorithm. The small-request/small-response RPC
        # pattern hits Nagle + delayed-ACK pathology and adds 5-200ms of
        # latency per call (depending on the OS's delayed-ACK timer),
        # which is poison for high-frequency uses like parametric sweeps.
        # Note: Sockets.nagle(sock, enabled) — pass false to DISABLE Nagle.
        # femm-qt's IPC server sets TCP_NODELAY on its accepted-client
        # side too; both ends need it.
        Sockets.nagle(sock, false)
    catch
    end

    if sock === nothing
        # Build candidate paths. In each directory try every platform's
        # executable form; the first that exists wins, so no OS branching is
        # needed. The .app form is the macOS application bundle (the GUI builds
        # as <exename>.app, whose real executable is at Contents/MacOS/<exename>);
        # that path only exists on macOS and is harmlessly skipped elsewhere.
        searchdirs = String[]
        femmdir = get(ENV, "FEMM_DIR", "")
        if femmdir != ""
            push!(searchdirs, femmdir)
        end
        moddir = @__DIR__
        push!(searchdirs, joinpath(moddir, "..", "build", "bin"))
        push!(searchdirs, moddir)
        # Last resort: every directory on PATH. An install puts femm_gui
        # and femm_headless there -- symlinks in ~/.local/bin on Unix,
        # {app}\bin on Windows -- so this is what lets the bindings work
        # with no FEMM_DIR set at all. It goes LAST so an explicit
        # FEMM_DIR, or a source tree being worked in, still wins.
        for d in split(get(ENV, "PATH", ""), Sys.iswindows() ? ';' : ':')
            isempty(d) || push!(searchdirs, String(d))
        end
        # macOS keeps programs in /Applications, not on PATH, so the PATH
        # search above cannot find an installed bundle -- and the bundle
        # is where a Mac user puts FEMM. These two directories are that
        # platform's equivalent of a PATH entry; the
        # femm_gui.app/Contents/MacOS/ form tried in every search
        # directory is what reaches inside. Added unconditionally rather
        # than behind an is-this-a-Mac test: on Windows and Linux they
        # simply do not exist, so the check costs two failed lookups, and
        # five languages means five chances to get five different platform
        # tests wrong.
        push!(searchdirs, "/Applications")
        push!(searchdirs, joinpath(homedir(), "Applications"))

        candidates = String[]
        for d in searchdirs
            push!(candidates, joinpath(d, exename * ".exe"))                               # Windows
            push!(candidates, joinpath(d, "femm_gui.app", "Contents", "MacOS", exename))   # macOS: inside the bundle
            push!(candidates, joinpath(d, exename))                                        # Linux / macOS CLI
            push!(candidates, joinpath(d, exename * ".app", "Contents", "MacOS", exename)) # macOS .app
        end

        exe = ""
        for c in candidates
            if isfile(c)
                exe = c
                break
            end
        end
        if exe == ""
            error("Cannot find $exename. Set FEMM_DIR environment variable.")
        end

        # Launch
        cmd = `$exe --port $port`
        run(pipeline(cmd, devnull); wait=false)

        # Wait for TCP server to start
        for _ in 1:50
            sleep(0.1)
            try
                sock = connect("127.0.0.1", port)
                Sockets.nagle(sock, false)
                break
            catch
            end
        end
        if sock === nothing
            error("femm-qt launched but TCP connection failed")
        end
    end

    _sockets[port] = sock
    _active_port[] = port

    cwd = replace(pwd(), "\\" => "/")
    callfemm("setcurrentdirectory($(quote_str(cwd)))")
    nothing
end

"""
    femmselect(port)

Switch which FEMM instance receives commands.
"""
function femmselect(port::Int)
    if !haskey(_sockets, port)
        error("No FEMM connection on port $port. Call openfemm(port=$port) first.")
    end
    _active_port[] = port
    nothing
end

function _active_sock()
    if _active_port[] < 0 || !haskey(_sockets, _active_port[])
        error("Not connected to FEMM. Call openfemm() first.")
    end
    return _sockets[_active_port[]]
end

"""Send a command to FEMM and return the parsed result."""
function callfemm(cmd::AbstractString)
    sock = _active_sock()
    write(sock, cmd * "\n")

    response = rstrip(readline(sock), ['\r'])

    if response == "" || response[1] == ' '
        return 0
    end

    if response[1] == 'e'
        error("FEMM returns: $response")
    end

    response = replace(response, "\t" => ",")
    response = replace(response, r"(?<![A-Za-z])I(?![A-Za-z])" => "im")
    # Parse as a Julia expression. If eval fails (e.g. the response is
    # a string like a filepath from mi_gettitle), return raw string.
    try
        if occursin(',', response)
            return eval(Meta.parse("(" * response * ")"))
        else
            return eval(Meta.parse(response))
        end
    catch
        return response
    end
end

"""Send a command without parsing the response."""
function callfemm_noeval(cmd::AbstractString)
    sock = _active_sock()
    write(sock, cmd * "\n")
    readline(sock)  # drain response
    return 0
end

"""Close the active FEMM connection."""
function closefemm()
    if _active_port[] < 0
        return nothing
    end
    port = _active_port[]
    if haskey(_sockets, port)
        # Send quit command fire-and-forget (server exits before responding)
        try write(_sockets[port], "quit()\n") catch end
        close(_sockets[port])
        delete!(_sockets, port)
    end
    # Switch to another open connection, or mark disconnected
    if isempty(_sockets)
        _active_port[] = -1
    else
        _active_port[] = first(keys(_sockets))
    end
    nothing
end

# ---------------------------------------------------------------------------
# Global / document commands
# ---------------------------------------------------------------------------
opendocument(fn) = callfemm("open($(quote_str(fn)))")
newdocument(n) = callfemm("create($(num(n)))")
smartmesh(n) = callfemm("smartmesh($(num(n)))")

main_maximize() = callfemm("main_maximize()")
main_minimize() = callfemm("main_minimize()")
main_resize(w, h) = callfemm("main_resize($(numc(w))$(num(h)))")
main_restore() = callfemm("main_restore()")

hideconsole() = callfemm("hideconsole()")
showconsole() = callfemm("showconsole()")
messagebox(msg) = callfemm("messagebox($(quote_str(msg)))")

# In OF these toggle the floating "FEMM Properties" window that always
# hangs over the preprocessor. femm-qt has no such persistent floating
# window, so there is nothing to toggle -- kept as no-ops for
# legacy-script compatibility.
# Dropped in 0a5be31; restored for octavefemm and luafemm in c6fe935.
showpointprops() = nothing
hidepointprops() = nothing

# ===================================================================
#  MAGNETICS PRE-PROCESSOR (mi_)
# ===================================================================

mi_addarc(x1,y1,x2,y2,angle,maxseg) = callfemm("mi_addarc($(numc(x1))$(numc(y1))$(numc(x2))$(numc(y2))$(numc(angle))$(num(maxseg)))")
mi_addbhpoint(name,b,h) = callfemm("mi_addbhpoint($(quotec(name))$(numc(b))$(num(h)))")

function mi_addbhpoints(name, bh::AbstractMatrix)
    for k in 1:size(bh,1)
        mi_addbhpoint(name, bh[k,1], bh[k,2])
    end
end

mi_addblocklabel(x,y) = callfemm("mi_addblocklabel($(numc(x))$(num(y)))")

function mi_addboundprop(pname="", a0=0, a1=0, a2=0, phi=0, mu=0, sig=0, c0=0, c1=0, fmt=0, ia=0, oa=0; _nargs::Int=0)
    # This uses varargs-style: call with only the args you need
    _mi_addboundprop_va(pname, a0, a1, a2, phi, mu, sig, c0, c1, fmt, ia, oa)
end

function _mi_addboundprop_va(args...)
    # Count non-default trailing args -- just pass them all via varargs wrapper
    n = length(args)
    if n == 0;  return callfemm("mi_addboundprop()"); end
    if n == 1;  return callfemm("mi_addboundprop($(quote_str(args[1])))"); end
    parts = [quotec(args[1])]
    for i in 2:n-1
        push!(parts, numc(args[i]))
    end
    push!(parts, num(args[n]))
    return callfemm("mi_addboundprop(" * join(parts) * ")")
end
# Convenience: call with specific arg count
mi_addboundprop(pname, a0, a1, a2, phi, mu, sig, c0, c1, fmt, ia, oa) =
    _mi_addboundprop_va(pname, a0, a1, a2, phi, mu, sig, c0, c1, fmt, ia, oa)
mi_addboundprop(pname, a0, a1, a2, phi, mu, sig, c0, c1, fmt, ia) =
    _mi_addboundprop_va(pname, a0, a1, a2, phi, mu, sig, c0, c1, fmt, ia)
mi_addboundprop(pname, a0, a1, a2, phi, mu, sig, c0, c1, fmt) =
    _mi_addboundprop_va(pname, a0, a1, a2, phi, mu, sig, c0, c1, fmt)
mi_addboundprop(pname, a0, a1, a2, phi, mu, sig, c0, c1) =
    _mi_addboundprop_va(pname, a0, a1, a2, phi, mu, sig, c0, c1)
mi_addboundprop(pname, a0, a1, a2, phi, mu, sig, c0) =
    _mi_addboundprop_va(pname, a0, a1, a2, phi, mu, sig, c0)
mi_addboundprop(pname, a0, a1, a2, phi, mu, sig) =
    _mi_addboundprop_va(pname, a0, a1, a2, phi, mu, sig)
mi_addboundprop(pname, a0, a1, a2, phi, mu) =
    _mi_addboundprop_va(pname, a0, a1, a2, phi, mu)
mi_addboundprop(pname, a0, a1, a2, phi) =
    _mi_addboundprop_va(pname, a0, a1, a2, phi)
mi_addboundprop(pname, a0, a1, a2) =
    _mi_addboundprop_va(pname, a0, a1, a2)
mi_addboundprop(pname, a0, a1) =
    _mi_addboundprop_va(pname, a0, a1)
mi_addboundprop(pname, a0) =
    _mi_addboundprop_va(pname, a0)
mi_addboundprop(pname::AbstractString) =
    _mi_addboundprop_va(pname)

mi_addcircprop(pname,ic,ptype) = callfemm("mi_addcircprop($(quotec(pname))$(numc(ic))$(num(ptype)))")

# mi_addmaterial -- up to 14 args
function _mi_addmaterial_va(args...)
    n = length(args)
    if n == 0; return callfemm("mi_addmaterial($(quote_str("Air")))"); end
    if n == 1; return callfemm("mi_addmaterial($(quote_str(args[1])))"); end
    parts = [quotec(args[1])]
    for i in 2:n-1
        push!(parts, numc(args[i]))
    end
    push!(parts, num(args[n]))
    return callfemm("mi_addmaterial(" * join(parts) * ")")
end
mi_addmaterial() = _mi_addmaterial_va()
mi_addmaterial(name) = _mi_addmaterial_va(name)
mi_addmaterial(name,mux) = _mi_addmaterial_va(name,mux)
mi_addmaterial(name,mux,muy) = _mi_addmaterial_va(name,mux,muy)
mi_addmaterial(name,mux,muy,hc) = _mi_addmaterial_va(name,mux,muy,hc)
mi_addmaterial(name,mux,muy,hc,js) = _mi_addmaterial_va(name,mux,muy,hc,js)
mi_addmaterial(name,mux,muy,hc,js,c) = _mi_addmaterial_va(name,mux,muy,hc,js,c)
mi_addmaterial(name,mux,muy,hc,js,c,lamd) = _mi_addmaterial_va(name,mux,muy,hc,js,c,lamd)
mi_addmaterial(name,mux,muy,hc,js,c,lamd,phih) = _mi_addmaterial_va(name,mux,muy,hc,js,c,lamd,phih)
mi_addmaterial(name,mux,muy,hc,js,c,lamd,phih,lamfill) = _mi_addmaterial_va(name,mux,muy,hc,js,c,lamd,phih,lamfill)
mi_addmaterial(name,mux,muy,hc,js,c,lamd,phih,lamfill,lamtype) = _mi_addmaterial_va(name,mux,muy,hc,js,c,lamd,phih,lamfill,lamtype)
mi_addmaterial(name,mux,muy,hc,js,c,lamd,phih,lamfill,lamtype,phihx) = _mi_addmaterial_va(name,mux,muy,hc,js,c,lamd,phih,lamfill,lamtype,phihx)
mi_addmaterial(name,mux,muy,hc,js,c,lamd,phih,lamfill,lamtype,phihx,phihy) = _mi_addmaterial_va(name,mux,muy,hc,js,c,lamd,phih,lamfill,lamtype,phihx,phihy)
mi_addmaterial(name,mux,muy,hc,js,c,lamd,phih,lamfill,lamtype,phihx,phihy,nstr) = _mi_addmaterial_va(name,mux,muy,hc,js,c,lamd,phih,lamfill,lamtype,phihx,phihy,nstr)
mi_addmaterial(name,mux,muy,hc,js,c,lamd,phih,lamfill,lamtype,phihx,phihy,nstr,dwire) = _mi_addmaterial_va(name,mux,muy,hc,js,c,lamd,phih,lamfill,lamtype,phihx,phihy,nstr,dwire)

mi_addnode(x,y) = callfemm("mi_addnode($(numc(x))$(num(y)))")
mi_addpointprop(pname,ap,jp) = callfemm("mi_addpointprop($(quotec(pname))$(numc(ap))$(num(jp)))")
mi_addsegment(x1,y1,x2,y2) = callfemm("mi_addsegment($(numc(x1))$(numc(y1))$(numc(x2))$(num(y2)))")

mi_analyze() = callfemm("mi_analyze()")
mi_analyze(n) = callfemm("mi_analyze($(num(n)))")
mi_analyse() = mi_analyze()
mi_analyse(n) = mi_analyze(n)

mi_attachouterspace() = callfemm("mi_attachouterspace()")
mi_attachdefault() = callfemm("mi_attachdefault()")
mi_clearbhpoints(n) = callfemm("mi_clearbhpoints($(quote_str(n)))")
mi_clearselected() = callfemm("mi_clearselected()")
mi_close() = callfemm("mi_close()")

mi_copyrotate(bx,by,angle,copies) = callfemm("mi_copyrotate($(numc(bx))$(numc(by))$(numc(angle))$(num(copies)))")
mi_copyrotate2(bx,by,angle,copies,editaction) = callfemm("mi_copyrotate($(numc(bx))$(numc(by))$(numc(angle))$(numc(copies))$(num(editaction)))")
mi_copytranslate(bx,by,copies) = callfemm("mi_copytranslate($(numc(bx))$(numc(by))$(num(copies)))")
mi_copytranslate2(bx,by,copies,editaction) = callfemm("mi_copytranslate($(numc(bx))$(numc(by))$(numc(copies))$(num(editaction)))")

mi_createmesh() = callfemm("mi_createmesh()")
mi_createradius(x,y,r) = callfemm("mi_createradius($(numc(x))$(numc(y))$(num(r)))")
mi_defineouterspace(Zo,Ro,Ri) = callfemm("mi_defineouterspace($(numc(Zo))$(numc(Ro))$(num(Ri)))")
mi_deleteboundprop(n) = callfemm("mi_deleteboundprop($(quote_str(n)))")
mi_deletecircuit(n) = callfemm("mi_deletecircuit($(quote_str(n)))")
mi_deletematerial(n) = callfemm("mi_deletematerial($(quote_str(n)))")
mi_deletepointprop(n) = callfemm("mi_deletepointprop($(quote_str(n)))")
mi_deleteselected() = callfemm("mi_deleteselected()")
mi_deleteselectedarcsegments() = callfemm("mi_deleteselectedarcsegments()")
mi_deleteselectedlabels() = callfemm("mi_deleteselectedlabels()")
mi_deleteselectednodes() = callfemm("mi_deleteselectednodes()")
mi_deleteselectedsegments() = callfemm("mi_deleteselectedsegments()")
mi_detachouterspace() = callfemm("mi_detachouterspace()")
mi_detachdefault() = callfemm("mi_detachdefault()")

function mi_drawarc(x1,y1,x2,y2,angle,maxseg)
    mi_addnode(x1,y1); mi_addnode(x2,y2)
    mi_addarc(x1,y1,x2,y2,angle,maxseg)
end
function mi_drawline(x1,y1,x2,y2)
    mi_addnode(x1,y1); mi_addnode(x2,y2)
    mi_addsegment(x1,y1,x2,y2)
end
function mi_drawpolyline(p::AbstractMatrix)
    for k in 1:size(p,1)-1
        mi_drawline(p[k,1],p[k,2],p[k+1,1],p[k+1,2])
    end
end
function mi_drawpolygon(p::AbstractMatrix)
    mi_drawpolyline(p)
    n = size(p,1)
    mi_drawline(p[1,1],p[1,2],p[n,1],p[n,2])
end
function mi_drawrectangle(x1,y1,x2,y2)
    mi_drawpolygon([x1 y1; x1 y2; x2 y2; x2 y1])
end

mi_getmaterial(matname) = callfemm("mi_getmaterial($(quote_str(matname)))")
mi_hidegrid() = callfemm("mi_hidegrid()")
mi_hidenames() = callfemm("mi_shownames(0)")
mi_loadsolution() = callfemm("mi_loadsolution()")
mi_maximize() = callfemm("mi_maximize()")
mi_minimize() = callfemm("mi_minimize()")

mi_mirror(x1,y1,x2,y2) = callfemm("mi_mirror($(numc(x1))$(numc(y1))$(numc(x2))$(num(y2)))")
mi_mirror2(x1,y1,x2,y2,editaction) = callfemm("mi_mirror($(numc(x1))$(numc(y1))$(numc(x2))$(numc(y2))$(num(editaction)))")

function mi_modifyboundprop(pname,pnum,pval)
    if pnum == 0
        callfemm("mi_modifyboundprop($(quotec(pname))$(numc(pnum))$(quote_str(pval)))")
    else
        callfemm("mi_modifyboundprop($(quotec(pname))$(numc(pnum))$(num(pval)))")
    end
end
function mi_modifycircprop(pname,pnum,pval)
    if pnum == 0
        callfemm("mi_modifycircprop($(quotec(pname))$(numc(pnum))$(quote_str(pval)))")
    else
        callfemm("mi_modifycircprop($(quotec(pname))$(numc(pnum))$(num(pval)))")
    end
end
function mi_modifymaterial(pname,pnum,pval)
    if pnum == 0
        callfemm("mi_modifymaterial($(quotec(pname))$(numc(pnum))$(quote_str(pval)))")
    else
        callfemm("mi_modifymaterial($(quotec(pname))$(numc(pnum))$(num(pval)))")
    end
end
function mi_modifypointprop(pname,pnum,pval)
    if pnum == 0
        callfemm("mi_modifypointprop($(quotec(pname))$(numc(pnum))$(quote_str(pval)))")
    else
        callfemm("mi_modifypointprop($(quotec(pname))$(numc(pnum))$(num(pval)))")
    end
end

mi_moverotate(bx,by,shiftangle) = callfemm("mi_moverotate($(numc(bx))$(numc(by))$(num(shiftangle)))")
mi_moverotate2(bx,by,shiftangle,editaction) = callfemm("mi_moverotate($(numc(bx))$(numc(by))$(numc(shiftangle))$(num(editaction)))")
mi_movetranslate(bx,by) = callfemm("mi_movetranslate($(numc(bx))$(num(by)))")
mi_movetranslate2(bx,by,editaction) = callfemm("mi_movetranslate($(numc(bx))$(numc(by))$(num(editaction)))")

# mi_probdef -- variable args
function _mi_probdef_va(args...)
    n = length(args)
    if n == 0; return callfemm("mi_probdef()"); end
    # args: freq, units, ptype, precision, depth, minangle, acsolver
    # units and ptype are strings, rest are numbers
    if n == 1; return callfemm("mi_probdef($(num(args[1])))"); end
    if n == 2; return callfemm("mi_probdef($(numc(args[1]))$(quote_str(args[2])))"); end
    if n == 3; return callfemm("mi_probdef($(numc(args[1]))$(quotec(args[2]))$(quote_str(args[3])))"); end
    # From here, args 2,3 are strings, rest are numbers
    parts = "mi_probdef($(numc(args[1]))$(quotec(args[2]))$(quotec(args[3]))"
    for i in 4:n-1
        parts *= numc(args[i])
    end
    parts *= num(args[n]) * ")"
    return callfemm(parts)
end
mi_probdef(freq) = _mi_probdef_va(freq)
mi_probdef(freq,units) = _mi_probdef_va(freq,units)
mi_probdef(freq,units,ptype) = _mi_probdef_va(freq,units,ptype)
mi_probdef(freq,units,ptype,precision) = _mi_probdef_va(freq,units,ptype,precision)
mi_probdef(freq,units,ptype,precision,depth) = _mi_probdef_va(freq,units,ptype,precision,depth)
mi_probdef(freq,units,ptype,precision,depth,minangle) = _mi_probdef_va(freq,units,ptype,precision,depth,minangle)
mi_probdef(freq,units,ptype,precision,depth,minangle,acsolver) = _mi_probdef_va(freq,units,ptype,precision,depth,minangle,acsolver)

mi_purgemesh() = callfemm("mi_purgemesh()")
mi_readdxf(docname) = callfemm("mi_readdxf($(quote_str(docname)))")
mi_refreshview() = callfemm("mi_refreshview()")
mi_resize(w,h) = callfemm("mi_resize($(numc(w))$(num(h)))")
mi_restore() = callfemm("mi_restore()")
mi_saveas(fn) = callfemm("mi_saveas($(quote_str(fn)))")
mi_savebitmap(n) = callfemm("mi_savebitmap($(quote_str(n)))")
mi_savedxf(docname) = callfemm("mi_savedxf($(quote_str(docname)))")
mi_savemetafile(n) = callfemm("mi_savemetafile($(quote_str(n)))")

mi_scale(bx,by,sc) = callfemm("mi_scale($(numc(bx))$(numc(by))$(num(sc)))")
mi_scale2(bx,by,sc,ea) = callfemm("mi_scale($(numc(bx))$(numc(by))$(numc(sc))$(num(ea)))")

mi_selectarcsegment(x,y) = callfemm("mi_selectarcsegment($(numc(x))$(num(y)))")
mi_selectgroup(gr) = callfemm("mi_selectgroup($(num(gr)))")
mi_selectlabel(x,y) = callfemm("mi_selectlabel($(numc(x))$(num(y)))")
mi_selectnode(x,y) = callfemm("mi_selectnode($(numc(x))$(num(y)))")
mi_selectsegment(x,y) = callfemm("mi_selectsegment($(numc(x))$(num(y)))")

mi_selectcircle(x,y,R) = callfemm("mi_selectcircle($(numc(x))$(numc(y))$(num(R)))")
mi_selectcircle(x,y,R,editmode) = callfemm("mi_selectcircle($(numc(x))$(numc(y))$(numc(R))$(num(editmode)))")
mi_selectrectangle(x1,y1,x2,y2) = callfemm("mi_selectrectangle($(numc(x1))$(numc(y1))$(numc(x2))$(num(y2)))")
mi_selectrectangle(x1,y1,x2,y2,editmode) = callfemm("mi_selectrectangle($(numc(x1))$(numc(y1))$(numc(x2))$(numc(y2))$(num(editmode)))")

mi_setarcsegmentprop(maxsegdeg,propname,hide,group) = callfemm("mi_setarcsegmentprop($(numc(maxsegdeg))$(quotec(propname))$(numc(hide))$(num(group)))")

function mi_setblockprop(blockname,automesh,meshsize,incirc,magdir,group,turns)
    if isa(magdir, Number)
        callfemm("mi_setblockprop($(quotec(blockname))$(numc(automesh))$(numc(meshsize))$(quotec(incirc))$(numc(magdir))$(numc(group))$(num(turns)))")
    else
        callfemm("mi_setblockprop($(quotec(blockname))$(numc(automesh))$(numc(meshsize))$(quotec(incirc))$(quotec(magdir))$(numc(group))$(num(turns)))")
    end
end

mi_setcurrent(name,x) = mi_modifycircprop(name, 1, x)
mi_seteditmode(editmode) = callfemm("mi_seteditmode($(quote_str(editmode)))")
mi_setfocus(docname) = callfemm("mi_setfocus($(quote_str(docname)))")
mi_setgrid(density,ptype) = callfemm("mi_setgrid($(numc(density))$(quote_str(ptype)))")
mi_setgroup(n) = callfemm("mi_setgroup($(num(n)))")
mi_setnodeprop(nodeprop,groupno) = callfemm("mi_setnodeprop($(quotec(nodeprop))$(num(groupno)))")
mi_setsegmentprop(pn,es,am,hi,gr) = callfemm("mi_setsegmentprop($(quotec(pn))$(numc(es))$(numc(am))$(numc(hi))$(num(gr)))")

mi_showgrid() = callfemm("mi_showgrid()")
mi_showmesh() = callfemm("mi_showmesh()")
mi_shownames() = callfemm("mi_shownames(1)")
mi_snapgridoff() = callfemm("mi_gridsnap(\"off\")")
mi_snapgridon() = callfemm("mi_gridsnap(\"on\")")

mi_zoom(x1,y1,x2,y2) = callfemm("mi_zoom($(numc(x1))$(numc(y1))$(numc(x2))$(num(y2)))")
mi_zoomin() = callfemm("mi_zoomin()")
mi_zoomnatural() = callfemm("mi_zoomnatural()")
mi_zoomout() = callfemm("mi_zoomout()")

mi_smartmesh(n) = callfemm("mi_smartmesh($(num(n)))")
mi_setcomment(c) = callfemm("mi_setcomment($(quote_str(c)))")
mi_getboundingbox() = callfemm("mi_getboundingbox()")
mi_getprobleminfo() = callfemm("mi_getprobleminfo()")
mi_gettitle() = callfemm("mi_gettitle()")
mi_gridsnap(flag) = callfemm("mi_gridsnap($(quote_str(flag)))")

function mi_setprevious(fn, pt=nothing)
    if pt === nothing
        callfemm("mi_setprevious($(quote_str(fn)))")
    else
        callfemm("mi_setprevious($(quotec(fn))$(num(pt)))")
    end
end

function mi_makeABC(args...)
    n = length(args)
    if n == 0; return callfemm("mi_makeABC()"); end
    if n == 1; return callfemm("mi_makeABC($(num(args[1])))"); end
    parts = String[]
    for i in 1:n-1; push!(parts, numc(args[i])); end
    push!(parts, num(args[n]))
    callfemm("mi_makeABC(" * join(parts) * ")")
end

# ===================================================================
#  MAGNETICS POST-PROCESSOR (mo_)
# ===================================================================

mo_addcontour(x,y) = callfemm("mo_addcontour($(numc(x))$(num(y)))")
mo_bendcontour(tta,dtta) = callfemm("mo_bendcontour($(numc(tta))$(num(dtta)))")
mo_blockintegral(ptype) = callfemm("mo_blockintegral($(num(ptype)))")
mo_clearblock() = callfemm("mo_clearblock()")
mo_clearcontour() = callfemm("mo_clearcontour()")
mo_close() = callfemm("mo_close()")

function mo_getpointvalues(x,y)
    callfemm("mo_getpointvalues($(numc(x))$(num(y)))")
end
function mo_getpointvalues(x::AbstractVector, y::AbstractVector)
    n = length(x)
    length(y) == n || error("must be same number of entries in X and Y arrays")
    z = zeros(n, 14)
    for k in 1:n
        z[k,:] .= collect(callfemm("mo_getpointvalues($(numc(x[k]))$(num(y[k])))"))
    end
    z
end

mo_geta(x,y) = let v = mo_getpointvalues(x,y); isa(v, Tuple) ? v[1] : v; end
function mo_getb(x,y)
    v = mo_getpointvalues(x,y)
    isa(v, Tuple) ? (v[2], v[3]) : v
end
mo_getcircuitproperties(name) = callfemm("mo_getcircuitproperties($(quote_str(name)))")
mo_getconductivity(x,y) = let v = mo_getpointvalues(x,y); isa(v, Tuple) ? v[4] : v; end
mo_getelement(n) = callfemm("mo_getelement($(num(n)))")
mo_getenergydensity(x,y) = let v = mo_getpointvalues(x,y); isa(v, Tuple) ? v[5] : v; end
function mo_geth(x,y)
    v = mo_getpointvalues(x,y)
    isa(v, Tuple) ? (v[6], v[7]) : v
end
function mo_getj(x,y)
    v = mo_getpointvalues(x,y)
    isa(v, Tuple) ? v[8] + v[9] : v
end
function mo_getmu(x,y)
    v = mo_getpointvalues(x,y)
    isa(v, Tuple) ? (v[10], v[11]) : v
end
mo_getnode(n) = callfemm("mo_getnode($(num(n)))")
mo_getpe(x,y) = let v = mo_getpointvalues(x,y); isa(v, Tuple) ? v[12] : v; end
mo_getph(x,y) = let v = mo_getpointvalues(x,y); isa(v, Tuple) ? v[13] : v; end
mo_getfill(x,y) = let v = mo_getpointvalues(x,y); isa(v, Tuple) ? v[14] : v; end
mo_getprobleminfo() = callfemm("mo_getprobleminfo()")

mo_groupselectblock() = callfemm("mo_groupselectblock()")
mo_groupselectblock(n) = callfemm("mo_groupselectblock($(num(n)))")

mo_hidecontourplot() = callfemm("mo_hidecontourplot()")
mo_hidedensityplot() = callfemm("mo_hidedensityplot()")
mo_hidegrid() = callfemm("mo_hidegrid()")
mo_hidemesh() = callfemm("mo_hidemesh()")
mo_hidenames() = callfemm("mo_shownames(0)")
mo_hidepoints() = callfemm("mo_hidepoints()")

mo_lineintegral(ptype) = callfemm("mo_lineintegral($(num(ptype)))")

mo_makeplot(plottype,numpoints) = callfemm("mo_makeplot($(numc(plottype))$(num(numpoints)))")
mo_makeplot(plottype,numpoints,filename) = callfemm("mo_makeplot($(numc(plottype))$(numc(numpoints))$(quote_str(filename)))")
mo_makeplot(plottype,numpoints,filename,fileformat) = callfemm("mo_makeplot($(numc(plottype))$(numc(numpoints))$(quotec(filename))$(num(fileformat)))")

mo_maximize() = callfemm("mo_maximize()")
mo_minimize() = callfemm("mo_minimize()")
mo_numelements() = callfemm("mo_numelements()")
mo_numnodes() = callfemm("mo_numnodes()")
mo_refreshview() = callfemm("mo_refreshview()")
mo_reload() = callfemm("mo_reload()")
mo_resize(w,h) = callfemm("mo_resize($(numc(w))$(num(h)))")
mo_restore() = callfemm("mo_restore()")
mo_savebitmap(fn) = callfemm("mo_savebitmap($(quote_str(fn)))")
mo_savemetafile(fn) = callfemm("mo_savemetafile($(quote_str(fn)))")

mo_selectblock(x,y) = callfemm("mo_selectblock($(numc(x))$(num(y)))")
mo_selectpoint(x,y) = callfemm("mo_selectpoint($(numc(x))$(num(y)))")
mo_seteditmode(mode) = callfemm("mo_seteditmode($(quote_str(mode)))")
mo_setweightingscheme(mode) = callfemm("mo_setweightingscheme($(num(mode)))")
mo_setgrid(density,ptype) = callfemm("mo_setgrid($(numc(density))$(quote_str(ptype)))")

mo_showcontourplot(numcontours,al,au,ptype) = callfemm("mo_showcontourplot($(numc(numcontours))$(numc(al))$(numc(au))$(quote_str(ptype)))")
mo_showdensityplot(legend,gscale,bu,bl,ptype) = callfemm("mo_showdensityplot($(numc(legend))$(numc(gscale))$(numc(bu))$(numc(bl))$(quote_str(ptype)))")
mo_showgrid() = callfemm("mo_showgrid()")
mo_showmesh() = callfemm("mo_showmesh()")
mo_shownames() = callfemm("mo_shownames(1)")
mo_showpoints() = callfemm("mo_showpoints()")

mo_showvectorplot(plottype) = callfemm("mo_showvectorplot($(num(plottype)))")
mo_showvectorplot(plottype,scalefactor) = callfemm("mo_showvectorplot($(numc(plottype))$(num(scalefactor)))")

mo_smooth(flag) = callfemm("mo_smooth($(quote_str(flag)))")
mo_smoothoff() = callfemm("mo_smooth(\"off\")")
mo_smoothon() = callfemm("mo_smooth(\"on\")")
mo_snapgrid(flag) = callfemm("mo_gridsnap($(quote_str(flag)))")
mo_snapgridoff() = callfemm("mo_gridsnap(\"off\")")
mo_snapgridon() = callfemm("mo_gridsnap(\"on\")")
mo_gridsnap(flag) = callfemm("mo_gridsnap($(quote_str(flag)))")

mo_zoom(x1,y1,x2,y2) = callfemm("mo_zoom($(numc(x1))$(numc(y1))$(numc(x2))$(num(y2)))")
mo_zoomin() = callfemm("mo_zoomin()")
mo_zoomnatural() = callfemm("mo_zoomnatural()")
mo_zoomout() = callfemm("mo_zoomout()")

# Gap functions
mo_getgapb(bdryname,angle) = callfemm("mo_getgapb($(quotec(bdryname))$(num(angle)))")
mo_getgapa(bdryname,angle) = callfemm("mo_getgapa($(quotec(bdryname))$(num(angle)))")
mo_gapintegral(bdryname,inttype) = callfemm("mo_gapintegral($(quotec(bdryname))$(num(inttype)))")
mo_getgapharmonics(bdryname) = callfemm("mo_getgapharmonics($(quote_str(bdryname)))")
mo_getgapharmonics(bdryname,n) = callfemm("mo_getgapharmonics($(quotec(bdryname))$(num(n)))")

# Bulk data retrieval
function mo_getallelements()
    raw = collect(callfemm("mo_getallelements()"))
    return permutedims(reshape(raw, 7, :))
end
function mo_getallnodes()
    raw = collect(callfemm("mo_getallnodes()"))
    return permutedims(reshape(raw, 2, :))
end
function mo_getallb()
    raw = collect(callfemm("mo_getallb()"))
    return permutedims(reshape(raw, 2, :))
end
function mo_getalla()
    raw = callfemm("mo_getalla()")
    return collect(isa(raw, Tuple) ? raw : (raw,))
end
function eo_getallelements()
    raw = collect(callfemm("eo_getallelements()"))
    return permutedims(reshape(raw, 7, :))
end
function eo_getallnodes()
    raw = collect(callfemm("eo_getallnodes()"))
    return permutedims(reshape(raw, 2, :))
end
function eo_getalld()
    raw = collect(callfemm("eo_getalld()"))
    return permutedims(reshape(raw, 2, :))
end
function eo_getallv()
    raw = callfemm("eo_getallv()")
    return collect(isa(raw, Tuple) ? raw : (raw,))
end
function ho_getallnodes()
    raw = collect(callfemm("ho_getallnodes()"))
    return permutedims(reshape(raw, 2, :))
end
function ho_getallelements()
    raw = collect(callfemm("ho_getallelements()"))
    return permutedims(reshape(raw, 7, :))
end
function ho_getallf()
    raw = collect(callfemm("ho_getallf()"))
    return permutedims(reshape(raw, 2, :))
end
function ho_getallt()
    raw = callfemm("ho_getallt()")
    return collect(isa(raw, Tuple) ? raw : (raw,))
end
function co_getallnodes()
    raw = collect(callfemm("co_getallnodes()"))
    return permutedims(reshape(raw, 2, :))
end
function co_getallelements()
    raw = collect(callfemm("co_getallelements()"))
    return permutedims(reshape(raw, 7, :))
end
function co_getallj()
    raw = collect(callfemm("co_getallj()"))
    return permutedims(reshape(raw, 2, :))
end
function co_getallv()
    raw = callfemm("co_getallv()")
    return collect(isa(raw, Tuple) ? raw : (raw,))
end

function mo_getallgapb(bdryname, npts=360)
    raw = collect(callfemm("mo_getallgapb($(quotec(bdryname))$(num(npts)))"))
    return permutedims(reshape(raw, 2, :))
end
function mo_getallgapa(bdryname, npts=360)
    raw = callfemm("mo_getallgapa($(quotec(bdryname))$(num(npts)))")
    return collect(isa(raw, Tuple) ? raw : (raw,))
end
function mo_getallgapharmonics(bdryname)
    raw = collect(callfemm("mo_getallgapharmonics($(quote_str(bdryname)))"))
    return permutedims(reshape(raw, 6, :))
end

mo_gettitle() = callfemm("mo_gettitle()")

# ===================================================================
#  ELECTROSTATICS PRE-PROCESSOR (ei_)
# ===================================================================

ei_addarc(x1,y1,x2,y2,angle,maxseg) = callfemm("ei_addarc($(numc(x1))$(numc(y1))$(numc(x2))$(numc(y2))$(numc(angle))$(num(maxseg)))")
ei_addblocklabel(x,y) = callfemm("ei_addblocklabel($(numc(x))$(num(y)))")
ei_addboundprop(pname,vs,qs,c0,c1,fmt) = callfemm("ei_addboundprop($(quotec(pname))$(numc(vs))$(numc(qs))$(numc(c0))$(numc(c1))$(num(fmt)))")
ei_addconductorprop(pname,vc,qc,ptype) = callfemm("ei_addconductorprop($(quotec(pname))$(numc(vc))$(numc(qc))$(num(ptype)))")
ei_addmaterial(pname,ex,ey,qv) = callfemm("ei_addmaterial($(quotec(pname))$(numc(ex))$(numc(ey))$(num(qv)))")
ei_addnode(x,y) = callfemm("ei_addnode($(numc(x))$(num(y)))")
ei_addpointprop(pname,vp,qp) = callfemm("ei_addpointprop($(quotec(pname))$(numc(vp))$(num(qp)))")
ei_addsegment(x1,y1,x2,y2) = callfemm("ei_addsegment($(numc(x1))$(numc(y1))$(numc(x2))$(num(y2)))")

ei_analyze() = callfemm("ei_analyze()")
ei_analyze(n) = callfemm("ei_analyze($(num(n)))")
ei_analyse() = ei_analyze()
ei_analyse(n) = ei_analyze(n)

ei_attachouterspace() = callfemm("ei_attachouterspace()")
ei_attachdefault() = callfemm("ei_attachdefault()")
ei_clearselected() = callfemm("ei_clearselected()")
ei_close() = callfemm("ei_close()")

ei_copyrotate(bx,by,angle,copies) = callfemm("ei_copyrotate($(numc(bx))$(numc(by))$(numc(angle))$(num(copies)))")
ei_copyrotate2(bx,by,angle,copies,editaction) = callfemm("ei_copyrotate($(numc(bx))$(numc(by))$(numc(angle))$(numc(copies))$(num(editaction)))")
ei_copytranslate(bx,by,copies) = callfemm("ei_copytranslate($(numc(bx))$(numc(by))$(num(copies)))")
ei_copytranslate2(bx,by,copies,editaction) = callfemm("ei_copytranslate($(numc(bx))$(numc(by))$(numc(copies))$(num(editaction)))")

ei_createmesh() = callfemm("ei_createmesh()")
ei_createradius(x,y,r) = callfemm("ei_createradius($(numc(x))$(numc(y))$(num(r)))")
ei_defineouterspace(Zo,Ro,Ri) = callfemm("ei_defineouterspace($(numc(Zo))$(numc(Ro))$(num(Ri)))")
ei_deleteboundprop(n) = callfemm("ei_deleteboundprop($(quote_str(n)))")
ei_deleteconductor(n) = callfemm("ei_deleteconductor($(quote_str(n)))")
ei_deletematerial(n) = callfemm("ei_deletematerial($(quote_str(n)))")
ei_deletepointprop(n) = callfemm("ei_deletepointprop($(quote_str(n)))")
ei_deleteselected() = callfemm("ei_deleteselected()")
ei_deleteselectedarcsegments() = callfemm("ei_deleteselectedarcsegments()")
ei_deleteselectedlabels() = callfemm("ei_deleteselectedlabels()")
ei_deleteselectednodes() = callfemm("ei_deleteselectednodes()")
ei_deleteselectedsegments() = callfemm("ei_deleteselectedsegments()")
ei_detachouterspace() = callfemm("ei_detachouterspace()")
ei_detachdefault() = callfemm("ei_detachdefault()")

function ei_drawarc(x1,y1,x2,y2,angle,maxseg)
    ei_addnode(x1,y1); ei_addnode(x2,y2)
    ei_addarc(x1,y1,x2,y2,angle,maxseg)
end
function ei_drawline(x1,y1,x2,y2)
    ei_addnode(x1,y1); ei_addnode(x2,y2)
    ei_addsegment(x1,y1,x2,y2)
end
function ei_drawpolyline(p::AbstractMatrix)
    for k in 1:size(p,1)-1
        ei_drawline(p[k,1],p[k,2],p[k+1,1],p[k+1,2])
    end
end
function ei_drawpolygon(p::AbstractMatrix)
    ei_drawpolyline(p)
    n = size(p,1)
    ei_drawline(p[1,1],p[1,2],p[n,1],p[n,2])
end
function ei_drawrectangle(x1,y1,x2,y2)
    ei_drawpolygon([x1 y1; x1 y2; x2 y2; x2 y1])
end

ei_getmaterial(matname) = callfemm("ei_getmaterial($(quote_str(matname)))")
ei_hidegrid() = callfemm("ei_hidegrid()")
ei_hidenames() = callfemm("ei_shownames(0)")
ei_loadsolution() = callfemm("ei_loadsolution()")
ei_maximize() = callfemm("ei_maximize()")
ei_minimize() = callfemm("ei_minimize()")

ei_mirror(x1,y1,x2,y2) = callfemm("ei_mirror($(numc(x1))$(numc(y1))$(numc(x2))$(num(y2)))")
ei_mirror2(x1,y1,x2,y2,editaction) = callfemm("ei_mirror($(numc(x1))$(numc(y1))$(numc(x2))$(numc(y2))$(num(editaction)))")

function ei_modifyboundprop(pname,pnum,pval)
    pnum == 0 ? callfemm("ei_modifyboundprop($(quotec(pname))$(numc(pnum))$(quote_str(pval)))") :
                callfemm("ei_modifyboundprop($(quotec(pname))$(numc(pnum))$(num(pval)))")
end
function ei_modifyconductorprop(pname,pnum,pval)
    pnum == 0 ? callfemm("ei_modifyconductorprop($(quotec(pname))$(numc(pnum))$(quote_str(pval)))") :
                callfemm("ei_modifyconductorprop($(quotec(pname))$(numc(pnum))$(num(pval)))")
end
function ei_modifymaterial(pname,pnum,pval)
    pnum == 0 ? callfemm("ei_modifymaterial($(quotec(pname))$(numc(pnum))$(quote_str(pval)))") :
                callfemm("ei_modifymaterial($(quotec(pname))$(numc(pnum))$(num(pval)))")
end
function ei_modifypointprop(pname,pnum,pval)
    pnum == 0 ? callfemm("ei_modifypointprop($(quotec(pname))$(numc(pnum))$(quote_str(pval)))") :
                callfemm("ei_modifypointprop($(quotec(pname))$(numc(pnum))$(num(pval)))")
end

ei_moverotate(bx,by,shiftangle) = callfemm("ei_moverotate($(numc(bx))$(numc(by))$(num(shiftangle)))")
ei_moverotate2(bx,by,shiftangle,editaction) = callfemm("ei_moverotate($(numc(bx))$(numc(by))$(numc(shiftangle))$(num(editaction)))")
ei_movetranslate(bx,by) = callfemm("ei_movetranslate($(numc(bx))$(num(by)))")
ei_movetranslate2(bx,by,editaction) = callfemm("ei_movetranslate($(numc(bx))$(numc(by))$(num(editaction)))")

ei_probdef(units,ptype,precision,depth,minangle) = callfemm("ei_probdef($(quotec(units))$(quotec(ptype))$(numc(precision))$(numc(depth))$(num(minangle)))")

ei_purgemesh() = callfemm("ei_purgemesh()")
ei_readdxf(docname) = callfemm("ei_readdxf($(quote_str(docname)))")
ei_refreshview() = callfemm("ei_refreshview()")
ei_resize(w,h) = callfemm("ei_resize($(numc(w))$(num(h)))")
ei_restore() = callfemm("ei_restore()")
ei_saveas(fn) = callfemm("ei_saveas($(quote_str(fn)))")
ei_savebitmap(n) = callfemm("ei_savebitmap($(quote_str(n)))")
ei_savedxf(docname) = callfemm("ei_savedxf($(quote_str(docname)))")
ei_savemetafile(n) = callfemm("ei_savemetafile($(quote_str(n)))")

ei_scale(bx,by,sc) = callfemm("ei_scale($(numc(bx))$(numc(by))$(num(sc)))")
ei_scale2(bx,by,sc,ea) = callfemm("ei_scale($(numc(bx))$(numc(by))$(numc(sc))$(num(ea)))")

ei_selectarcsegment(x,y) = callfemm("ei_selectarcsegment($(numc(x))$(num(y)))")
ei_selectgroup(gr) = callfemm("ei_selectgroup($(num(gr)))")
ei_selectlabel(x,y) = callfemm("ei_selectlabel($(numc(x))$(num(y)))")
ei_selectnode(x,y) = callfemm("ei_selectnode($(numc(x))$(num(y)))")
ei_selectsegment(x,y) = callfemm("ei_selectsegment($(numc(x))$(num(y)))")

ei_selectcircle(x,y,R) = callfemm("ei_selectcircle($(numc(x))$(numc(y))$(num(R)))")
ei_selectcircle(x,y,R,editmode) = callfemm("ei_selectcircle($(numc(x))$(numc(y))$(numc(R))$(num(editmode)))")
ei_selectrectangle(x1,y1,x2,y2) = callfemm("ei_selectrectangle($(numc(x1))$(numc(y1))$(numc(x2))$(num(y2)))")
ei_selectrectangle(x1,y1,x2,y2,editmode) = callfemm("ei_selectrectangle($(numc(x1))$(numc(y1))$(numc(x2))$(numc(y2))$(num(editmode)))")

ei_setarcsegmentprop(maxsegdeg,propname,hide,group,incond) = callfemm("ei_setarcsegmentprop($(numc(maxsegdeg))$(quotec(propname))$(numc(hide))$(numc(group))$(quote_str(incond)))")
ei_setblockprop(blockname,automesh,meshsize,group) = callfemm("ei_setblockprop($(quotec(blockname))$(numc(automesh))$(numc(meshsize))$(num(group)))")
ei_seteditmode(editmode) = callfemm("ei_seteditmode($(quote_str(editmode)))")
ei_setfocus(docname) = callfemm("ei_setfocus($(quote_str(docname)))")
ei_setgrid(density,ptype) = callfemm("ei_setgrid($(numc(density))$(quote_str(ptype)))")
ei_setgroup(n) = callfemm("ei_setgroup($(num(n)))")
ei_setnodeprop(nodeprop,groupno,inconductor) = callfemm("ei_setnodeprop($(quotec(nodeprop))$(numc(groupno))$(quote_str(inconductor)))")
ei_setsegmentprop(pn,es,am,hi,gr,inconductor) = callfemm("ei_setsegmentprop($(quotec(pn))$(numc(es))$(numc(am))$(numc(hi))$(numc(gr))$(quote_str(inconductor)))")

ei_showgrid() = callfemm("ei_showgrid()")
ei_showmesh() = callfemm("ei_showmesh()")
ei_shownames() = callfemm("ei_shownames(1)")
ei_snapgridoff() = callfemm("ei_gridsnap(\"off\")")
ei_snapgridon() = callfemm("ei_gridsnap(\"on\")")

ei_zoom(x1,y1,x2,y2) = callfemm("ei_zoom($(numc(x1))$(numc(y1))$(numc(x2))$(num(y2)))")
ei_zoomin() = callfemm("ei_zoomin()")
ei_zoomnatural() = callfemm("ei_zoomnatural()")
ei_zoomout() = callfemm("ei_zoomout()")

ei_smartmesh(n) = callfemm("ei_smartmesh($(num(n)))")
ei_setcomment(c) = callfemm("ei_setcomment($(quote_str(c)))")
ei_getboundingbox() = callfemm("ei_getboundingbox()")
ei_getprobleminfo() = callfemm("ei_getprobleminfo()")
ei_gettitle() = callfemm("ei_gettitle()")
ei_gridsnap(flag) = callfemm("ei_gridsnap($(quote_str(flag)))")

function ei_makeABC(args...)
    n = length(args)
    if n == 0; return callfemm("ei_makeABC()"); end
    if n == 1; return callfemm("ei_makeABC($(num(args[1])))"); end
    parts = String[]
    for i in 1:n-1; push!(parts, numc(args[i])); end
    push!(parts, num(args[n]))
    callfemm("ei_makeABC(" * join(parts) * ")")
end

# ===================================================================
#  ELECTROSTATICS POST-PROCESSOR (eo_)
# ===================================================================

eo_addcontour(x,y) = callfemm("eo_addcontour($(numc(x))$(num(y)))")
eo_bendcontour(tta,dtta) = callfemm("eo_bendcontour($(numc(tta))$(num(dtta)))")
eo_blockintegral(ptype) = callfemm("eo_blockintegral($(num(ptype)))")
eo_clearblock() = callfemm("eo_clearblock()")
eo_clearcontour() = callfemm("eo_clearcontour()")
eo_close() = callfemm("eo_close()")

eo_getconductorproperties(pname) = callfemm("eo_getconductorproperties($(quote_str(pname)))")

function eo_getpointvalues(x,y)
    callfemm("eo_getpointvalues($(numc(x))$(num(y)))")
end
function eo_getpointvalues(x::AbstractVector, y::AbstractVector)
    n = length(x)
    length(y) == n || error("must be same number of entries in X and Y arrays")
    z = zeros(n, 8)
    for k in 1:n
        z[k,:] .= collect(callfemm("eo_getpointvalues($(numc(x[k]))$(num(y[k])))"))
    end
    z
end

eo_getv(x,y) = let v = eo_getpointvalues(x,y); isa(v, Tuple) ? v[1] : v; end
function eo_getd(x,y)
    v = eo_getpointvalues(x,y)
    isa(v, Tuple) ? (v[2], v[3]) : v
end
function eo_gete(x,y)
    v = eo_getpointvalues(x,y)
    isa(v, Tuple) ? (v[4], v[5]) : v
end
function eo_getperm(x,y)
    v = eo_getpointvalues(x,y)
    isa(v, Tuple) ? (v[6], v[7]) : v
end
eo_getenergydensity(x,y) = let v = eo_getpointvalues(x,y); isa(v, Tuple) ? v[8] : v; end

eo_getelement(n) = callfemm("eo_getelement($(num(n)))")
eo_getnode(n) = callfemm("eo_getnode($(num(n)))")
eo_getprobleminfo() = callfemm("eo_getprobleminfo()")

eo_groupselectblock() = callfemm("eo_groupselectblock()")
eo_groupselectblock(n) = callfemm("eo_groupselectblock($(num(n)))")

eo_hidecontourplot() = callfemm("eo_hidecontourplot()")
eo_hidedensityplot() = callfemm("eo_hidedensityplot()")
eo_hidegrid() = callfemm("eo_hidegrid()")
eo_hidemesh() = callfemm("eo_hidemesh()")
eo_hidenames() = callfemm("eo_shownames(0)")
eo_hidepoints() = callfemm("eo_hidepoints()")

eo_lineintegral(ptype) = callfemm("eo_lineintegral($(num(ptype)))")

eo_makeplot(plottype,numpoints) = callfemm("eo_makeplot($(numc(plottype))$(num(numpoints)))")
eo_makeplot(plottype,numpoints,filename) = callfemm("eo_makeplot($(numc(plottype))$(numc(numpoints))$(quote_str(filename)))")
eo_makeplot(plottype,numpoints,filename,fileformat) = callfemm("eo_makeplot($(numc(plottype))$(numc(numpoints))$(quotec(filename))$(num(fileformat)))")

eo_maximize() = callfemm("eo_maximize()")
eo_minimize() = callfemm("eo_minimize()")
eo_numelements() = callfemm("eo_numelements()")
eo_numnodes() = callfemm("eo_numnodes()")
eo_refreshview() = callfemm("eo_refreshview()")
eo_reload() = callfemm("eo_reload()")
eo_resize(w,h) = callfemm("eo_resize($(numc(w))$(num(h)))")
eo_restore() = callfemm("eo_restore()")
eo_savebitmap(fn) = callfemm("eo_savebitmap($(quote_str(fn)))")
eo_savemetafile(fn) = callfemm("eo_savemetafile($(quote_str(fn)))")

eo_selectblock(x,y) = callfemm("eo_selectblock($(numc(x))$(num(y)))")
eo_selectconductor(pname) = callfemm("eo_selectconductor($(quote_str(pname)))")
eo_selectpoint(x,y) = callfemm("eo_selectpoint($(numc(x))$(num(y)))")
eo_seteditmode(mode) = callfemm("eo_seteditmode($(quote_str(mode)))")
eo_setgrid(density,ptype) = callfemm("eo_setgrid($(numc(density))$(quote_str(ptype)))")

eo_showcontourplot(numcontours,al,au) = callfemm("eo_showcontourplot($(numc(numcontours))$(numc(al))$(num(au)))")
eo_showdensityplot(legend,gscale,ptype,bu,bl) = callfemm("eo_showdensityplot($(numc(legend))$(numc(gscale))$(numc(ptype))$(numc(bu))$(num(bl)))")
eo_showgrid() = callfemm("eo_showgrid()")
eo_showmesh() = callfemm("eo_showmesh()")
eo_shownames() = callfemm("eo_shownames(1)")
eo_showpoints() = callfemm("eo_showpoints()")

eo_showvectorplot(plottype) = callfemm("eo_showvectorplot($(num(plottype)))")
eo_showvectorplot(plottype,scalefactor) = callfemm("eo_showvectorplot($(numc(plottype))$(num(scalefactor)))")

eo_smooth(flag) = callfemm("eo_smooth($(quote_str(flag)))")
eo_smoothoff() = callfemm("eo_smooth(\"off\")")
eo_smoothon() = callfemm("eo_smooth(\"on\")")
eo_snapgrid(flag) = callfemm("eo_gridsnap($(quote_str(flag)))")
eo_snapgridoff() = callfemm("eo_gridsnap(\"off\")")
eo_snapgridon() = callfemm("eo_gridsnap(\"on\")")
eo_gridsnap(flag) = callfemm("eo_gridsnap($(quote_str(flag)))")

eo_zoom(x1,y1,x2,y2) = callfemm("eo_zoom($(numc(x1))$(numc(y1))$(numc(x2))$(num(y2)))")
eo_zoomin() = callfemm("eo_zoomin()")
eo_zoomnatural() = callfemm("eo_zoomnatural()")
eo_zoomout() = callfemm("eo_zoomout()")

eo_gettitle() = callfemm("eo_gettitle()")

# ===================================================================
#  HEAT FLOW PRE-PROCESSOR (hi_)
# ===================================================================

hi_addarc(x1,y1,x2,y2,angle,maxseg) = callfemm("hi_addarc($(numc(x1))$(numc(y1))$(numc(x2))$(numc(y2))$(numc(angle))$(num(maxseg)))")
hi_addblocklabel(x,y) = callfemm("hi_addblocklabel($(numc(x))$(num(y)))")

function _hi_addboundprop_va(args...)
    n = length(args)
    if n == 0; return callfemm("hi_addboundprop()"); end
    if n == 1; return callfemm("hi_addboundprop($(quote_str(args[1])))"); end
    parts = [quotec(args[1])]
    for i in 2:n-1; push!(parts, numc(args[i])); end
    push!(parts, num(args[n]))
    callfemm("hi_addboundprop(" * join(parts) * ")")
end
hi_addboundprop() = _hi_addboundprop_va()
hi_addboundprop(pname) = _hi_addboundprop_va(pname)
hi_addboundprop(pname,fmt) = _hi_addboundprop_va(pname,fmt)
hi_addboundprop(pname,fmt,Tset) = _hi_addboundprop_va(pname,fmt,Tset)
hi_addboundprop(pname,fmt,Tset,qs) = _hi_addboundprop_va(pname,fmt,Tset,qs)
hi_addboundprop(pname,fmt,Tset,qs,Tinf) = _hi_addboundprop_va(pname,fmt,Tset,qs,Tinf)
hi_addboundprop(pname,fmt,Tset,qs,Tinf,h) = _hi_addboundprop_va(pname,fmt,Tset,qs,Tinf,h)
hi_addboundprop(pname,fmt,Tset,qs,Tinf,h,bta) = _hi_addboundprop_va(pname,fmt,Tset,qs,Tinf,h,bta)
hi_addboundprop(pname,fmt,Tset,qs,Tinf,h,bta,TinfRad) = _hi_addboundprop_va(pname,fmt,Tset,qs,Tinf,h,bta,TinfRad)

function _hi_addconductorprop_va(args...)
    n = length(args)
    if n == 0; return callfemm("hi_addconductorprop()"); end
    if n == 1; return callfemm("hi_addconductorprop($(quote_str(args[1])))"); end
    parts = [quotec(args[1])]
    for i in 2:n-1; push!(parts, numc(args[i])); end
    push!(parts, num(args[n]))
    callfemm("hi_addconductorprop(" * join(parts) * ")")
end
hi_addconductorprop() = _hi_addconductorprop_va()
hi_addconductorprop(pname) = _hi_addconductorprop_va(pname)
hi_addconductorprop(pname,Tc) = _hi_addconductorprop_va(pname,Tc)
hi_addconductorprop(pname,Tc,qc) = _hi_addconductorprop_va(pname,Tc,qc)
hi_addconductorprop(pname,Tc,qc,ptype) = _hi_addconductorprop_va(pname,Tc,qc,ptype)

function _hi_addmaterial_va(args...)
    n = length(args)
    if n == 0; return callfemm("hi_addmaterial()"); end
    if n == 1; return callfemm("hi_addmaterial($(quote_str(args[1])))"); end
    parts = [quotec(args[1])]
    for i in 2:n-1; push!(parts, numc(args[i])); end
    push!(parts, num(args[n]))
    callfemm("hi_addmaterial(" * join(parts) * ")")
end
hi_addmaterial() = _hi_addmaterial_va()
hi_addmaterial(pname) = _hi_addmaterial_va(pname)
hi_addmaterial(pname,kx) = _hi_addmaterial_va(pname,kx)
hi_addmaterial(pname,kx,ky) = _hi_addmaterial_va(pname,kx,ky)
hi_addmaterial(pname,kx,ky,qv) = _hi_addmaterial_va(pname,kx,ky,qv)
hi_addmaterial(pname,kx,ky,qv,kt) = _hi_addmaterial_va(pname,kx,ky,qv,kt)

hi_addnode(x,y) = callfemm("hi_addnode($(numc(x))$(num(y)))")

function _hi_addpointprop_va(args...)
    n = length(args)
    if n == 0; return callfemm("hi_addpointprop()"); end
    if n == 1; return callfemm("hi_addpointprop($(quote_str(args[1])))"); end
    parts = [quotec(args[1])]
    for i in 2:n-1; push!(parts, numc(args[i])); end
    push!(parts, num(args[n]))
    callfemm("hi_addpointprop(" * join(parts) * ")")
end
hi_addpointprop() = _hi_addpointprop_va()
hi_addpointprop(pname) = _hi_addpointprop_va(pname)
hi_addpointprop(pname,Tp) = _hi_addpointprop_va(pname,Tp)
hi_addpointprop(pname,Tp,qp) = _hi_addpointprop_va(pname,Tp,qp)

hi_addsegment(x1,y1,x2,y2) = callfemm("hi_addsegment($(numc(x1))$(numc(y1))$(numc(x2))$(num(y2)))")

hi_addtkpoint(name,b,h) = callfemm("hi_addtkpoint($(quotec(name))$(numc(b))$(num(h)))")
function hi_addtkpoints(name, bh::AbstractMatrix)
    for k in 1:size(bh,1)
        hi_addtkpoint(name, bh[k,1], bh[k,2])
    end
end

hi_analyze() = callfemm("hi_analyze()")
hi_analyze(n) = callfemm("hi_analyze($(num(n)))")
hi_analyse() = hi_analyze()
hi_analyse(n) = hi_analyze(n)

hi_attachouterspace() = callfemm("hi_attachouterspace()")
hi_attachdefault() = callfemm("hi_attachdefault()")
hi_clearselected() = callfemm("hi_clearselected()")
hi_cleartkpoints(n) = callfemm("hi_cleartkpoints($(quote_str(n)))")
hi_close() = callfemm("hi_close()")

hi_copyrotate(bx,by,angle,copies) = callfemm("hi_copyrotate($(numc(bx))$(numc(by))$(numc(angle))$(num(copies)))")
hi_copyrotate2(bx,by,angle,copies,editaction) = callfemm("hi_copyrotate($(numc(bx))$(numc(by))$(numc(angle))$(numc(copies))$(num(editaction)))")
hi_copytranslate(bx,by,copies) = callfemm("hi_copytranslate($(numc(bx))$(numc(by))$(num(copies)))")
hi_copytranslate2(bx,by,copies,editaction) = callfemm("hi_copytranslate($(numc(bx))$(numc(by))$(numc(copies))$(num(editaction)))")

hi_createmesh() = callfemm("hi_createmesh()")
hi_createradius(x,y,r) = callfemm("hi_createradius($(numc(x))$(numc(y))$(num(r)))")
hi_defineouterspace(Zo,Ro,Ri) = callfemm("hi_defineouterspace($(numc(Zo))$(numc(Ro))$(num(Ri)))")
hi_deleteboundprop(n) = callfemm("hi_deleteboundprop($(quote_str(n)))")
hi_deleteconductor(n) = callfemm("hi_deleteconductor($(quote_str(n)))")
hi_deletematerial(n) = callfemm("hi_deletematerial($(quote_str(n)))")
hi_deletepointprop(n) = callfemm("hi_deletepointprop($(quote_str(n)))")
hi_deleteselected() = callfemm("hi_deleteselected()")
hi_deleteselectedarcsegments() = callfemm("hi_deleteselectedarcsegments()")
hi_deleteselectedlabels() = callfemm("hi_deleteselectedlabels()")
hi_deleteselectednodes() = callfemm("hi_deleteselectednodes()")
hi_deleteselectedsegments() = callfemm("hi_deleteselectedsegments()")
hi_detachouterspace() = callfemm("hi_detachouterspace()")
hi_detachdefault() = callfemm("hi_detachdefault()")

function hi_drawarc(x1,y1,x2,y2,angle,maxseg)
    hi_addnode(x1,y1); hi_addnode(x2,y2)
    hi_addarc(x1,y1,x2,y2,angle,maxseg)
end
function hi_drawline(x1,y1,x2,y2)
    hi_addnode(x1,y1); hi_addnode(x2,y2)
    hi_addsegment(x1,y1,x2,y2)
end
function hi_drawpolyline(p::AbstractMatrix)
    for k in 1:size(p,1)-1
        hi_drawline(p[k,1],p[k,2],p[k+1,1],p[k+1,2])
    end
end
function hi_drawpolygon(p::AbstractMatrix)
    hi_drawpolyline(p)
    n = size(p,1)
    hi_drawline(p[1,1],p[1,2],p[n,1],p[n,2])
end
function hi_drawrectangle(x1,y1,x2,y2)
    hi_drawpolygon([x1 y1; x1 y2; x2 y2; x2 y1])
end

hi_getmaterial(matname) = callfemm("hi_getmaterial($(quote_str(matname)))")
hi_hidegrid() = callfemm("hi_hidegrid()")
hi_hidenames() = callfemm("hi_shownames(0)")
hi_loadsolution() = callfemm("hi_loadsolution()")
hi_maximize() = callfemm("hi_maximize()")
hi_minimize() = callfemm("hi_minimize()")

hi_mirror(x1,y1,x2,y2) = callfemm("hi_mirror($(numc(x1))$(numc(y1))$(numc(x2))$(num(y2)))")
hi_mirror2(x1,y1,x2,y2,editaction) = callfemm("hi_mirror($(numc(x1))$(numc(y1))$(numc(x2))$(numc(y2))$(num(editaction)))")

function hi_modifyboundprop(pname,pnum,pval)
    pnum == 0 ? callfemm("hi_modifyboundprop($(quotec(pname))$(numc(pnum))$(quote_str(pval)))") :
                callfemm("hi_modifyboundprop($(quotec(pname))$(numc(pnum))$(num(pval)))")
end
function hi_modifyconductorprop(pname,pnum,pval)
    pnum == 0 ? callfemm("hi_modifyconductorprop($(quotec(pname))$(numc(pnum))$(quote_str(pval)))") :
                callfemm("hi_modifyconductorprop($(quotec(pname))$(numc(pnum))$(num(pval)))")
end
function hi_modifymaterial(pname,pnum,pval)
    pnum == 0 ? callfemm("hi_modifymaterial($(quotec(pname))$(numc(pnum))$(quote_str(pval)))") :
                callfemm("hi_modifymaterial($(quotec(pname))$(numc(pnum))$(num(pval)))")
end
function hi_modifypointprop(pname,pnum,pval)
    pnum == 0 ? callfemm("hi_modifypointprop($(quotec(pname))$(numc(pnum))$(quote_str(pval)))") :
                callfemm("hi_modifypointprop($(quotec(pname))$(numc(pnum))$(num(pval)))")
end

hi_moverotate(bx,by,shiftangle) = callfemm("hi_moverotate($(numc(bx))$(numc(by))$(num(shiftangle)))")
hi_moverotate2(bx,by,shiftangle,editaction) = callfemm("hi_moverotate($(numc(bx))$(numc(by))$(numc(shiftangle))$(num(editaction)))")
hi_movetranslate(bx,by) = callfemm("hi_movetranslate($(numc(bx))$(num(by)))")
hi_movetranslate2(bx,by,editaction) = callfemm("hi_movetranslate($(numc(bx))$(numc(by))$(num(editaction)))")

hi_probdef(units,ptype,precision,depth,minangle) = callfemm("hi_probdef($(quotec(units))$(quotec(ptype))$(numc(precision))$(numc(depth))$(num(minangle)))")
hi_probdef(units,ptype,precision,depth,minangle,prevsoln,dt) = callfemm("hi_probdef($(quotec(units))$(quotec(ptype))$(numc(precision))$(numc(depth))$(numc(minangle))$(quotec(prevsoln))$(num(dt)))")

hi_purgemesh() = callfemm("hi_purgemesh()")
hi_readdxf(docname) = callfemm("hi_readdxf($(quote_str(docname)))")
hi_refreshview() = callfemm("hi_refreshview()")
hi_resize(w,h) = callfemm("hi_resize($(numc(w))$(num(h)))")
hi_restore() = callfemm("hi_restore()")
hi_saveas(fn) = callfemm("hi_saveas($(quote_str(fn)))")
hi_savebitmap(n) = callfemm("hi_savebitmap($(quote_str(n)))")
hi_savedxf(docname) = callfemm("hi_savedxf($(quote_str(docname)))")
hi_savemetafile(n) = callfemm("hi_savemetafile($(quote_str(n)))")

hi_scale(bx,by,sc) = callfemm("hi_scale($(numc(bx))$(numc(by))$(num(sc)))")
hi_scale2(bx,by,sc,ea) = callfemm("hi_scale($(numc(bx))$(numc(by))$(numc(sc))$(num(ea)))")

hi_selectarcsegment(x,y) = callfemm("hi_selectarcsegment($(numc(x))$(num(y)))")
hi_selectgroup(gr) = callfemm("hi_selectgroup($(num(gr)))")
hi_selectlabel(x,y) = callfemm("hi_selectlabel($(numc(x))$(num(y)))")
hi_selectnode(x,y) = callfemm("hi_selectnode($(numc(x))$(num(y)))")
hi_selectsegment(x,y) = callfemm("hi_selectsegment($(numc(x))$(num(y)))")

hi_selectcircle(x,y,R) = callfemm("hi_selectcircle($(numc(x))$(numc(y))$(num(R)))")
hi_selectcircle(x,y,R,editmode) = callfemm("hi_selectcircle($(numc(x))$(numc(y))$(numc(R))$(num(editmode)))")
hi_selectrectangle(x1,y1,x2,y2) = callfemm("hi_selectrectangle($(numc(x1))$(numc(y1))$(numc(x2))$(num(y2)))")
hi_selectrectangle(x1,y1,x2,y2,editmode) = callfemm("hi_selectrectangle($(numc(x1))$(numc(y1))$(numc(x2))$(numc(y2))$(num(editmode)))")

hi_setarcsegmentprop(maxsegdeg,propname,hide,group,incond) = callfemm("hi_setarcsegmentprop($(numc(maxsegdeg))$(quotec(propname))$(numc(hide))$(numc(group))$(quote_str(incond)))")
hi_setblockprop(blockname,automesh,meshsize,group) = callfemm("hi_setblockprop($(quotec(blockname))$(numc(automesh))$(numc(meshsize))$(num(group)))")
hi_seteditmode(editmode) = callfemm("hi_seteditmode($(quote_str(editmode)))")
hi_setfocus(docname) = callfemm("hi_setfocus($(quote_str(docname)))")
hi_setgrid(density,ptype) = callfemm("hi_setgrid($(numc(density))$(quote_str(ptype)))")
hi_setgroup(n) = callfemm("hi_setgroup($(num(n)))")
hi_setnodeprop(nodeprop,groupno,inconductor) = callfemm("hi_setnodeprop($(quotec(nodeprop))$(numc(groupno))$(quote_str(inconductor)))")
hi_setsegmentprop(pn,es,am,hi_val,gr,inconductor) = callfemm("hi_setsegmentprop($(quotec(pn))$(numc(es))$(numc(am))$(numc(hi_val))$(numc(gr))$(quote_str(inconductor)))")

hi_showgrid() = callfemm("hi_showgrid()")
hi_showmesh() = callfemm("hi_showmesh()")
hi_shownames() = callfemm("hi_shownames(1)")
hi_snapgridoff() = callfemm("hi_gridsnap(\"off\")")
hi_snapgridon() = callfemm("hi_gridsnap(\"on\")")

hi_zoom(x1,y1,x2,y2) = callfemm("hi_zoom($(numc(x1))$(numc(y1))$(numc(x2))$(num(y2)))")
hi_zoomin() = callfemm("hi_zoomin()")
hi_zoomnatural() = callfemm("hi_zoomnatural()")
hi_zoomout() = callfemm("hi_zoomout()")

hi_smartmesh(n) = callfemm("hi_smartmesh($(num(n)))")
hi_setcomment(c) = callfemm("hi_setcomment($(quote_str(c)))")
hi_getboundingbox() = callfemm("hi_getboundingbox()")
hi_getprobleminfo() = callfemm("hi_getprobleminfo()")
hi_gettitle() = callfemm("hi_gettitle()")
hi_gridsnap(flag) = callfemm("hi_gridsnap($(quote_str(flag)))")

function hi_makeABC(args...)
    n = length(args)
    if n == 0; return callfemm("hi_makeABC()"); end
    if n == 1; return callfemm("hi_makeABC($(num(args[1])))"); end
    parts = String[]
    for i in 1:n-1; push!(parts, numc(args[i])); end
    push!(parts, num(args[n]))
    callfemm("hi_makeABC(" * join(parts) * ")")
end

# ===================================================================
#  HEAT FLOW POST-PROCESSOR (ho_)
# ===================================================================

ho_addcontour(x,y) = callfemm("ho_addcontour($(numc(x))$(num(y)))")
ho_bendcontour(tta,dtta) = callfemm("ho_bendcontour($(numc(tta))$(num(dtta)))")
ho_blockintegral(ptype) = callfemm("ho_blockintegral($(num(ptype)))")
ho_clearblock() = callfemm("ho_clearblock()")
ho_clearcontour() = callfemm("ho_clearcontour()")
ho_close() = callfemm("ho_close()")

ho_getconductorproperties(pname) = callfemm("ho_getconductorproperties($(quote_str(pname)))")

function ho_getpointvalues(x,y)
    callfemm("ho_getpointvalues($(numc(x))$(num(y)))")
end
function ho_getpointvalues(x::AbstractVector, y::AbstractVector)
    n = length(x)
    length(y) == n || error("must be same number of entries in X and Y arrays")
    z = zeros(n, 7)
    for k in 1:n
        z[k,:] .= collect(callfemm("ho_getpointvalues($(numc(x[k]))$(num(y[k])))"))
    end
    z
end

ho_gett(x,y) = let v = ho_getpointvalues(x,y); isa(v, Tuple) ? v[1] : v; end
function ho_getf(x,y)
    v = ho_getpointvalues(x,y)
    isa(v, Tuple) ? (v[2], v[3]) : v
end
function ho_getg(x,y)
    v = ho_getpointvalues(x,y)
    isa(v, Tuple) ? (v[4], v[5]) : v
end
function ho_getk(x,y)
    v = ho_getpointvalues(x,y)
    isa(v, Tuple) ? (v[6], v[7]) : v
end

ho_getelement(n) = callfemm("ho_getelement($(num(n)))")
ho_getnode(n) = callfemm("ho_getnode($(num(n)))")
ho_getprobleminfo() = callfemm("ho_getprobleminfo()")

ho_groupselectblock() = callfemm("ho_groupselectblock()")
ho_groupselectblock(n) = callfemm("ho_groupselectblock($(num(n)))")

ho_hidecontourplot() = callfemm("ho_hidecontourplot()")
ho_hidedensityplot() = callfemm("ho_hidedensityplot()")
ho_hidegrid() = callfemm("ho_hidegrid()")
ho_hidemesh() = callfemm("ho_hidemesh()")
ho_hidenames() = callfemm("ho_shownames(0)")
ho_hidepoints() = callfemm("ho_hidepoints()")

ho_lineintegral(ptype) = callfemm("ho_lineintegral($(num(ptype)))")

ho_makeplot(plottype,numpoints) = callfemm("ho_makeplot($(numc(plottype))$(num(numpoints)))")
ho_makeplot(plottype,numpoints,filename) = callfemm("ho_makeplot($(numc(plottype))$(numc(numpoints))$(quote_str(filename)))")
ho_makeplot(plottype,numpoints,filename,fileformat) = callfemm("ho_makeplot($(numc(plottype))$(numc(numpoints))$(quotec(filename))$(num(fileformat)))")

ho_maximize() = callfemm("ho_maximize()")
ho_minimize() = callfemm("ho_minimize()")
ho_numelements() = callfemm("ho_numelements()")
ho_numnodes() = callfemm("ho_numnodes()")
ho_refreshview() = callfemm("ho_refreshview()")
ho_reload() = callfemm("ho_reload()")
ho_resize(w,h) = callfemm("ho_resize($(numc(w))$(num(h)))")
ho_restore() = callfemm("ho_restore()")
ho_savebitmap(fn) = callfemm("ho_savebitmap($(quote_str(fn)))")
ho_savemetafile(fn) = callfemm("ho_savemetafile($(quote_str(fn)))")

ho_selectblock(x,y) = callfemm("ho_selectblock($(numc(x))$(num(y)))")
ho_selectconductor(pname) = callfemm("ho_selectconductor($(quote_str(pname)))")
ho_selectpoint(x,y) = callfemm("ho_selectpoint($(numc(x))$(num(y)))")
ho_seteditmode(mode) = callfemm("ho_seteditmode($(quote_str(mode)))")
ho_setgrid(density,ptype) = callfemm("ho_setgrid($(numc(density))$(quote_str(ptype)))")

ho_showcontourplot(numcontours,al,au) = callfemm("ho_showcontourplot($(numc(numcontours))$(numc(al))$(num(au)))")
ho_showdensityplot(legend,gscale,ptype,bu,bl) = callfemm("ho_showdensityplot($(numc(legend))$(numc(gscale))$(numc(ptype))$(numc(bu))$(num(bl)))")
ho_showgrid() = callfemm("ho_showgrid()")
ho_showmesh() = callfemm("ho_showmesh()")
ho_shownames() = callfemm("ho_shownames(1)")
ho_showpoints() = callfemm("ho_showpoints()")

ho_showvectorplot(plottype) = callfemm("ho_showvectorplot($(num(plottype)))")
ho_showvectorplot(plottype,scalefactor) = callfemm("ho_showvectorplot($(numc(plottype))$(num(scalefactor)))")

ho_smooth(flag) = callfemm("ho_smooth($(quote_str(flag)))")
ho_smoothoff() = callfemm("ho_smooth(\"off\")")
ho_smoothon() = callfemm("ho_smooth(\"on\")")
ho_snapgrid(flag) = callfemm("ho_gridsnap($(quote_str(flag)))")
ho_snapgridoff() = callfemm("ho_gridsnap(\"off\")")
ho_snapgridon() = callfemm("ho_gridsnap(\"on\")")
ho_gridsnap(flag) = callfemm("ho_gridsnap($(quote_str(flag)))")

ho_zoom(x1,y1,x2,y2) = callfemm("ho_zoom($(numc(x1))$(numc(y1))$(numc(x2))$(num(y2)))")
ho_zoomin() = callfemm("ho_zoomin()")
ho_zoomnatural() = callfemm("ho_zoomnatural()")
ho_zoomout() = callfemm("ho_zoomout()")

ho_gettitle() = callfemm("ho_gettitle()")

# ===================================================================
#  CURRENT FLOW PRE-PROCESSOR (ci_)
# ===================================================================

ci_addarc(x1,y1,x2,y2,angle,maxseg) = callfemm("ci_addarc($(numc(x1))$(numc(y1))$(numc(x2))$(numc(y2))$(numc(angle))$(num(maxseg)))")
ci_addblocklabel(x,y) = callfemm("ci_addblocklabel($(numc(x))$(num(y)))")

function _ci_addboundprop_va(args...)
    n = length(args)
    if n == 0; return callfemm("ci_addboundprop()"); end
    if n == 1; return callfemm("ci_addboundprop($(quote_str(args[1])))"); end
    parts = [quotec(args[1])]
    for i in 2:n-1; push!(parts, numc(args[i])); end
    push!(parts, num(args[n]))
    callfemm("ci_addboundprop(" * join(parts) * ")")
end
ci_addboundprop() = _ci_addboundprop_va()
ci_addboundprop(pname) = _ci_addboundprop_va(pname)
ci_addboundprop(pname,Vs) = _ci_addboundprop_va(pname,Vs)
ci_addboundprop(pname,Vs,qs) = _ci_addboundprop_va(pname,Vs,qs)
ci_addboundprop(pname,Vs,qs,c0) = _ci_addboundprop_va(pname,Vs,qs,c0)
ci_addboundprop(pname,Vs,qs,c0,c1) = _ci_addboundprop_va(pname,Vs,qs,c0,c1)
ci_addboundprop(pname,Vs,qs,c0,c1,fmt) = _ci_addboundprop_va(pname,Vs,qs,c0,c1,fmt)

function _ci_addconductorprop_va(args...)
    n = length(args)
    if n == 0; return callfemm("ci_addconductorprop()"); end
    if n == 1; return callfemm("ci_addconductorprop($(quote_str(args[1])))"); end
    parts = [quotec(args[1])]
    for i in 2:n-1; push!(parts, numc(args[i])); end
    push!(parts, num(args[n]))
    callfemm("ci_addconductorprop(" * join(parts) * ")")
end
ci_addconductorprop() = _ci_addconductorprop_va()
ci_addconductorprop(pname) = _ci_addconductorprop_va(pname)
ci_addconductorprop(pname,Tc) = _ci_addconductorprop_va(pname,Tc)
ci_addconductorprop(pname,Tc,qc) = _ci_addconductorprop_va(pname,Tc,qc)
ci_addconductorprop(pname,Tc,qc,ptype) = _ci_addconductorprop_va(pname,Tc,qc,ptype)

function _ci_addmaterial_va(args...)
    n = length(args)
    if n == 0; return callfemm("ci_addmaterial()"); end
    if n == 1; return callfemm("ci_addmaterial($(quote_str(args[1])))"); end
    parts = [quotec(args[1])]
    for i in 2:n-1; push!(parts, numc(args[i])); end
    push!(parts, num(args[n]))
    callfemm("ci_addmaterial(" * join(parts) * ")")
end
ci_addmaterial() = _ci_addmaterial_va()
ci_addmaterial(pname) = _ci_addmaterial_va(pname)
ci_addmaterial(pname,ox) = _ci_addmaterial_va(pname,ox)
ci_addmaterial(pname,ox,oy) = _ci_addmaterial_va(pname,ox,oy)
ci_addmaterial(pname,ox,oy,ex) = _ci_addmaterial_va(pname,ox,oy,ex)
ci_addmaterial(pname,ox,oy,ex,ey) = _ci_addmaterial_va(pname,ox,oy,ex,ey)
ci_addmaterial(pname,ox,oy,ex,ey,ltx) = _ci_addmaterial_va(pname,ox,oy,ex,ey,ltx)
ci_addmaterial(pname,ox,oy,ex,ey,ltx,lty) = _ci_addmaterial_va(pname,ox,oy,ex,ey,ltx,lty)

ci_addnode(x,y) = callfemm("ci_addnode($(numc(x))$(num(y)))")

function _ci_addpointprop_va(args...)
    n = length(args)
    if n == 0; return callfemm("ci_addpointprop()"); end
    if n == 1; return callfemm("ci_addpointprop($(quote_str(args[1])))"); end
    parts = [quotec(args[1])]
    for i in 2:n-1; push!(parts, numc(args[i])); end
    push!(parts, num(args[n]))
    callfemm("ci_addpointprop(" * join(parts) * ")")
end
ci_addpointprop() = _ci_addpointprop_va()
ci_addpointprop(pname) = _ci_addpointprop_va(pname)
ci_addpointprop(pname,Tp) = _ci_addpointprop_va(pname,Tp)
ci_addpointprop(pname,Tp,qp) = _ci_addpointprop_va(pname,Tp,qp)

ci_addsegment(x1,y1,x2,y2) = callfemm("ci_addsegment($(numc(x1))$(numc(y1))$(numc(x2))$(num(y2)))")

ci_analyze() = callfemm("ci_analyze()")
ci_analyze(n) = callfemm("ci_analyze($(num(n)))")
ci_analyse() = ci_analyze()
ci_analyse(n) = ci_analyze(n)

ci_attachdefault() = callfemm("ci_attachdefault()")
ci_attachouterspace() = callfemm("ci_attachouterspace()")
ci_clearselected() = callfemm("ci_clearselected()")
ci_close() = callfemm("ci_close()")

ci_copyrotate(bx,by,angle,copies) = callfemm("ci_copyrotate($(numc(bx))$(numc(by))$(numc(angle))$(num(copies)))")
ci_copyrotate2(bx,by,angle,copies,editaction) = callfemm("ci_copyrotate($(numc(bx))$(numc(by))$(numc(angle))$(numc(copies))$(num(editaction)))")
ci_copytranslate(bx,by,copies) = callfemm("ci_copytranslate($(numc(bx))$(numc(by))$(num(copies)))")
ci_copytranslate2(bx,by,copies,editaction) = callfemm("ci_copytranslate($(numc(bx))$(numc(by))$(numc(copies))$(num(editaction)))")

ci_createmesh() = callfemm("ci_createmesh()")
ci_createradius(x,y,r) = callfemm("ci_createradius($(numc(x))$(numc(y))$(num(r)))")
ci_defineouterspace(Zo,Ro,Ri) = callfemm("ci_defineouterspace($(numc(Zo))$(numc(Ro))$(num(Ri)))")
ci_deleteboundprop(n) = callfemm("ci_deleteboundprop($(quote_str(n)))")
ci_deleteconductor(n) = callfemm("ci_deleteconductor($(quote_str(n)))")
ci_deletematerial(n) = callfemm("ci_deletematerial($(quote_str(n)))")
ci_deletepointprop(n) = callfemm("ci_deletepointprop($(quote_str(n)))")
ci_deleteselected() = callfemm("ci_deleteselected()")
ci_deleteselectedarcsegments() = callfemm("ci_deleteselectedarcsegments()")
ci_deleteselectedlabels() = callfemm("ci_deleteselectedlabels()")
ci_deleteselectednodes() = callfemm("ci_deleteselectednodes()")
ci_deleteselectedsegments() = callfemm("ci_deleteselectedsegments()")
ci_detachdefault() = callfemm("ci_detachdefault()")

function ci_drawarc(x1,y1,x2,y2,angle,maxseg)
    ci_addnode(x1,y1); ci_addnode(x2,y2)
    ci_addarc(x1,y1,x2,y2,angle,maxseg)
end
function ci_drawline(x1,y1,x2,y2)
    ci_addnode(x1,y1); ci_addnode(x2,y2)
    ci_addsegment(x1,y1,x2,y2)
end
function ci_drawpolyline(p::AbstractMatrix)
    for k in 1:size(p,1)-1
        ci_drawline(p[k,1],p[k,2],p[k+1,1],p[k+1,2])
    end
end
function ci_drawpolygon(p::AbstractMatrix)
    ci_drawpolyline(p)
    n = size(p,1)
    ci_drawline(p[1,1],p[1,2],p[n,1],p[n,2])
end
function ci_drawrectangle(x1,y1,x2,y2)
    ci_drawpolygon([x1 y1; x1 y2; x2 y2; x2 y1])
end

ci_detachouterspace() = callfemm("ci_detachouterspace()")
ci_getmaterial(matname) = callfemm("ci_getmaterial($(quote_str(matname)))")
ci_hidegrid() = callfemm("ci_hidegrid()")
ci_hidenames() = callfemm("ci_shownames(0)")
ci_loadsolution() = callfemm("ci_loadsolution()")
ci_maximize() = callfemm("ci_maximize()")
ci_minimize() = callfemm("ci_minimize()")

ci_mirror(x1,y1,x2,y2) = callfemm("ci_mirror($(numc(x1))$(numc(y1))$(numc(x2))$(num(y2)))")
ci_mirror2(x1,y1,x2,y2,editaction) = callfemm("ci_mirror($(numc(x1))$(numc(y1))$(numc(x2))$(numc(y2))$(num(editaction)))")

function ci_modifyboundprop(pname,pnum,pval)
    pnum == 0 ? callfemm("ci_modifyboundprop($(quotec(pname))$(numc(pnum))$(quote_str(pval)))") :
                callfemm("ci_modifyboundprop($(quotec(pname))$(numc(pnum))$(num(pval)))")
end
function ci_modifyconductorprop(pname,pnum,pval)
    pnum == 0 ? callfemm("ci_modifyconductorprop($(quotec(pname))$(numc(pnum))$(quote_str(pval)))") :
                callfemm("ci_modifyconductorprop($(quotec(pname))$(numc(pnum))$(num(pval)))")
end
function ci_modifymaterial(pname,pnum,pval)
    pnum == 0 ? callfemm("ci_modifymaterial($(quotec(pname))$(numc(pnum))$(quote_str(pval)))") :
                callfemm("ci_modifymaterial($(quotec(pname))$(numc(pnum))$(num(pval)))")
end
function ci_modifypointprop(pname,pnum,pval)
    pnum == 0 ? callfemm("ci_modifypointprop($(quotec(pname))$(numc(pnum))$(quote_str(pval)))") :
                callfemm("ci_modifypointprop($(quotec(pname))$(numc(pnum))$(num(pval)))")
end

ci_moverotate(bx,by,shiftangle) = callfemm("ci_moverotate($(numc(bx))$(numc(by))$(num(shiftangle)))")
ci_moverotate2(bx,by,shiftangle,editaction) = callfemm("ci_moverotate($(numc(bx))$(numc(by))$(numc(shiftangle))$(num(editaction)))")
ci_movetranslate(bx,by) = callfemm("ci_movetranslate($(numc(bx))$(num(by)))")
ci_movetranslate2(bx,by,editaction) = callfemm("ci_movetranslate($(numc(bx))$(numc(by))$(num(editaction)))")

ci_probdef(units,ptype,frequency,precision,depth,minangle) = callfemm("ci_probdef($(quotec(units))$(quotec(ptype))$(numc(frequency))$(numc(precision))$(numc(depth))$(num(minangle)))")

ci_purgemesh() = callfemm("ci_purgemesh()")
ci_readdxf(docname) = callfemm("ci_readdxf($(quote_str(docname)))")
ci_refreshview() = callfemm("ci_refreshview()")
ci_resize(w,h) = callfemm("ci_resize($(numc(w))$(num(h)))")
ci_restore() = callfemm("ci_restore()")
ci_saveas(fn) = callfemm("ci_saveas($(quote_str(fn)))")
ci_savebitmap(n) = callfemm("ci_savebitmap($(quote_str(n)))")
ci_savedxf(docname) = callfemm("ci_savedxf($(quote_str(docname)))")
ci_savemetafile(n) = callfemm("ci_savemetafile($(quote_str(n)))")

ci_scale(bx,by,sc) = callfemm("ci_scale($(numc(bx))$(numc(by))$(num(sc)))")
ci_scale2(bx,by,sc,ea) = callfemm("ci_scale($(numc(bx))$(numc(by))$(numc(sc))$(num(ea)))")

ci_selectarcsegment(x,y) = callfemm("ci_selectarcsegment($(numc(x))$(num(y)))")
ci_selectgroup(gr) = callfemm("ci_selectgroup($(num(gr)))")
ci_selectlabel(x,y) = callfemm("ci_selectlabel($(numc(x))$(num(y)))")
ci_selectnode(x,y) = callfemm("ci_selectnode($(numc(x))$(num(y)))")
ci_selectsegment(x,y) = callfemm("ci_selectsegment($(numc(x))$(num(y)))")

ci_selectcircle(x,y,R) = callfemm("ci_selectcircle($(numc(x))$(numc(y))$(num(R)))")
ci_selectcircle(x,y,R,editmode) = callfemm("ci_selectcircle($(numc(x))$(numc(y))$(numc(R))$(num(editmode)))")
ci_selectrectangle(x1,y1,x2,y2) = callfemm("ci_selectrectangle($(numc(x1))$(numc(y1))$(numc(x2))$(num(y2)))")
ci_selectrectangle(x1,y1,x2,y2,editmode) = callfemm("ci_selectrectangle($(numc(x1))$(numc(y1))$(numc(x2))$(numc(y2))$(num(editmode)))")

ci_setarcsegmentprop(maxsegdeg,propname,hide,group,incond) = callfemm("ci_setarcsegmentprop($(numc(maxsegdeg))$(quotec(propname))$(numc(hide))$(numc(group))$(quote_str(incond)))")
ci_setblockprop(blockname,automesh,meshsize,group) = callfemm("ci_setblockprop($(quotec(blockname))$(numc(automesh))$(numc(meshsize))$(num(group)))")
ci_seteditmode(editmode) = callfemm("ci_seteditmode($(quote_str(editmode)))")
ci_setfocus(docname) = callfemm("ci_setfocus($(quote_str(docname)))")
ci_setgrid(density,ptype) = callfemm("ci_setgrid($(numc(density))$(quote_str(ptype)))")
ci_setgroup(n) = callfemm("ci_setgroup($(num(n)))")
ci_setnodeprop(nodeprop,groupno,inconductor) = callfemm("ci_setnodeprop($(quotec(nodeprop))$(numc(groupno))$(quote_str(inconductor)))")
ci_setsegmentprop(pn,es,am,hi_val,gr,inconductor) = callfemm("ci_setsegmentprop($(quotec(pn))$(numc(es))$(numc(am))$(numc(hi_val))$(numc(gr))$(quote_str(inconductor)))")

ci_showgrid() = callfemm("ci_showgrid()")
ci_showmesh() = callfemm("ci_showmesh()")
ci_shownames() = callfemm("ci_shownames(1)")
ci_snapgridoff() = callfemm("ci_gridsnap(\"off\")")
ci_snapgridon() = callfemm("ci_gridsnap(\"on\")")

ci_zoom(x1,y1,x2,y2) = callfemm("ci_zoom($(numc(x1))$(numc(y1))$(numc(x2))$(num(y2)))")
ci_zoomin() = callfemm("ci_zoomin()")
ci_zoomnatural() = callfemm("ci_zoomnatural()")
ci_zoomout() = callfemm("ci_zoomout()")

ci_smartmesh(n) = callfemm("ci_smartmesh($(num(n)))")
ci_setcomment(c) = callfemm("ci_setcomment($(quote_str(c)))")
ci_getboundingbox() = callfemm("ci_getboundingbox()")
ci_getprobleminfo() = callfemm("ci_getprobleminfo()")
ci_gettitle() = callfemm("ci_gettitle()")
ci_gridsnap(flag) = callfemm("ci_gridsnap($(quote_str(flag)))")

function ci_makeABC(args...)
    n = length(args)
    if n == 0; return callfemm("ci_makeABC()"); end
    if n == 1; return callfemm("ci_makeABC($(num(args[1])))"); end
    parts = String[]
    for i in 1:n-1; push!(parts, numc(args[i])); end
    push!(parts, num(args[n]))
    callfemm("ci_makeABC(" * join(parts) * ")")
end

# ===================================================================
#  CURRENT FLOW POST-PROCESSOR (co_)
# ===================================================================

co_addcontour(x,y) = callfemm("co_addcontour($(numc(x))$(num(y)))")
co_bendcontour(tta,dtta) = callfemm("co_bendcontour($(numc(tta))$(num(dtta)))")
co_blockintegral(ptype) = callfemm("co_blockintegral($(num(ptype)))")
co_clearblock() = callfemm("co_clearblock()")
co_clearcontour() = callfemm("co_clearcontour()")
co_close() = callfemm("co_close()")

co_getconductorproperties(pname) = callfemm("co_getconductorproperties($(quote_str(pname)))")

function co_getpointvalues(x,y)
    callfemm("co_getpointvalues($(numc(x))$(num(y)))")
end
function co_getpointvalues(x::AbstractVector, y::AbstractVector)
    n = length(x)
    length(y) == n || error("must be same number of entries in X and Y arrays")
    z = zeros(n, 15)
    for k in 1:n
        z[k,:] .= collect(callfemm("co_getpointvalues($(numc(x[k]))$(num(y[k])))"))
    end
    z
end

co_getv(x,y) = let v = co_getpointvalues(x,y); isa(v, Tuple) ? v[1] : v; end
function co_getj(x,y)
    v = co_getpointvalues(x,y)
    isa(v, Tuple) ? (v[2], v[3]) : v
end
function co_getk(x,y)
    v = co_getpointvalues(x,y)
    isa(v, Tuple) ? (v[4], v[5]) : v
end
function co_gete(x,y)
    v = co_getpointvalues(x,y)
    isa(v, Tuple) ? (v[6], v[7]) : v
end

co_getelement(n) = callfemm("co_getelement($(num(n)))")
co_getnode(n) = callfemm("co_getnode($(num(n)))")
co_getprobleminfo() = callfemm("co_getprobleminfo()")

co_groupselectblock() = callfemm("co_groupselectblock()")
co_groupselectblock(n) = callfemm("co_groupselectblock($(num(n)))")

co_hidecontourplot() = callfemm("co_hidecontourplot()")
co_hidedensityplot() = callfemm("co_hidedensityplot()")
co_hidegrid() = callfemm("co_hidegrid()")
co_hidemesh() = callfemm("co_hidemesh()")
co_hidenames() = callfemm("co_shownames(0)")
co_hidepoints() = callfemm("co_hidepoints()")

co_lineintegral(ptype) = callfemm("co_lineintegral($(num(ptype)))")

co_makeplot(plottype,numpoints) = callfemm("co_makeplot($(numc(plottype))$(num(numpoints)))")
co_makeplot(plottype,numpoints,filename) = callfemm("co_makeplot($(numc(plottype))$(numc(numpoints))$(quote_str(filename)))")
co_makeplot(plottype,numpoints,filename,fileformat) = callfemm("co_makeplot($(numc(plottype))$(numc(numpoints))$(quotec(filename))$(num(fileformat)))")

co_maximize() = callfemm("co_maximize()")
co_minimize() = callfemm("co_minimize()")
co_numelements() = callfemm("co_numelements()")
co_numnodes() = callfemm("co_numnodes()")
co_refreshview() = callfemm("co_refreshview()")
co_reload() = callfemm("co_reload()")
co_resize(w,h) = callfemm("co_resize($(numc(w))$(num(h)))")
co_restore() = callfemm("co_restore()")
co_savebitmap(fn) = callfemm("co_savebitmap($(quote_str(fn)))")
co_savemetafile(fn) = callfemm("co_savemetafile($(quote_str(fn)))")

co_selectblock(x,y) = callfemm("co_selectblock($(numc(x))$(num(y)))")
co_selectconductor(pname) = callfemm("co_selectconductor($(quote_str(pname)))")
co_selectpoint(x,y) = callfemm("co_selectpoint($(numc(x))$(num(y)))")
co_seteditmode(mode) = callfemm("co_seteditmode($(quote_str(mode)))")
co_setgrid(density,ptype) = callfemm("co_setgrid($(numc(density))$(quote_str(ptype)))")

co_showcontourplot(numcontours,al,au) = callfemm("co_showcontourplot($(numc(numcontours))$(numc(al))$(num(au)))")
co_showdensityplot(legend,gscale,ptype,bu,bl) = callfemm("co_showdensityplot($(numc(legend))$(numc(gscale))$(numc(ptype))$(numc(bu))$(num(bl)))")
co_showgrid() = callfemm("co_showgrid()")
co_showmesh() = callfemm("co_showmesh()")
co_shownames() = callfemm("co_shownames(1)")
co_showpoints() = callfemm("co_showpoints()")

co_showvectorplot(plottype) = callfemm("co_showvectorplot($(num(plottype)))")
co_showvectorplot(plottype,scalefactor) = callfemm("co_showvectorplot($(numc(plottype))$(num(scalefactor)))")

co_smooth(flag) = callfemm("co_smooth($(quote_str(flag)))")
co_smoothoff() = callfemm("co_smooth(\"off\")")
co_smoothon() = callfemm("co_smooth(\"on\")")
co_snapgrid(flag) = callfemm("co_gridsnap($(quote_str(flag)))")
co_snapgridoff() = callfemm("co_gridsnap(\"off\")")
co_snapgridon() = callfemm("co_gridsnap(\"on\")")
co_gridsnap(flag) = callfemm("co_gridsnap($(quote_str(flag)))")

co_zoom(x1,y1,x2,y2) = callfemm("co_zoom($(numc(x1))$(numc(y1))$(numc(x2))$(num(y2)))")
co_zoomin() = callfemm("co_zoomin()")
co_zoomnatural() = callfemm("co_zoomnatural()")
co_zoomout() = callfemm("co_zoomout()")

co_gettitle() = callfemm("co_gettitle()")

# ===================================================================
#  Exports
# ===================================================================

# IPC layer
export openfemm, callfemm, callfemm_noeval, closefemm, femmselect
# Helpers
export num, numc, quote_str, quotec, complex2str
# Global
export opendocument, newdocument, smartmesh
export main_maximize, main_minimize, main_resize, main_restore
export hideconsole, showconsole, messagebox
# The editor forms dispatched to *_snapgridon/off, which are binding
# wrapper names rather than engine commands, so they always failed.
export mo_gridsnap, eo_gridsnap, ho_gridsnap, co_gridsnap
export showpointprops, hidepointprops
# Magnetics pre-processor
export mi_addarc, mi_addbhpoint, mi_addbhpoints, mi_addblocklabel
export mi_addboundprop, mi_addcircprop, mi_addmaterial, mi_addnode
export mi_addpointprop, mi_addsegment, mi_analyze, mi_analyse
export mi_attachouterspace, mi_attachdefault, mi_clearbhpoints, mi_clearselected
export mi_close, mi_copyrotate, mi_copyrotate2, mi_copytranslate, mi_copytranslate2
export mi_createmesh, mi_createradius, mi_defineouterspace
export mi_deleteboundprop, mi_deletecircuit, mi_deletematerial, mi_deletepointprop
export mi_deleteselected, mi_deleteselectedarcsegments, mi_deleteselectedlabels
export mi_deleteselectednodes, mi_deleteselectedsegments
export mi_detachouterspace, mi_detachdefault
export mi_drawarc, mi_drawline, mi_drawpolyline, mi_drawpolygon, mi_drawrectangle
export mi_getmaterial, mi_hidegrid, mi_hidenames, mi_loadsolution
export mi_maximize, mi_minimize
export mi_mirror, mi_mirror2
export mi_modifyboundprop, mi_modifycircprop, mi_modifymaterial, mi_modifypointprop
export mi_moverotate, mi_movetranslate, mi_movetranslate2
export mi_moverotate2
export mi_probdef, mi_purgemesh, mi_readdxf, mi_refreshview
export mi_resize, mi_restore, mi_saveas, mi_savebitmap, mi_savedxf, mi_savemetafile
export mi_scale, mi_scale2
export mi_selectarcsegment, mi_selectgroup, mi_selectlabel, mi_selectnode, mi_selectsegment
export mi_selectcircle, mi_selectrectangle
export mi_setarcsegmentprop, mi_setblockprop, mi_setcurrent
export mi_seteditmode, mi_setfocus, mi_setgrid, mi_setgroup
export mi_setnodeprop, mi_setsegmentprop
export mi_showgrid, mi_showmesh, mi_shownames, mi_snapgridoff, mi_snapgridon
export mi_zoom, mi_zoomin, mi_zoomnatural, mi_zoomout
export mi_smartmesh, mi_setcomment, mi_setprevious, mi_makeABC
export mi_getboundingbox, mi_getprobleminfo, mi_gettitle, mi_gridsnap
# Magnetics post-processor
export mo_addcontour, mo_bendcontour, mo_blockintegral, mo_clearblock, mo_clearcontour
export mo_close, mo_getpointvalues, mo_geta, mo_getb, mo_getcircuitproperties
export mo_getconductivity, mo_getelement, mo_getenergydensity, mo_geth, mo_getj
export mo_getmu, mo_getnode, mo_getpe, mo_getph, mo_getfill, mo_getprobleminfo
export mo_groupselectblock
export mo_hidecontourplot, mo_hidedensityplot, mo_hidegrid, mo_hidemesh
export mo_hidenames, mo_hidepoints
export mo_lineintegral, mo_makeplot
export mo_maximize, mo_minimize, mo_numelements, mo_numnodes
export mo_refreshview, mo_reload, mo_resize, mo_restore
export mo_savebitmap, mo_savemetafile
export mo_selectblock, mo_selectpoint, mo_seteditmode, mo_setweightingscheme, mo_setgrid
export mo_showcontourplot, mo_showdensityplot, mo_showgrid, mo_showmesh
export mo_shownames, mo_showpoints, mo_showvectorplot
export mo_smooth, mo_smoothoff, mo_smoothon
export mo_snapgrid, mo_snapgridoff, mo_snapgridon
export mo_zoom, mo_zoomin, mo_zoomnatural, mo_zoomout
export mo_gettitle
export mo_getgapb, mo_getgapa, mo_gapintegral, mo_getgapharmonics
export mo_getallelements, mo_getallnodes, mo_getallb, mo_getalla
export mo_getallgapb, mo_getallgapa, mo_getallgapharmonics
# Electrostatics pre-processor
export ei_addarc, ei_addblocklabel, ei_addboundprop, ei_addconductorprop
export ei_addmaterial, ei_addnode, ei_addpointprop, ei_addsegment
export ei_analyze, ei_analyse
export ei_attachouterspace, ei_attachdefault, ei_clearselected, ei_close
export ei_copyrotate, ei_copyrotate2, ei_copytranslate, ei_copytranslate2
export ei_createmesh, ei_createradius, ei_defineouterspace
export ei_deleteboundprop, ei_deleteconductor, ei_deletematerial, ei_deletepointprop
export ei_deleteselected, ei_deleteselectedarcsegments, ei_deleteselectedlabels
export ei_deleteselectednodes, ei_deleteselectedsegments
export ei_detachouterspace, ei_detachdefault
export ei_drawarc, ei_drawline, ei_drawpolyline, ei_drawpolygon, ei_drawrectangle
export ei_getmaterial, ei_hidegrid, ei_hidenames, ei_loadsolution
export ei_maximize, ei_minimize
export ei_mirror, ei_mirror2
export ei_modifyboundprop, ei_modifyconductorprop, ei_modifymaterial, ei_modifypointprop
export ei_moverotate, ei_movetranslate, ei_movetranslate2
export ei_moverotate2
export ei_probdef, ei_purgemesh, ei_readdxf, ei_refreshview
export ei_resize, ei_restore, ei_saveas, ei_savebitmap, ei_savedxf, ei_savemetafile
export ei_scale, ei_scale2
export ei_selectarcsegment, ei_selectgroup, ei_selectlabel, ei_selectnode, ei_selectsegment
export ei_selectcircle, ei_selectrectangle
export ei_setarcsegmentprop, ei_setblockprop
export ei_seteditmode, ei_setfocus, ei_setgrid, ei_setgroup
export ei_setnodeprop, ei_setsegmentprop
export ei_showgrid, ei_showmesh, ei_shownames, ei_snapgridoff, ei_snapgridon
export ei_zoom, ei_zoomin, ei_zoomnatural, ei_zoomout
export ei_smartmesh, ei_setcomment, ei_makeABC
export ei_getboundingbox, ei_getprobleminfo, ei_gettitle, ei_gridsnap
# Electrostatics post-processor
export eo_addcontour, eo_bendcontour, eo_blockintegral, eo_clearblock, eo_clearcontour
export eo_close, eo_getconductorproperties, eo_getpointvalues
export eo_selectconductor
export eo_getv, eo_getd, eo_gete, eo_getperm, eo_getenergydensity
export eo_getelement, eo_getnode, eo_getprobleminfo
export eo_groupselectblock
export eo_hidecontourplot, eo_hidedensityplot, eo_hidegrid, eo_hidemesh
export eo_hidenames, eo_hidepoints
export eo_lineintegral, eo_makeplot
export eo_maximize, eo_minimize, eo_numelements, eo_numnodes
export eo_refreshview, eo_reload, eo_resize, eo_restore
export eo_savebitmap, eo_savemetafile
export eo_selectblock, eo_selectpoint, eo_seteditmode, eo_setgrid
export eo_showcontourplot, eo_showdensityplot, eo_showgrid, eo_showmesh
export eo_shownames, eo_showpoints, eo_showvectorplot
export eo_smooth, eo_smoothoff, eo_smoothon
export eo_snapgrid, eo_snapgridoff, eo_snapgridon
export eo_zoom, eo_zoomin, eo_zoomnatural, eo_zoomout
export eo_gettitle
export eo_getallelements, eo_getallnodes, eo_getalld, eo_getallv
# Heat flow pre-processor
export hi_addarc, hi_addblocklabel, hi_addboundprop, hi_addconductorprop
export hi_addmaterial, hi_addnode, hi_addpointprop, hi_addsegment
export hi_addtkpoint, hi_addtkpoints
export hi_analyze, hi_analyse
export hi_attachouterspace, hi_attachdefault, hi_clearselected, hi_cleartkpoints, hi_close
export hi_copyrotate, hi_copyrotate2, hi_copytranslate, hi_copytranslate2
export hi_createmesh, hi_createradius, hi_defineouterspace
export hi_deleteboundprop, hi_deleteconductor, hi_deletematerial, hi_deletepointprop
export hi_deleteselected, hi_deleteselectedarcsegments, hi_deleteselectedlabels
export hi_deleteselectednodes, hi_deleteselectedsegments
export hi_detachouterspace, hi_detachdefault
export hi_drawarc, hi_drawline, hi_drawpolyline, hi_drawpolygon, hi_drawrectangle
export hi_getmaterial, hi_hidegrid, hi_hidenames, hi_loadsolution
export hi_maximize, hi_minimize
export hi_mirror, hi_mirror2
export hi_modifyboundprop, hi_modifyconductorprop, hi_modifymaterial, hi_modifypointprop
export hi_moverotate, hi_movetranslate, hi_movetranslate2
export hi_moverotate2
export hi_probdef, hi_purgemesh, hi_readdxf, hi_refreshview
export hi_resize, hi_restore, hi_saveas, hi_savebitmap, hi_savedxf, hi_savemetafile
export hi_scale, hi_scale2
export hi_selectarcsegment, hi_selectgroup, hi_selectlabel, hi_selectnode, hi_selectsegment
export hi_selectcircle, hi_selectrectangle
export hi_setarcsegmentprop, hi_setblockprop
export hi_seteditmode, hi_setfocus, hi_setgrid, hi_setgroup
export hi_setnodeprop, hi_setsegmentprop
export hi_showgrid, hi_showmesh, hi_shownames, hi_snapgridoff, hi_snapgridon
export hi_zoom, hi_zoomin, hi_zoomnatural, hi_zoomout
export hi_smartmesh, hi_setcomment, hi_makeABC
export hi_getboundingbox, hi_getprobleminfo, hi_gettitle, hi_gridsnap
# Heat flow post-processor
export ho_addcontour, ho_bendcontour, ho_blockintegral, ho_clearblock, ho_clearcontour
export ho_close, ho_getconductorproperties, ho_getpointvalues
export ho_selectconductor
export ho_gett, ho_getf, ho_getg, ho_getk
export ho_getelement, ho_getnode, ho_getprobleminfo
export ho_groupselectblock
export ho_hidecontourplot, ho_hidedensityplot, ho_hidegrid, ho_hidemesh
export ho_hidenames, ho_hidepoints
export ho_lineintegral, ho_makeplot
export ho_maximize, ho_minimize, ho_numelements, ho_numnodes
export ho_refreshview, ho_reload, ho_resize, ho_restore
export ho_savebitmap, ho_savemetafile
export ho_selectblock, ho_selectpoint, ho_seteditmode, ho_setgrid
export ho_showcontourplot, ho_showdensityplot, ho_showgrid, ho_showmesh
export ho_shownames, ho_showpoints, ho_showvectorplot
export ho_smooth, ho_smoothoff, ho_smoothon
export ho_snapgrid, ho_snapgridoff, ho_snapgridon
export ho_zoom, ho_zoomin, ho_zoomnatural, ho_zoomout
export ho_gettitle
export ho_getallelements, ho_getallnodes, ho_getallf, ho_getallt
# Current flow pre-processor
export ci_addarc, ci_addblocklabel, ci_addboundprop, ci_addconductorprop
export ci_addmaterial, ci_addnode, ci_addpointprop, ci_addsegment
export ci_analyze, ci_analyse
export ci_attachdefault, ci_clearselected, ci_close
export ci_attachouterspace
export ci_copyrotate, ci_copyrotate2, ci_copytranslate, ci_copytranslate2
export ci_createmesh, ci_createradius
export ci_deleteboundprop, ci_deleteconductor, ci_deletematerial, ci_deletepointprop
export ci_deleteselected, ci_deleteselectedarcsegments, ci_deleteselectedlabels
export ci_deleteselectednodes, ci_deleteselectedsegments
export ci_defineouterspace
export ci_detachdefault
export ci_detachouterspace
export ci_drawarc, ci_drawline, ci_drawpolyline, ci_drawpolygon, ci_drawrectangle
export ci_getmaterial, ci_hidegrid, ci_hidenames, ci_loadsolution
export ci_maximize, ci_minimize
export ci_mirror, ci_mirror2
export ci_modifyboundprop, ci_modifyconductorprop, ci_modifymaterial, ci_modifypointprop
export ci_moverotate, ci_movetranslate, ci_movetranslate2
export ci_moverotate2
export ci_probdef, ci_purgemesh, ci_readdxf, ci_refreshview
export ci_resize, ci_restore, ci_saveas, ci_savebitmap, ci_savedxf, ci_savemetafile
export ci_scale, ci_scale2
export ci_selectarcsegment, ci_selectgroup, ci_selectlabel, ci_selectnode, ci_selectsegment
export ci_selectcircle, ci_selectrectangle
export ci_setarcsegmentprop, ci_setblockprop
export ci_seteditmode, ci_setfocus, ci_setgrid, ci_setgroup
export ci_setnodeprop, ci_setsegmentprop
export ci_showgrid, ci_showmesh, ci_shownames, ci_snapgridoff, ci_snapgridon
export ci_zoom, ci_zoomin, ci_zoomnatural, ci_zoomout
export ci_smartmesh, ci_setcomment, ci_makeABC
export ci_getboundingbox, ci_getprobleminfo, ci_gettitle, ci_gridsnap
# Current flow post-processor
export co_addcontour, co_bendcontour, co_blockintegral, co_clearblock, co_clearcontour
export co_close, co_getconductorproperties, co_getpointvalues
export co_selectconductor
export co_getv, co_getj, co_getk, co_gete
export co_getelement, co_getnode, co_getprobleminfo
export co_groupselectblock
export co_hidecontourplot, co_hidedensityplot, co_hidegrid, co_hidemesh
export co_hidenames, co_hidepoints
export co_lineintegral, co_makeplot
export co_maximize, co_minimize, co_numelements, co_numnodes
export co_refreshview, co_reload, co_resize, co_restore
export co_savebitmap, co_savemetafile
export co_selectblock, co_selectpoint, co_seteditmode, co_setgrid
export co_showcontourplot, co_showdensityplot, co_showgrid, co_showmesh
export co_shownames, co_showpoints, co_showvectorplot
export co_smooth, co_smoothoff, co_smoothon
export co_snapgrid, co_snapgridoff, co_snapgridon
export co_zoom, co_zoomin, co_zoomnatural, co_zoomout
export co_gettitle
export co_getallelements, co_getallnodes, co_getallj, co_getallv

end # module JuliaFEMM
