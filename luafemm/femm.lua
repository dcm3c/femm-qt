-- SPDX-License-Identifier: MIT
-- Copyright (C) 2026 David Meeker and contributors

-- femm.lua — FEMM IPC binding for Lua 4 with CComplex numbers
-- Connects to femm_headless or femm_gui over TCP using LuaSocket 1.0.

_femm_conn = nil
_femm_port = 48015

-- ============================================================
-- Transport: openfemm / closefemm / callfemm
-- ============================================================

function try_connect(port)
    -- Bound each connect attempt to 0.2 s. Without a timeout, connect() to a
    -- not-yet-listening port can block for the OS-default connect timeout
    -- (~2 s on some Windows machines, where a SYN to a closed loopback port is
    -- dropped rather than refused), making the launch probe and the wait loop
    -- agonizingly slow. 0.2 s is plenty for a real localhost handshake while
    -- still failing fast so the poll loop can retry as the server comes up.
    local sock, err = connect("127.0.0.1", port, 0.2)
    if sock then return sock end
    return nil
end

function openfemm(headless)
    local port = _femm_port
    if headless == nil then headless = 0 end

    local sock = try_connect(port)
    if sock then
        _femm_conn = sock
        return
    end

    -- Detect Windows so the server is spawned correctly on any OS (this
    -- binding works regardless of platform, like the other language bindings).
    local iswin = (getenv("WINDIR") ~= nil) or (getenv("OS") == "Windows_NT")

    -- The windowless headless build (femm_headlessw) exists only on Windows,
    -- where it keeps a console window from flashing; elsewhere it is the plain
    -- femm_headless.
    local exename
    if headless == 1 then
        if iswin then exename = "femm_headlessw" else exename = "femm_headless" end
    else
        exename = "femm_gui"
    end

    local femmdir = getenv("FEMM_DIR")
    local exe = nil
    if femmdir and femmdir ~= "" then
        -- Try every platform's executable form; the first that opens wins, so
        -- no OS branching is needed. The .app form is the macOS application
        -- bundle (the GUI builds as <exename>.app, whose real executable is at
        -- Contents/MacOS/<exename>); it only exists on macOS, skipped elsewhere.
        local forms = {
            femmdir .. "/" .. exename,                                       -- Linux / macOS CLI
            femmdir .. "/" .. exename .. ".exe",                             -- Windows
            femmdir .. "/" .. exename .. ".app/Contents/MacOS/" .. exename,  -- macOS .app
        }
        local i = 1
        while i <= getn(forms) and not exe do
            local f = openfile(forms[i], "r")
            if f then closefile(f); exe = forms[i] end
            i = i + 1
        end
    end

    if not exe then
        error("Cannot find " .. exename .. ". Set FEMM_DIR environment variable.")
    end

    local cmd = '"' .. exe .. '" --port ' .. port
    -- Launch in the background, the OS-appropriate way.
    if iswin then
        execute('start "" /B ' .. cmd .. " >NUL 2>NUL")
    else
        execute(cmd .. " >/dev/null 2>&1 &")
    end

    -- Wait for the server's listening socket to come up. Poll connect-first,
    -- then yield the CPU with a short sleep(). We deliberately do NOT busy-wait
    -- on clock(): a spin loop pins a core and steals cycles from the very
    -- femm-qt process we are waiting for, slowing its startup. The timeout is
    -- counted in attempts rather than clock() because clock() is not a portable
    -- wall clock here -- it measures CPU time on POSIX (so it barely advances
    -- while we sleep) and wall time on Windows. ~0.02 s/attempt * 1000 ~= 20 s,
    -- enough to cover a cold Qt start (first launch, antivirus scan).
    local attempts = 0
    while attempts < 1000 do
        sock = try_connect(port)
        if sock then
            _femm_conn = sock
            return
        end
        sleep(0.02)
        attempts = attempts + 1
    end
    error("femm-qt launched but TCP connection failed (timed out)")
end

function closefemm()
    if _femm_conn then
        close(_femm_conn)
        _femm_conn = nil
    end
end

function format_arg(v)
    if v == nil then return nil end
    if type(v) == "string" then
        return '"' .. gsub(gsub(v, "\\", "\\\\"), '"', '\\"') .. '"'
    end
    return tostring(v)
end

function callfemm(cmd, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10)
    if not _femm_conn then error("Not connected to FEMM. Call openfemm() first.") end

    -- Collect non-nil arguments
    local args = { a1, a2, a3, a4, a5, a6, a7, a8, a9, a10 }
    local nargs = 0
    for i = 1, 10 do
        if args[i] ~= nil then nargs = i
        else break end
    end

    -- If no extra args and cmd contains "(", send as pre-formatted
    local wire
    if nargs == 0 and strfind(cmd, "%(") then
        wire = cmd .. "\n"
    else
        local parts = ""
        for i = 1, nargs do
            local formatted = format_arg(args[i])
            if formatted then
                if parts ~= "" then parts = parts .. "," end
                parts = parts .. formatted
            end
        end
        wire = cmd .. "(" .. parts .. ")\n"
    end

    send(_femm_conn, wire)

    local response, err = receive(_femm_conn)
    if err then
        if err == "closed" then
            _femm_conn = nil
            error("FEMM connection closed unexpectedly")
        end
        error("FEMM receive failed: " .. err)
    end

    -- Strip trailing \r
    if strsub(response, -1) == "\r" then
        response = strsub(response, 1, -2)
    end

    if response == "" then return nil end

    -- Check for error response
    if strsub(response, 1, 6) == "error:" then
        error("FEMM: " .. strsub(response, 7))
    end

    -- Parse tab-separated response
    local results = {}
    local pos = 1
    local len = strlen(response)
    while pos <= len do
        local tab = strfind(response, "\t", pos)
        local field
        if tab then
            field = strsub(response, pos, tab - 1)
            pos = tab + 1
        else
            field = strsub(response, pos)
            pos = len + 1
        end
        -- Try to parse as number
        local n = tonumber(field)
        if n then
            tinsert(results, n)
        elseif strsub(field, 1, 1) == '"' and strsub(field, -1) == '"' then
            tinsert(results, strsub(field, 2, -2))
        elseif field ~= "" then
            tinsert(results, field)
        end
    end

    if getn(results) == 0 then return nil end
    if getn(results) == 1 then return results[1] end
    return results
end

-- ============================================================
-- Global / document commands
-- ============================================================
function opendocument(fn) return callfemm("open", fn) end
function newdocument(n) return callfemm("create", n) end
function smartmesh(n) return callfemm("smartmesh", n) end
function setcurrentdirectory(d) return callfemm("setcurrentdirectory", d) end

function main_maximize() return callfemm("main_maximize") end
function main_minimize() return callfemm("main_minimize") end
function main_resize(w, h) return callfemm("main_resize", w, h) end
function main_restore() return callfemm("main_restore") end

-- ============================================================
-- MAGNETICS PRE-PROCESSOR (mi_)
-- ============================================================
function mi_addarc(x1,y1,x2,y2,angle,maxseg) return callfemm("mi_addarc",x1,y1,x2,y2,angle,maxseg) end
function mi_addbhpoint(name,b,h) return callfemm("mi_addbhpoint",name,b,h) end
function mi_addblocklabel(x,y) return callfemm("mi_addblocklabel",x,y) end
function mi_addboundprop(a1,a2,a3,a4,a5,a6,a7,a8,a9,a10) return callfemm("mi_addboundprop",a1,a2,a3,a4,a5,a6,a7,a8,a9,a10) end
function mi_addcircprop(pname,ic,ptype) return callfemm("mi_addcircprop",pname,ic,ptype) end
function mi_addmaterial(a1,a2,a3,a4,a5,a6,a7,a8,a9,a10) return callfemm("mi_addmaterial",a1,a2,a3,a4,a5,a6,a7,a8,a9,a10) end
function mi_addnode(x,y) return callfemm("mi_addnode",x,y) end
function mi_addpointprop(pname,ap,jp) return callfemm("mi_addpointprop",pname,ap,jp) end
function mi_addsegment(x1,y1,x2,y2) return callfemm("mi_addsegment",x1,y1,x2,y2) end

function mi_analyze(flag) return callfemm("mi_analyze",flag) end
function mi_analyse(flag) return mi_analyze(flag) end

function mi_attachouterspace() return callfemm("mi_attachouterspace") end
function mi_attachdefault() return callfemm("mi_attachdefault") end
function mi_clearbhpoints(n) return callfemm("mi_clearbhpoints",n) end
function mi_clearselected() return callfemm("mi_clearselected") end
function mi_close() return callfemm("mi_close") end

function mi_copyrotate(bx,by,angle,copies) return callfemm("mi_copyrotate",bx,by,angle,copies) end
function mi_copyrotate2(bx,by,angle,copies,editaction) return callfemm("mi_copyrotate",bx,by,angle,copies,editaction) end
function mi_copytranslate(bx,by,copies) return callfemm("mi_copytranslate",bx,by,copies) end
function mi_copytranslate2(bx,by,copies,editaction) return callfemm("mi_copytranslate",bx,by,copies,editaction) end

function mi_createmesh() return callfemm("mi_createmesh") end
function mi_createradius(x,y,r) return callfemm("mi_createradius",x,y,r) end
function mi_defineouterspace(Zo,Ro,Ri) return callfemm("mi_defineouterspace",Zo,Ro,Ri) end
function mi_deleteboundprop(n) return callfemm("mi_deleteboundprop",n) end
function mi_deletecircuit(n) return callfemm("mi_deletecircuit",n) end
function mi_deletematerial(n) return callfemm("mi_deletematerial",n) end
function mi_deletepointprop(n) return callfemm("mi_deletepointprop",n) end
function mi_deleteselected() return callfemm("mi_deleteselected") end
function mi_deleteselectedarcsegments() return callfemm("mi_deleteselectedarcsegments") end
function mi_deleteselectedlabels() return callfemm("mi_deleteselectedlabels") end
function mi_deleteselectednodes() return callfemm("mi_deleteselectednodes") end
function mi_deleteselectedsegments() return callfemm("mi_deleteselectedsegments") end
function mi_detachouterspace() return callfemm("mi_detachouterspace") end
function mi_detachdefault() return callfemm("mi_detachdefault") end

function mi_drawarc(x1,y1,x2,y2,angle,maxseg) return callfemm("mi_drawarc",x1,y1,x2,y2,angle,maxseg) end
function mi_drawline(x1,y1,x2,y2) return callfemm("mi_drawline",x1,y1,x2,y2) end
function mi_drawpolyline(p)
    local k = 1
    while k < getn(p) do
        mi_drawline(p[k][1],p[k][2],p[k+1][1],p[k+1][2])
        k = k + 1
    end
end
function mi_drawpolygon(p)
    mi_drawpolyline(p)
    mi_drawline(p[1][1],p[1][2],p[getn(p)][1],p[getn(p)][2])
end
function mi_drawrectangle(x1,y1,x2,y2)
    mi_drawpolygon({{x1,y1},{x1,y2},{x2,y2},{x2,y1}})
end

function mi_getmaterial(matname) return callfemm("mi_getmaterial",matname) end
function mi_hidegrid() return callfemm("mi_hidegrid") end
function mi_hidenames() return callfemm("mi_shownames",0) end
function mi_loadsolution() return callfemm("mi_loadsolution") end
function mi_maximize() return callfemm("mi_maximize") end
function mi_minimize() return callfemm("mi_minimize") end

function mi_mirror(x1,y1,x2,y2) return callfemm("mi_mirror",x1,y1,x2,y2) end
function mi_mirror2(x1,y1,x2,y2,editaction) return callfemm("mi_mirror",x1,y1,x2,y2,editaction) end

function mi_modifyboundprop(pname,pnum,pval) return callfemm("mi_modifyboundprop",pname,pnum,pval) end
function mi_modifycircprop(pname,pnum,pval) return callfemm("mi_modifycircprop",pname,pnum,pval) end
function mi_modifymaterial(pname,pnum,pval) return callfemm("mi_modifymaterial",pname,pnum,pval) end
function mi_modifypointprop(pname,pnum,pval) return callfemm("mi_modifypointprop",pname,pnum,pval) end

function mi_moverotate(bx,by,shiftangle) return callfemm("mi_moverotate",bx,by,shiftangle) end
function mi_movetranslate(bx,by) return callfemm("mi_movetranslate",bx,by) end
function mi_movetranslate2(bx,by,editaction) return callfemm("mi_movetranslate",bx,by,editaction) end

function mi_probdef(a1,a2,a3,a4,a5,a6,a7) return callfemm("mi_probdef",a1,a2,a3,a4,a5,a6,a7) end
function mi_purgemesh() return callfemm("mi_purgemesh") end
function mi_readdxf(docname) return callfemm("mi_readdxf",docname) end
function mi_refreshview() return callfemm("mi_refreshview") end
function mi_resize(w,h) return callfemm("mi_resize",w,h) end
function mi_restore() return callfemm("mi_restore") end
function mi_saveas(fn) return callfemm("mi_saveas",fn) end
function mi_savebitmap(n) return callfemm("mi_savebitmap",n) end
function mi_savedxf(docname) return callfemm("mi_savedxf",docname) end
function mi_savemetafile(n) return callfemm("mi_savemetafile",n) end

function mi_scale(bx,by,sc) return callfemm("mi_scale",bx,by,sc) end
function mi_scale2(bx,by,sc,ea) return callfemm("mi_scale",bx,by,sc,ea) end

function mi_selectarcsegment(x,y) return callfemm("mi_selectarcsegment",x,y) end
function mi_selectgroup(gr) return callfemm("mi_selectgroup",gr) end
function mi_selectlabel(x,y) return callfemm("mi_selectlabel",x,y) end
function mi_selectnode(x,y) return callfemm("mi_selectnode",x,y) end
function mi_selectsegment(x,y) return callfemm("mi_selectsegment",x,y) end

function mi_selectcircle(x,y,R,editmode) return callfemm("mi_selectcircle",x,y,R,editmode) end
function mi_selectrectangle(x1,y1,x2,y2,editmode) return callfemm("mi_selectrectangle",x1,y1,x2,y2,editmode) end

function mi_setarcsegmentprop(maxsegdeg,propname,hide,group) return callfemm("mi_setarcsegmentprop",maxsegdeg,propname,hide,group) end
function mi_setblockprop(blockname,automesh,meshsize,incirc,magdir,group,turns) return callfemm("mi_setblockprop",blockname,automesh,meshsize,incirc,magdir,group,turns) end
function mi_setcurrent(name,x) return mi_modifycircprop(name, 1, x) end
function mi_seteditmode(editmode) return callfemm("mi_seteditmode",editmode) end
function mi_setfocus(docname) return callfemm("mi_setfocus",docname) end
function mi_setgrid(density,ptype) return callfemm("mi_setgrid",density,ptype) end
function mi_setgroup(n) return callfemm("mi_setgroup",n) end
function mi_setnodeprop(nodeprop,groupno) return callfemm("mi_setnodeprop",nodeprop,groupno) end
function mi_setsegmentprop(pn,es,am,hi,gr) return callfemm("mi_setsegmentprop",pn,es,am,hi,gr) end

function mi_showgrid() return callfemm("mi_showgrid") end
function mi_showmesh() return callfemm("mi_showmesh") end
function mi_shownames() return callfemm("mi_shownames",1) end
function mi_gridsnap(flag)
    if flag == 1 then return callfemm("mi_gridsnap","on")
    else return callfemm("mi_gridsnap","off") end
end

function mi_zoom(x1,y1,x2,y2) return callfemm("mi_zoom",x1,y1,x2,y2) end
function mi_zoomin() return callfemm("mi_zoomin") end
function mi_zoomnatural() return callfemm("mi_zoomnatural") end
function mi_zoomout() return callfemm("mi_zoomout") end

function mi_smartmesh(n) return callfemm("mi_smartmesh",n) end
function mi_setcomment(c) return callfemm("mi_setcomment",c) end
function mi_getboundingbox() return callfemm("mi_getboundingbox") end
function mi_getprobleminfo() return callfemm("mi_getprobleminfo") end
function mi_gettitle() return callfemm("mi_gettitle") end
function mi_setprevious(a1,a2) return callfemm("mi_setprevious",a1,a2) end
function mi_makeABC(a1,a2,a3,a4,a5,a6,a7) return callfemm("mi_makeABC",a1,a2,a3,a4,a5,a6,a7) end

-- ============================================================
-- MAGNETICS POST-PROCESSOR (mo_)
-- ============================================================
function mo_addcontour(x,y) return callfemm("mo_addcontour",x,y) end
function mo_bendcontour(tta,dtta) return callfemm("mo_bendcontour",tta,dtta) end
function mo_blockintegral(ptype) return callfemm("mo_blockintegral",ptype) end
function mo_clearblock() return callfemm("mo_clearblock") end
function mo_clearcontour() return callfemm("mo_clearcontour") end
function mo_close() return callfemm("mo_close") end

function mo_getpointvalues(x,y) return callfemm("mo_getpointvalues",x,y) end
function mo_getcircuitproperties(name) return callfemm("mo_getcircuitproperties",name) end
function mo_getelement(n) return callfemm("mo_getelement",n) end
function mo_getnode(n) return callfemm("mo_getnode",n) end
function mo_getprobleminfo() return callfemm("mo_getprobleminfo") end

function mo_groupselectblock(n) return callfemm("mo_groupselectblock",n) end

function mo_hidecontourplot() return callfemm("mo_hidecontourplot") end
function mo_hidedensityplot() return callfemm("mo_hidedensityplot") end
function mo_hidegrid() return callfemm("mo_hidegrid") end
function mo_hidemesh() return callfemm("mo_hidemesh") end
function mo_hidenames() return callfemm("mo_shownames",0) end
function mo_hidepoints() return callfemm("mo_hidepoints") end

function mo_lineintegral(ptype) return callfemm("mo_lineintegral",ptype) end
function mo_makeplot(a1,a2,a3) return callfemm("mo_makeplot",a1,a2,a3) end

function mo_maximize() return callfemm("mo_maximize") end
function mo_minimize() return callfemm("mo_minimize") end
function mo_numelements() return callfemm("mo_numelements") end
function mo_numnodes() return callfemm("mo_numnodes") end
function mo_refreshview() return callfemm("mo_refreshview") end
function mo_reload() return callfemm("mo_reload") end
function mo_resize(w,h) return callfemm("mo_resize",w,h) end
function mo_restore() return callfemm("mo_restore") end
function mo_savebitmap(fn) return callfemm("mo_savebitmap",fn) end
function mo_savemetafile(fn) return callfemm("mo_savemetafile",fn) end

function mo_selectblock(x,y) return callfemm("mo_selectblock",x,y) end
function mo_selectpoint(x,y) return callfemm("mo_selectpoint",x,y) end
function mo_seteditmode(mode) return callfemm("mo_seteditmode",mode) end
function mo_setweightingscheme(mode) return callfemm("mo_setweightingscheme",mode) end
function mo_setgrid(density,ptype) return callfemm("mo_setgrid",density,ptype) end

function mo_showcontourplot(numcontours,al,au,ptype) return callfemm("mo_showcontourplot",numcontours,al,au,ptype) end
function mo_showdensityplot(legend,gscale,bu,bl,ptype) return callfemm("mo_showdensityplot",legend,gscale,bu,bl,ptype) end
function mo_showgrid() return callfemm("mo_showgrid") end
function mo_showmesh() return callfemm("mo_showmesh") end
function mo_shownames() return callfemm("mo_shownames",1) end
function mo_showpoints() return callfemm("mo_showpoints") end
function mo_showvectorplot(ptype) return callfemm("mo_showvectorplot",ptype) end

function mo_smooth(flag) return callfemm("mo_smooth",flag) end
function mo_smoothoff() return callfemm("mo_smooth","off") end
function mo_smoothon() return callfemm("mo_smooth","on") end

function mo_zoom(x1,y1,x2,y2) return callfemm("mo_zoom",x1,y1,x2,y2) end
function mo_zoomin() return callfemm("mo_zoomin") end
function mo_zoomnatural() return callfemm("mo_zoomnatural") end
function mo_zoomout() return callfemm("mo_zoomout") end

function mo_getgapb(bdryname,angle) return callfemm("mo_getgapb",bdryname,angle) end
function mo_getgapa(bdryname,angle) return callfemm("mo_getgapa",bdryname,angle) end
function mo_gapintegral(bdryname,inttype) return callfemm("mo_gapintegral",bdryname,inttype) end
function mo_getgapharmonics(a1,a2) return callfemm("mo_getgapharmonics",a1,a2) end

function mo_gettitle() return callfemm("mo_gettitle") end

-- ============================================================
-- ELECTROSTATICS PRE-PROCESSOR (ei_)
-- ============================================================
function ei_addarc(x1,y1,x2,y2,angle,maxseg) return callfemm("ei_addarc",x1,y1,x2,y2,angle,maxseg) end
function ei_addblocklabel(x,y) return callfemm("ei_addblocklabel",x,y) end
function ei_addboundprop(pname,vs,qs,c0,c1,fmt) return callfemm("ei_addboundprop",pname,vs,qs,c0,c1,fmt) end
function ei_addconductorprop(pname,vc,qc,ptype) return callfemm("ei_addconductorprop",pname,vc,qc,ptype) end
function ei_addmaterial(pname,ex,ey,qv) return callfemm("ei_addmaterial",pname,ex,ey,qv) end
function ei_addnode(x,y) return callfemm("ei_addnode",x,y) end
function ei_addpointprop(pname,vp,qp) return callfemm("ei_addpointprop",pname,vp,qp) end
function ei_addsegment(x1,y1,x2,y2) return callfemm("ei_addsegment",x1,y1,x2,y2) end
function ei_analyze(flag) return callfemm("ei_analyze",flag) end
function ei_analyse(flag) return ei_analyze(flag) end
function ei_clearselected() return callfemm("ei_clearselected") end
function ei_close() return callfemm("ei_close") end
function ei_copyrotate(bx,by,angle,copies) return callfemm("ei_copyrotate",bx,by,angle,copies) end
function ei_copytranslate(bx,by,copies) return callfemm("ei_copytranslate",bx,by,copies) end
function ei_createmesh() return callfemm("ei_createmesh") end
function ei_createradius(x,y,r) return callfemm("ei_createradius",x,y,r) end
function ei_deleteboundprop(n) return callfemm("ei_deleteboundprop",n) end
function ei_deleteconductor(n) return callfemm("ei_deleteconductor",n) end
function ei_deletematerial(n) return callfemm("ei_deletematerial",n) end
function ei_deletepointprop(n) return callfemm("ei_deletepointprop",n) end
function ei_deleteselected() return callfemm("ei_deleteselected") end
function ei_loadsolution() return callfemm("ei_loadsolution") end
function ei_mirror(x1,y1,x2,y2) return callfemm("ei_mirror",x1,y1,x2,y2) end
function ei_modifyboundprop(pname,pnum,pval) return callfemm("ei_modifyboundprop",pname,pnum,pval) end
function ei_modifyconductorprop(pname,pnum,pval) return callfemm("ei_modifyconductorprop",pname,pnum,pval) end
function ei_modifymaterial(pname,pnum,pval) return callfemm("ei_modifymaterial",pname,pnum,pval) end
function ei_modifypointprop(pname,pnum,pval) return callfemm("ei_modifypointprop",pname,pnum,pval) end
function ei_movetranslate(bx,by) return callfemm("ei_movetranslate",bx,by) end
function ei_moverotate(bx,by,shiftangle) return callfemm("ei_moverotate",bx,by,shiftangle) end
function ei_probdef(units,ptype,precision,depth,minangle) return callfemm("ei_probdef",units,ptype,precision,depth,minangle) end
function ei_saveas(fn) return callfemm("ei_saveas",fn) end
function ei_selectarcsegment(x,y) return callfemm("ei_selectarcsegment",x,y) end
function ei_selectgroup(gr) return callfemm("ei_selectgroup",gr) end
function ei_selectlabel(x,y) return callfemm("ei_selectlabel",x,y) end
function ei_selectnode(x,y) return callfemm("ei_selectnode",x,y) end
function ei_selectsegment(x,y) return callfemm("ei_selectsegment",x,y) end
function ei_setarcsegmentprop(maxsegdeg,propname,hide,group,incond) return callfemm("ei_setarcsegmentprop",maxsegdeg,propname,hide,group,incond) end
function ei_setblockprop(blockname,automesh,meshsize,group) return callfemm("ei_setblockprop",blockname,automesh,meshsize,group) end
function ei_seteditmode(editmode) return callfemm("ei_seteditmode",editmode) end
function ei_setnodeprop(nodeprop,groupno,inconductor) return callfemm("ei_setnodeprop",nodeprop,groupno,inconductor) end
function ei_setsegmentprop(pn,es,am,hi,gr,inconductor) return callfemm("ei_setsegmentprop",pn,es,am,hi,gr,inconductor) end
function ei_showmesh() return callfemm("ei_showmesh") end
function ei_zoomnatural() return callfemm("ei_zoomnatural") end

-- ============================================================
-- ELECTROSTATICS POST-PROCESSOR (eo_)
-- ============================================================
function eo_addcontour(x,y) return callfemm("eo_addcontour",x,y) end
function eo_bendcontour(tta,dtta) return callfemm("eo_bendcontour",tta,dtta) end
function eo_blockintegral(ptype) return callfemm("eo_blockintegral",ptype) end
function eo_clearblock() return callfemm("eo_clearblock") end
function eo_clearcontour() return callfemm("eo_clearcontour") end
function eo_close() return callfemm("eo_close") end
function eo_getconductorproperties(pname) return callfemm("eo_getconductorproperties",pname) end
function eo_getpointvalues(x,y) return callfemm("eo_getpointvalues",x,y) end
function eo_getelement(n) return callfemm("eo_getelement",n) end
function eo_getnode(n) return callfemm("eo_getnode",n) end
function eo_getprobleminfo() return callfemm("eo_getprobleminfo") end
function eo_groupselectblock(n) return callfemm("eo_groupselectblock",n) end
function eo_lineintegral(ptype) return callfemm("eo_lineintegral",ptype) end
function eo_makeplot(a1,a2,a3) return callfemm("eo_makeplot",a1,a2,a3) end
function eo_selectblock(x,y) return callfemm("eo_selectblock",x,y) end
function eo_selectpoint(x,y) return callfemm("eo_selectpoint",x,y) end
function eo_seteditmode(mode) return callfemm("eo_seteditmode",mode) end
function eo_showdensityplot(legend,gscale,ptype,bu,bl) return callfemm("eo_showdensityplot",legend,gscale,ptype,bu,bl) end
function eo_showcontourplot(numcontours,al,au) return callfemm("eo_showcontourplot",numcontours,al,au) end
function eo_showmesh() return callfemm("eo_showmesh") end
function eo_hidemesh() return callfemm("eo_hidemesh") end
function eo_shownames() return callfemm("eo_shownames",1) end
function eo_hidenames() return callfemm("eo_shownames",0) end
function eo_smooth(flag) return callfemm("eo_smooth",flag) end
function eo_reload() return callfemm("eo_reload") end
function eo_close() return callfemm("eo_close") end
function eo_zoomnatural() return callfemm("eo_zoomnatural") end
function eo_gettitle() return callfemm("eo_gettitle") end

-- ============================================================
-- HEAT FLOW PRE-PROCESSOR (hi_)
-- ============================================================
function hi_addarc(x1,y1,x2,y2,angle,maxseg) return callfemm("hi_addarc",x1,y1,x2,y2,angle,maxseg) end
function hi_addblocklabel(x,y) return callfemm("hi_addblocklabel",x,y) end
function hi_addboundprop(a1,a2,a3,a4,a5,a6,a7,a8) return callfemm("hi_addboundprop",a1,a2,a3,a4,a5,a6,a7,a8) end
function hi_addconductorprop(a1,a2,a3,a4) return callfemm("hi_addconductorprop",a1,a2,a3,a4) end
function hi_addmaterial(a1,a2,a3,a4,a5) return callfemm("hi_addmaterial",a1,a2,a3,a4,a5) end
function hi_addnode(x,y) return callfemm("hi_addnode",x,y) end
function hi_addpointprop(a1,a2,a3) return callfemm("hi_addpointprop",a1,a2,a3) end
function hi_addsegment(x1,y1,x2,y2) return callfemm("hi_addsegment",x1,y1,x2,y2) end
function hi_addtkpoint(name,b,h) return callfemm("hi_addtkpoint",name,b,h) end
function hi_analyze(flag) return callfemm("hi_analyze",flag) end
function hi_analyse(flag) return hi_analyze(flag) end
function hi_clearselected() return callfemm("hi_clearselected") end
function hi_cleartkpoints(n) return callfemm("hi_cleartkpoints",n) end
function hi_close() return callfemm("hi_close") end
function hi_copyrotate(bx,by,angle,copies) return callfemm("hi_copyrotate",bx,by,angle,copies) end
function hi_copytranslate(bx,by,copies) return callfemm("hi_copytranslate",bx,by,copies) end
function hi_createmesh() return callfemm("hi_createmesh") end
function hi_createradius(x,y,r) return callfemm("hi_createradius",x,y,r) end
function hi_deleteboundprop(n) return callfemm("hi_deleteboundprop",n) end
function hi_deletematerial(n) return callfemm("hi_deletematerial",n) end
function hi_deleteselected() return callfemm("hi_deleteselected") end
function hi_loadsolution() return callfemm("hi_loadsolution") end
function hi_mirror(x1,y1,x2,y2) return callfemm("hi_mirror",x1,y1,x2,y2) end
function hi_modifyboundprop(pname,pnum,pval) return callfemm("hi_modifyboundprop",pname,pnum,pval) end
function hi_modifyconductorprop(pname,pnum,pval) return callfemm("hi_modifyconductorprop",pname,pnum,pval) end
function hi_modifymaterial(pname,pnum,pval) return callfemm("hi_modifymaterial",pname,pnum,pval) end
function hi_modifypointprop(pname,pnum,pval) return callfemm("hi_modifypointprop",pname,pnum,pval) end
function hi_movetranslate(bx,by) return callfemm("hi_movetranslate",bx,by) end
function hi_moverotate(bx,by,shiftangle) return callfemm("hi_moverotate",bx,by,shiftangle) end
function hi_probdef(a1,a2,a3,a4,a5,a6,a7) return callfemm("hi_probdef",a1,a2,a3,a4,a5,a6,a7) end
function hi_saveas(fn) return callfemm("hi_saveas",fn) end
function hi_selectarcsegment(x,y) return callfemm("hi_selectarcsegment",x,y) end
function hi_selectgroup(gr) return callfemm("hi_selectgroup",gr) end
function hi_selectlabel(x,y) return callfemm("hi_selectlabel",x,y) end
function hi_selectnode(x,y) return callfemm("hi_selectnode",x,y) end
function hi_selectsegment(x,y) return callfemm("hi_selectsegment",x,y) end
function hi_setarcsegmentprop(maxsegdeg,propname,hide,group,incond) return callfemm("hi_setarcsegmentprop",maxsegdeg,propname,hide,group,incond) end
function hi_setblockprop(blockname,automesh,meshsize,group) return callfemm("hi_setblockprop",blockname,automesh,meshsize,group) end
function hi_seteditmode(editmode) return callfemm("hi_seteditmode",editmode) end
function hi_setnodeprop(nodeprop,groupno,inconductor) return callfemm("hi_setnodeprop",nodeprop,groupno,inconductor) end
function hi_setsegmentprop(pn,es,am,hi_val,gr,inconductor) return callfemm("hi_setsegmentprop",pn,es,am,hi_val,gr,inconductor) end
function hi_showmesh() return callfemm("hi_showmesh") end
function hi_zoomnatural() return callfemm("hi_zoomnatural") end

-- ============================================================
-- HEAT FLOW POST-PROCESSOR (ho_)
-- ============================================================
function ho_addcontour(x,y) return callfemm("ho_addcontour",x,y) end
function ho_bendcontour(tta,dtta) return callfemm("ho_bendcontour",tta,dtta) end
function ho_blockintegral(ptype) return callfemm("ho_blockintegral",ptype) end
function ho_clearblock() return callfemm("ho_clearblock") end
function ho_clearcontour() return callfemm("ho_clearcontour") end
function ho_close() return callfemm("ho_close") end
function ho_getconductorproperties(pname) return callfemm("ho_getconductorproperties",pname) end
function ho_getpointvalues(x,y) return callfemm("ho_getpointvalues",x,y) end
function ho_getelement(n) return callfemm("ho_getelement",n) end
function ho_getnode(n) return callfemm("ho_getnode",n) end
function ho_getprobleminfo() return callfemm("ho_getprobleminfo") end
function ho_groupselectblock(n) return callfemm("ho_groupselectblock",n) end
function ho_lineintegral(ptype) return callfemm("ho_lineintegral",ptype) end
function ho_makeplot(a1,a2,a3) return callfemm("ho_makeplot",a1,a2,a3) end
function ho_selectblock(x,y) return callfemm("ho_selectblock",x,y) end
function ho_selectpoint(x,y) return callfemm("ho_selectpoint",x,y) end
function ho_seteditmode(mode) return callfemm("ho_seteditmode",mode) end
function ho_showdensityplot(legend,gscale,ptype,bu,bl) return callfemm("ho_showdensityplot",legend,gscale,ptype,bu,bl) end
function ho_showcontourplot(numcontours,al,au) return callfemm("ho_showcontourplot",numcontours,al,au) end
function ho_showmesh() return callfemm("ho_showmesh") end
function ho_hidemesh() return callfemm("ho_hidemesh") end
function ho_shownames() return callfemm("ho_shownames",1) end
function ho_hidenames() return callfemm("ho_shownames",0) end
function ho_smooth(flag) return callfemm("ho_smooth",flag) end
function ho_reload() return callfemm("ho_reload") end
function ho_zoomnatural() return callfemm("ho_zoomnatural") end
function ho_gettitle() return callfemm("ho_gettitle") end

-- ============================================================
-- CURRENT FLOW PRE-PROCESSOR (ci_)
-- ============================================================
function ci_addarc(x1,y1,x2,y2,angle,maxseg) return callfemm("ci_addarc",x1,y1,x2,y2,angle,maxseg) end
function ci_addblocklabel(x,y) return callfemm("ci_addblocklabel",x,y) end
function ci_addboundprop(a1,a2,a3,a4,a5,a6,a7,a8) return callfemm("ci_addboundprop",a1,a2,a3,a4,a5,a6,a7,a8) end
function ci_addconductorprop(a1,a2,a3,a4) return callfemm("ci_addconductorprop",a1,a2,a3,a4) end
function ci_addmaterial(a1,a2,a3,a4,a5,a6,a7,a8,a9,a10) return callfemm("ci_addmaterial",a1,a2,a3,a4,a5,a6,a7,a8,a9,a10) end
function ci_addnode(x,y) return callfemm("ci_addnode",x,y) end
function ci_addpointprop(a1,a2,a3) return callfemm("ci_addpointprop",a1,a2,a3) end
function ci_addsegment(x1,y1,x2,y2) return callfemm("ci_addsegment",x1,y1,x2,y2) end
function ci_analyze(flag) return callfemm("ci_analyze",flag) end
function ci_analyse(flag) return ci_analyze(flag) end
function ci_clearselected() return callfemm("ci_clearselected") end
function ci_close() return callfemm("ci_close") end
function ci_copyrotate(bx,by,angle,copies) return callfemm("ci_copyrotate",bx,by,angle,copies) end
function ci_copytranslate(bx,by,copies) return callfemm("ci_copytranslate",bx,by,copies) end
function ci_createmesh() return callfemm("ci_createmesh") end
function ci_createradius(x,y,r) return callfemm("ci_createradius",x,y,r) end
function ci_deleteboundprop(n) return callfemm("ci_deleteboundprop",n) end
function ci_deletematerial(n) return callfemm("ci_deletematerial",n) end
function ci_deleteselected() return callfemm("ci_deleteselected") end
function ci_loadsolution() return callfemm("ci_loadsolution") end
function ci_mirror(x1,y1,x2,y2) return callfemm("ci_mirror",x1,y1,x2,y2) end
function ci_modifyboundprop(pname,pnum,pval) return callfemm("ci_modifyboundprop",pname,pnum,pval) end
function ci_modifyconductorprop(pname,pnum,pval) return callfemm("ci_modifyconductorprop",pname,pnum,pval) end
function ci_modifymaterial(pname,pnum,pval) return callfemm("ci_modifymaterial",pname,pnum,pval) end
function ci_modifypointprop(pname,pnum,pval) return callfemm("ci_modifypointprop",pname,pnum,pval) end
function ci_movetranslate(bx,by) return callfemm("ci_movetranslate",bx,by) end
function ci_moverotate(bx,by,shiftangle) return callfemm("ci_moverotate",bx,by,shiftangle) end
function ci_probdef(units,ptype,frequency,precision,depth,minangle) return callfemm("ci_probdef",units,ptype,frequency,precision,depth,minangle) end
function ci_saveas(fn) return callfemm("ci_saveas",fn) end
function ci_selectarcsegment(x,y) return callfemm("ci_selectarcsegment",x,y) end
function ci_selectgroup(gr) return callfemm("ci_selectgroup",gr) end
function ci_selectlabel(x,y) return callfemm("ci_selectlabel",x,y) end
function ci_selectnode(x,y) return callfemm("ci_selectnode",x,y) end
function ci_selectsegment(x,y) return callfemm("ci_selectsegment",x,y) end
function ci_setarcsegmentprop(maxsegdeg,propname,hide,group,incond) return callfemm("ci_setarcsegmentprop",maxsegdeg,propname,hide,group,incond) end
function ci_setblockprop(blockname,automesh,meshsize,group) return callfemm("ci_setblockprop",blockname,automesh,meshsize,group) end
function ci_seteditmode(editmode) return callfemm("ci_seteditmode",editmode) end
function ci_setnodeprop(nodeprop,groupno,inconductor) return callfemm("ci_setnodeprop",nodeprop,groupno,inconductor) end
function ci_setsegmentprop(pn,es,am,hi_val,gr,inconductor) return callfemm("ci_setsegmentprop",pn,es,am,hi_val,gr,inconductor) end
function ci_showmesh() return callfemm("ci_showmesh") end
function ci_zoomnatural() return callfemm("ci_zoomnatural") end

-- ============================================================
-- CURRENT FLOW POST-PROCESSOR (co_)
-- ============================================================
function co_addcontour(x,y) return callfemm("co_addcontour",x,y) end
function co_bendcontour(tta,dtta) return callfemm("co_bendcontour",tta,dtta) end
function co_blockintegral(ptype) return callfemm("co_blockintegral",ptype) end
function co_clearblock() return callfemm("co_clearblock") end
function co_clearcontour() return callfemm("co_clearcontour") end
function co_close() return callfemm("co_close") end
function co_getconductorproperties(pname) return callfemm("co_getconductorproperties",pname) end
function co_getpointvalues(x,y) return callfemm("co_getpointvalues",x,y) end
function co_getelement(n) return callfemm("co_getelement",n) end
function co_getnode(n) return callfemm("co_getnode",n) end
function co_getprobleminfo() return callfemm("co_getprobleminfo") end
function co_groupselectblock(n) return callfemm("co_groupselectblock",n) end
function co_lineintegral(ptype) return callfemm("co_lineintegral",ptype) end
function co_makeplot(a1,a2,a3) return callfemm("co_makeplot",a1,a2,a3) end
function co_selectblock(x,y) return callfemm("co_selectblock",x,y) end
function co_selectpoint(x,y) return callfemm("co_selectpoint",x,y) end
function co_seteditmode(mode) return callfemm("co_seteditmode",mode) end
function co_showdensityplot(legend,gscale,ptype,bu,bl) return callfemm("co_showdensityplot",legend,gscale,ptype,bu,bl) end
function co_showcontourplot(numcontours,al,au) return callfemm("co_showcontourplot",numcontours,al,au) end
function co_showmesh() return callfemm("co_showmesh") end
function co_hidemesh() return callfemm("co_hidemesh") end
function co_shownames() return callfemm("co_shownames",1) end
function co_hidenames() return callfemm("co_shownames",0) end
function co_smooth(flag) return callfemm("co_smooth",flag) end
function co_reload() return callfemm("co_reload") end
function co_zoomnatural() return callfemm("co_zoomnatural") end
function co_gettitle() return callfemm("co_gettitle") end

-- Unit Definitions

uo  = PI*4.e-7
muo = uo
eo  = 8.8541878188e-12
Pi  = PI; pi = PI

meter = 1
meters = meter; Meter = meter; Meters = meter
inch = 0.0254
inches = inch; Inch = inch; Inches = inch
cm = 0.01
centimeter = cm; centimeters = cm; Centimeters = cm; Centimeter = cm
mm = 0.001
millimeters = mm; millimeter = mm; Millimeter = mm; Millimeters = mm
milimeter = mm; milimeters = mm
um = 0.000001
micrometer = um; Micrometer = um; micrometers = um; Micrometers = um
micron = um; Micron = um; microns = um; Microns = um
mil = 0.001*inch
mils = mil; Mils = mil; Mil = mil

Tesla     = 1
mTesla    = 0.001
Gauss     = 0.0001
kGauss    = 0.1
AmpMeter  = 1
kAmpMeter = 1000
Oersted   = 250/PI
kOersted  = 1000*Oersted