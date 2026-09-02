# SPDX-License-Identifier: MIT
# Copyright (C) 2026 David Meeker and contributors
# femm.py -- pyFEMM interface for femm-qt
# Development version using TCP IPC instead of ActiveX/COM.
#
# Single-instance usage (backward compatible):
#   from femm import *
#   openfemm()
#   mi_addnode(0, 0)
#
# Multi-instance usage:
#   from femm import FemmSession
#   f1 = FemmSession(port=48015)
#   f2 = FemmSession(port=48016)
#   f1.mi_addnode(0, 0)
#   f2.mi_addnode(1, 1)

import socket
import subprocess
import os
import math
import time


def _fixpath(p):
    return p.replace('\\', '/').replace('//', '/')


def _find_exe(exename):
    """Find the FEMM executable by checking FEMM_DIR, then relative paths.

    In each directory, try every platform's executable form; the first one
    that exists wins, so there's no need to branch on the OS. The third form
    is the macOS application bundle: the GUI builds as <exename>.app, a
    directory whose actual executable is at Contents/MacOS/<exename>. That
    path only exists on macOS, so it is harmlessly skipped elsewhere.
    """
    exedir = os.path.dirname(os.path.abspath(__file__))
    searchdirs = []
    femm_dir = os.environ.get('FEMM_DIR')
    if femm_dir:
        searchdirs.append(femm_dir)
    searchdirs.append(os.path.join(exedir, '..', 'build', 'bin'))
    searchdirs.append(exedir)
    # Last resort: every directory on PATH. An install puts femm_gui and
    # femm_headless there -- symlinks in ~/.local/bin on Unix, {app}\bin
    # on Windows -- so this is what lets the bindings work with no
    # FEMM_DIR set at all. It goes LAST so an explicit FEMM_DIR, or a
    # source tree being worked in, still wins.
    searchdirs.extend(
        d for d in os.environ.get('PATH', '').split(os.pathsep) if d)
    # macOS keeps programs in /Applications, not on PATH, so the PATH
    # search above cannot find an installed bundle -- and the bundle is
    # where a Mac user puts FEMM. These two directories are that
    # platform's equivalent of a PATH entry; the
    # femm_gui.app/Contents/MacOS/ form tried in every search directory is
    # what reaches inside. Added unconditionally rather than behind an
    # is-this-a-Mac test: on Windows and Linux they simply do not exist,
    # so the check costs two failed lookups, and five languages means five
    # chances to get five different platform tests wrong.
    searchdirs.append('/Applications')
    searchdirs.append(os.path.expanduser('~/Applications'))
    candidates = []
    for d in searchdirs:
        candidates.append(os.path.join(d, exename + '.exe'))                          # Windows
        candidates.append(os.path.join(d, 'femm_gui.app', 'Contents', 'MacOS', exename))  # macOS: inside the bundle
        candidates.append(os.path.join(d, exename))                                   # Linux / macOS CLI
        candidates.append(os.path.join(d, exename + '.app', 'Contents', 'MacOS', exename))  # macOS .app
    for c in candidates:
        if os.path.isfile(c):
            return os.path.abspath(c)
    return None


def num(n):
    return str(n).replace('j', '*I')

def numc(n):
    return num(n) + ','

def quote(s):
    return '"' + s + '"'

def quotec(s):
    return '"' + s + '",'

def doargs(*arg):
    if len(arg) == 0:
        return '()'
    x = '('
    for k in range(len(arg)):
        y = arg[k]
        if isinstance(y, str):
            x += '"' + y + '"'
        else:
            x += num(y)
        if k == len(arg) - 1:
            x += ')'
        else:
            x += ','
    return x

def AWG(awg):
    return 8.2514694 * math.exp(-0.115943 * awg)

def IEC(iec):
    return 7.959159641581004 * math.exp(-0.11519673572274754 * iec)


# ================================================================
# FemmSession — one connection to one FEMM instance
# ================================================================

class FemmSession:
    """A connection to a single femm-qt (or femm_headless) instance."""

    def __init__(self, port=48015, host='127.0.0.1', headless=False, launch=True):
        """Connect to a FEMM instance.

        Args:
            port: TCP port number (default 48015)
            host: hostname (default localhost)
            headless: if True and launching, use femm_headless instead of femm_gui
            launch: if True, launch FEMM if not already running on this port
        """
        self._sock = None
        self._proc = None
        self._port = port
        self._host = host

        self._sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self._sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        self._sock.settimeout(0.1)  # 100ms timeout — avoids ~2s OS default
        try:
            self._sock.connect((host, port))
            self._sock.settimeout(None)  # back to blocking for normal I/O
        except (ConnectionRefusedError, socket.timeout, OSError):
            self._sock.close()
            self._sock = None
            if not launch:
                raise RuntimeError(f"No FEMM instance on {host}:{port}")

            # On Windows spawn the windowless build (femm_headlessw) so no
            # console window flashes; the console femm_headless is for
            # interactive terminal / batch use. Other platforms have only
            # femm_headless (no subsystem distinction).
            if headless:
                exename = 'femm_headlessw' if os.name == 'nt' else 'femm_headless'
            else:
                exename = 'femm_gui'
            exe = _find_exe(exename)
            if exe is None:
                raise RuntimeError(
                    f"Cannot find {exename} executable. "
                    "Set the FEMM_DIR environment variable to the directory containing it."
                )

            self._proc = subprocess.Popen(
                [exe, '--port', str(port)],
                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL
            )

            for _ in range(50):
                time.sleep(0.1)
                self._sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                self._sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
                self._sock.settimeout(0.1)  # 100ms connect timeout — avoids ~2s OS default
                try:
                    self._sock.connect((host, port))
                    self._sock.settimeout(None)  # back to blocking for normal I/O
                    break
                except (ConnectionRefusedError, socket.timeout, OSError):
                    self._sock.close()
                    self._sock = None
            if self._sock is None:
                raise RuntimeError("FEMM launched but TCP connection failed")

        self.callfemm('setcurrentdirectory(' + quote(_fixpath(os.getcwd())) + ')')

    def close(self):
        """Close connection to this FEMM instance."""
        try:
            self.callfemm_noeval('quit()')
        except:
            pass
        if self._sock:
            self._sock.close()
            self._sock = None
        self._proc = None

    def callfemm(self, myString):
        """Send a command and return the parsed result."""
        if self._sock is None:
            raise RuntimeError("Not connected to FEMM.")

        # Escape backslashes for the command interpreter's string-literal parser. Without this,
        # a Windows path embedded in a command like
        # mi_saveas("C:\foo\bar") gets corrupted: \f becomes form-feed, \b
        # backspace, etc. Doubling every \ to \\ makes the parser yield
        # the original path byte-for-byte. Matches the canonical
        # David-Meeker pattern from c:/femm42/mathfemm/mathfemm.m:3145.
        myString = myString.replace('\\', '\\\\')

        self._sock.sendall((myString + '\n').encode('utf-8'))

        buf = b''
        while True:
            chunk = self._sock.recv(4096)
            if not chunk:
                raise RuntimeError("Connection to FEMM lost")
            buf += chunk
            if b'\n' in buf:
                break

        response = buf[:buf.index(b'\n')].decode('utf-8')

        if response.startswith('error:'):
            raise Exception('FEMM returns:\n' + response[6:])

        if len(response) == 0:
            return []

        parts = response.split('\t')
        results = []
        for p in parts:
            p = p.strip()
            if len(p) == 0:
                continue
            p = p.replace('I', '1j').replace('i', '1j')
            try:
                results.append(eval(p))
            except:
                results.append(p)

        if len(results) == 1:
            return results[0]
        return results

    def callfemm_noeval(self, myString):
        """Send a command without parsing the result."""
        if self._sock is None:
            raise RuntimeError("Not connected to FEMM.")
        # Escape backslashes for the command parser — see callfemm for the rationale.
        myString = myString.replace('\\', '\\\\')
        self._sock.sendall((myString + '\n').encode('utf-8'))
        buf = b''
        while True:
            chunk = self._sock.recv(4096)
            if not chunk:
                break
            buf += chunk
            if b'\n' in buf:
                break

    def newdocument(self, k): self.callfemm('newdocument(' + num(k) + ')')

    # ---- mi_* (magnetics preprocessor) ----
    def mi_addnode(self, *arg): self.callfemm('mi_addnode' + doargs(*arg))
    def mi_addsegment(self, *arg): self.callfemm('mi_addsegment' + doargs(*arg))
    def mi_addarc(self, *arg): self.callfemm('mi_addarc' + doargs(*arg))
    def mi_addblocklabel(self, *arg): self.callfemm('mi_addblocklabel' + doargs(*arg))
    def mi_addmaterial(self, *arg): self.callfemm('mi_addmaterial' + doargs(*arg))
    def mi_addboundprop(self, *arg): self.callfemm('mi_addboundprop' + doargs(*arg))
    def mi_addpointprop(self, *arg): self.callfemm('mi_addpointprop' + doargs(*arg))
    def mi_addcircprop(self, *arg): self.callfemm('mi_addcircprop' + doargs(*arg))
    def mi_addbhpoint(self, *arg): self.callfemm('mi_addbhpoint' + doargs(*arg))
    def mi_addbhpoints(self, name, bh, h=None):
        """Add a whole B-H table. Either mi_addbhpoints(name, [[b,h],...]) or
        mi_addbhpoints(name, blist, hlist). Mirrors octavefemm's wrapper: there
        is no bulk command on the wire, so this loops mi_addbhpoint."""
        if h is not None:
            bh = list(zip(bh, h))
        for row in bh:
            self.mi_addbhpoint(name, row[0], row[1])

    def mi_clearbhpoints(self, *arg): self.callfemm('mi_clearbhpoints' + doargs(*arg))
    def mi_clearselected(self): self.callfemm('mi_clearselected()')
    def mi_selectnode(self, *arg): return self.callfemm('mi_selectnode' + doargs(*arg))
    def mi_selectsegment(self, *arg): return self.callfemm('mi_selectsegment' + doargs(*arg))
    def mi_selectarcsegment(self, *arg): return self.callfemm('mi_selectarcsegment' + doargs(*arg))
    def mi_selectlabel(self, *arg): return self.callfemm('mi_selectlabel' + doargs(*arg))
    def mi_selectgroup(self, *arg): self.callfemm('mi_selectgroup' + doargs(*arg))
    def mi_selectrectangle(self, *arg): self.callfemm('mi_selectrectangle' + doargs(*arg))
    def mi_selectcircle(self, *arg): self.callfemm('mi_selectcircle' + doargs(*arg))
    def mi_setnodeprop(self, *arg): self.callfemm('mi_setnodeprop' + doargs(*arg))
    def mi_setblockprop(self, *arg): self.callfemm('mi_setblockprop' + doargs(*arg))
    def mi_setsegmentprop(self, *arg): self.callfemm('mi_setsegmentprop' + doargs(*arg))
    def mi_setarcsegmentprop(self, *arg): self.callfemm('mi_setarcsegmentprop' + doargs(*arg))
    def mi_setgroup(self, *arg): self.callfemm('mi_setgroup' + doargs(*arg))
    def mi_setcurrent(self, name, x): self.mi_modifycircprop(name, 1, x)
    def mi_deleteselected(self): self.callfemm('mi_deleteselected()')
    def mi_deleteselectednodes(self): self.callfemm('mi_deleteselectednodes()')
    def mi_deleteselectedlabels(self): self.callfemm('mi_deleteselectedlabels()')
    def mi_deleteselectedsegments(self): self.callfemm('mi_deleteselectedsegments()')
    def mi_deleteselectedarcsegments(self): self.callfemm('mi_deleteselectedarcsegments()')
    def mi_movetranslate(self, *arg): self.callfemm('mi_movetranslate' + doargs(*arg))
    def mi_moverotate(self, *arg): self.callfemm('mi_moverotate' + doargs(*arg))
    def mi_copytranslate(self, *arg): self.callfemm('mi_copytranslate' + doargs(*arg))
    def mi_copyrotate(self, *arg): self.callfemm('mi_copyrotate' + doargs(*arg))
    def mi_mirror(self, *arg): self.callfemm('mi_mirror' + doargs(*arg))
    def mi_scale(self, *arg): self.callfemm('mi_scale' + doargs(*arg))
    def mi_modifymaterial(self, *arg): self.callfemm('mi_modifymaterial' + doargs(*arg))
    def mi_modifyboundprop(self, *arg): self.callfemm('mi_modifyboundprop' + doargs(*arg))
    def mi_modifypointprop(self, *arg): self.callfemm('mi_modifypointprop' + doargs(*arg))
    def mi_modifycircprop(self, *arg): self.callfemm('mi_modifycircprop' + doargs(*arg))
    def mi_deletematerial(self, *arg): self.callfemm('mi_deletematerial' + doargs(*arg))
    def mi_deleteboundprop(self, *arg): self.callfemm('mi_deleteboundprop' + doargs(*arg))
    def mi_deletepointprop(self, *arg): self.callfemm('mi_deletepointprop' + doargs(*arg))
    def mi_deletecircuit(self, *arg): self.callfemm('mi_deletecircuit' + doargs(*arg))
    def mi_probdef(self, *arg): self.callfemm('mi_probdef' + doargs(*arg))
    def mi_saveas(self, *arg): self.callfemm('mi_saveas' + doargs(*arg))
    def mi_analyze(self, *arg): self.callfemm('mi_analyze' + doargs(*arg))
    def mi_analyse(self, *arg): self.mi_analyze(*arg)
    def mi_loadsolution(self): self.callfemm('mi_loadsolution()')
    def mi_createmesh(self): self.callfemm('mi_createmesh()')
    def mi_smartmesh(self, *arg): self.callfemm('mi_smartmesh' + doargs(*arg))
    def mi_getmaterial(self, *arg): self.callfemm('mi_getmaterial' + doargs(*arg))
    def mi_getprobleminfo(self): return self.callfemm('mi_getprobleminfo()')
    def mi_getboundingbox(self): return self.callfemm('mi_getboundingbox()')
    def mi_defineouterspace(self, *arg): self.callfemm('mi_defineouterspace' + doargs(*arg))
    def mi_attachouterspace(self): self.callfemm('mi_attachouterspace()')
    def mi_detachouterspace(self): self.callfemm('mi_detachouterspace()')
    def mi_attachdefault(self): self.callfemm('mi_attachdefault()')
    def mi_detachdefault(self): self.callfemm('mi_detachdefault()')
    def mi_makeABC(self, *arg): self.callfemm('mi_makeABC' + doargs(*arg))
    def mi_createradius(self, *arg): self.callfemm('mi_createradius' + doargs(*arg))
    def mi_seteditmode(self, *arg): self.callfemm('mi_seteditmode' + doargs(*arg))
    def mi_setcomment(self, *arg): self.callfemm('mi_setcomment' + doargs(*arg))
    def mi_setprevious(self, *arg): self.callfemm('mi_setprevious' + doargs(*arg))
    def mi_readdxf(self, *arg): self.callfemm('mi_readdxf' + doargs(*arg))
    def mi_savedxf(self, *arg): self.callfemm('mi_savedxf' + doargs(*arg))
    def mi_newdocument(self): self.callfemm('mi_newdocument()')
    def mi_close(self): self.callfemm('mi_close()')
    def mi_refreshview(self): self.callfemm('mi_refreshview()')
    def mi_zoomnatural(self): self.callfemm('mi_zoomnatural()')
    def mi_zoomin(self): self.callfemm('mi_zoomin()')
    def mi_zoomout(self): self.callfemm('mi_zoomout()')
    def mi_zoom(self, *arg): self.callfemm('mi_zoom' + doargs(*arg))
    def mi_showgrid(self): self.callfemm('mi_showgrid()')
    def mi_hidegrid(self): self.callfemm('mi_hidegrid()')
    def mi_gridsnap(self, *arg): self.callfemm('mi_gridsnap' + doargs(*arg))
    def mi_setgrid(self, *arg): self.callfemm('mi_setgrid' + doargs(*arg))
    def mi_showmesh(self): self.callfemm('mi_showmesh()')
    def mi_purgemesh(self): self.callfemm('mi_purgemesh()')
    def mi_savebitmap(self, *arg): self.callfemm('mi_savebitmap' + doargs(*arg))
    def mi_savemetafile(self, *arg): self.callfemm('mi_savemetafile' + doargs(*arg))
    def mi_shownames(self, *arg): self.callfemm('mi_shownames' + doargs(*arg))
    def mi_hidenames(self): self.callfemm('mi_shownames(0)')
    def mi_snapgridoff(self): self.callfemm('mi_gridsnap("off")')
    def mi_snapgridon(self): self.callfemm('mi_gridsnap("on")')
    def mi_resize(self, *arg): self.callfemm('mi_resize' + doargs(*arg))
    def mi_minimize(self): self.callfemm('mi_minimize()')
    def mi_maximize(self): self.callfemm('mi_maximize()')
    def mi_restore(self): self.callfemm('mi_restore()')
    def mi_setfocus(self, *arg): self.callfemm('mi_setfocus' + doargs(*arg))
    def mi_gettitle(self): return self.callfemm('mi_gettitle()')

    def mi_drawline(self, x1, y1, x2, y2):
        self.mi_addnode(x1, y1); self.mi_addnode(x2, y2)
        self.mi_addsegment(x1, y1, x2, y2)

    def mi_drawarc(self, x1, y1, x2, y2, angle, maxseg):
        self.mi_addnode(x1, y1); self.mi_addnode(x2, y2)
        self.mi_addarc(x1, y1, x2, y2, angle, maxseg)

    def mi_drawrectangle(self, x1, y1, x2, y2):
        self.mi_drawline(x1, y1, x2, y1); self.mi_drawline(x2, y1, x2, y2)
        self.mi_drawline(x2, y2, x1, y2); self.mi_drawline(x1, y2, x1, y1)

    # ---- mo_* (magnetics postprocessor) ----
    def mo_getpointvalues(self, *arg): return self.callfemm('mo_getpointvalues' + doargs(*arg))
    def mo_blockintegral(self, *arg): return self.callfemm('mo_blockintegral' + doargs(*arg))
    def mo_lineintegral(self, *arg): return self.callfemm('mo_lineintegral' + doargs(*arg))
    def mo_getprobleminfo(self): return self.callfemm('mo_getprobleminfo()')
    def mo_gettitle(self): return self.callfemm('mo_gettitle()')
    def mo_getcircuitproperties(self, *arg): return self.callfemm('mo_getcircuitproperties' + doargs(*arg))
    def mo_addcontour(self, *arg): self.callfemm('mo_addcontour' + doargs(*arg))
    def mo_bendcontour(self, *arg): self.callfemm('mo_bendcontour' + doargs(*arg))
    def mo_clearcontour(self): self.callfemm('mo_clearcontour()')
    def mo_selectblock(self, *arg): self.callfemm('mo_selectblock' + doargs(*arg))
    def mo_groupselectblock(self, *arg): self.callfemm('mo_groupselectblock' + doargs(*arg))
    def mo_clearblock(self): self.callfemm('mo_clearblock()')
    def mo_makeplot(self, *arg): return self.callfemm('mo_makeplot' + doargs(*arg))
    def mo_getgapb(self, *arg): return self.callfemm('mo_getgapb' + doargs(*arg))
    def mo_getgapa(self, *arg): return self.callfemm('mo_getgapa' + doargs(*arg))
    def mo_gapintegral(self, *arg): return self.callfemm('mo_gapintegral' + doargs(*arg))
    def mo_getgapharmonics(self, *arg): return self.callfemm('mo_getgapharmonics' + doargs(*arg))
    def mo_getallelements(self):
        raw = self.callfemm('mo_getallelements()')
        return [raw[i:i+7] for i in range(0, len(raw), 7)]
    def mo_getallnodes(self):
        raw = self.callfemm('mo_getallnodes()')
        return [raw[i:i+2] for i in range(0, len(raw), 2)]
    def mo_getallb(self):
        raw = self.callfemm('mo_getallb()')
        return [raw[i:i+2] for i in range(0, len(raw), 2)]
    def mo_getalla(self):
        raw = self.callfemm('mo_getalla()')
        return raw if isinstance(raw, list) else [raw]
    def eo_getallnodes(self):
        raw = self.callfemm('eo_getallnodes()')
        return [raw[i:i+2] for i in range(0, len(raw), 2)]
    def eo_getallelements(self):
        raw = self.callfemm('eo_getallelements()')
        return [raw[i:i+7] for i in range(0, len(raw), 7)]
    def eo_getalld(self):
        raw = self.callfemm('eo_getalld()')
        return [raw[i:i+2] for i in range(0, len(raw), 2)]
    def eo_getallv(self):
        raw = self.callfemm('eo_getallv()')
        return raw if isinstance(raw, list) else [raw]
    def ho_getallnodes(self):
        raw = self.callfemm('ho_getallnodes()')
        return [raw[i:i+2] for i in range(0, len(raw), 2)]
    def ho_getallelements(self):
        raw = self.callfemm('ho_getallelements()')
        return [raw[i:i+7] for i in range(0, len(raw), 7)]
    def ho_getallf(self):
        raw = self.callfemm('ho_getallf()')
        return [raw[i:i+2] for i in range(0, len(raw), 2)]
    def ho_getallt(self):
        raw = self.callfemm('ho_getallt()')
        return raw if isinstance(raw, list) else [raw]
    def co_getallnodes(self):
        raw = self.callfemm('co_getallnodes()')
        return [raw[i:i+2] for i in range(0, len(raw), 2)]
    def co_getallelements(self):
        raw = self.callfemm('co_getallelements()')
        return [raw[i:i+7] for i in range(0, len(raw), 7)]
    def co_getallj(self):
        raw = self.callfemm('co_getallj()')
        return [raw[i:i+2] for i in range(0, len(raw), 2)]
    def co_getallv(self):
        raw = self.callfemm('co_getallv()')
        return raw if isinstance(raw, list) else [raw]
    def mo_getallgapb(self, *arg):
        raw = self.callfemm('mo_getallgapb' + doargs(*arg))
        return [raw[i:i+2] for i in range(0, len(raw), 2)]
    def mo_getallgapa(self, *arg):
        raw = self.callfemm('mo_getallgapa' + doargs(*arg))
        return raw if isinstance(raw, list) else [raw]
    def mo_getallgapharmonics(self, *arg):
        raw = self.callfemm('mo_getallgapharmonics' + doargs(*arg))
        return [raw[i:i+6] for i in range(0, len(raw), 6)]
    def mo_close(self): self.callfemm('mo_close()')
    def mo_showmesh(self): self.callfemm('mo_showmesh()')
    def mo_hidemesh(self): self.callfemm('mo_hidemesh()')
    def mo_showdensityplot(self, *arg): self.callfemm('mo_showdensityplot' + doargs(*arg))
    def mo_hidedensityplot(self): self.callfemm('mo_hidedensityplot()')
    def mo_showcontourplot(self, *arg): self.callfemm('mo_showcontourplot' + doargs(*arg))
    def mo_hidecontourplot(self): self.callfemm('mo_hidecontourplot()')
    def mo_showvectorplot(self, *arg): self.callfemm('mo_showvectorplot' + doargs(*arg))
    def mo_smooth(self, *arg): self.callfemm('mo_smooth' + doargs(*arg))
    def mo_smoothon(self): self.callfemm('mo_smooth("on")')
    def mo_smoothoff(self): self.callfemm('mo_smooth("off")')
    def mo_refreshview(self): self.callfemm('mo_refreshview()')
    def mo_savebitmap(self, *arg): self.callfemm('mo_savebitmap' + doargs(*arg))
    def mo_savemetafile(self, *arg): self.callfemm('mo_savemetafile' + doargs(*arg))
    def mo_shownames(self, *arg): self.callfemm('mo_shownames' + doargs(*arg))
    def mo_hidenames(self): self.callfemm('mo_shownames(0)')
    def mo_showgrid(self): self.callfemm('mo_showgrid()')
    def mo_hidegrid(self): self.callfemm('mo_hidegrid()')
    def mo_setgrid(self, *arg): self.callfemm('mo_setgrid' + doargs(*arg))
    def mo_showpoints(self): self.callfemm('mo_showpoints()')
    def mo_hidepoints(self): self.callfemm('mo_hidepoints()')
    def mo_reload(self): self.callfemm('mo_reload()')
    def mo_resize(self, *arg): self.callfemm('mo_resize' + doargs(*arg))
    def mo_minimize(self): self.callfemm('mo_minimize()')
    def mo_maximize(self): self.callfemm('mo_maximize()')
    def mo_restore(self): self.callfemm('mo_restore()')
    def mo_seteditmode(self, *arg): self.callfemm('mo_seteditmode' + doargs(*arg))
    def mo_selectpoint(self, *arg): return self.callfemm('mo_selectpoint' + doargs(*arg))
    def mo_numnodes(self): return self.callfemm('mo_numnodes()')
    def mo_numelements(self): return self.callfemm('mo_numelements()')
    def mo_getnode(self, *arg): return self.callfemm('mo_getnode' + doargs(*arg))
    def mo_getelement(self, *arg): return self.callfemm('mo_getelement' + doargs(*arg))
    def mo_setweightingscheme(self, *arg): self.callfemm('mo_setweightingscheme' + doargs(*arg))
    def mo_snapgrid(self, *arg): self.callfemm('mo_gridsnap' + doargs(*arg))
    def mo_snapgridon(self): self.callfemm('mo_gridsnap("on")')
    def mo_gridsnap(self, *arg): self.callfemm('mo_gridsnap' + doargs(*arg))
    def mo_snapgridoff(self): self.callfemm('mo_gridsnap("off")')
    def mo_zoom(self, *arg): self.callfemm('mo_zoom' + doargs(*arg))
    def mo_zoomin(self): self.callfemm('mo_zoomin()')
    def mo_zoomout(self): self.callfemm('mo_zoomout()')
    def mo_zoomnatural(self): self.callfemm('mo_zoomnatural()')

    # mo_* convenience point value extractors
    # mo_getpointvalues returns: [A, B1, B2, sig, e, H1, H2, Je, Js, mu1, mu2, Pe, Ph, fill]
    def mo_geta(self, x, y):
        z = self.mo_getpointvalues(x, y); return z[0]
    def mo_getb(self, x, y):
        z = self.mo_getpointvalues(x, y); return [z[1], z[2]]
    def mo_getconductivity(self, x, y):
        z = self.mo_getpointvalues(x, y); return z[3]
    def mo_getenergydensity(self, x, y):
        z = self.mo_getpointvalues(x, y); return z[4]
    def mo_geth(self, x, y):
        z = self.mo_getpointvalues(x, y); return [z[5], z[6]]
    def mo_getj(self, x, y):
        z = self.mo_getpointvalues(x, y); return z[7] + z[8]
    def mo_getmu(self, x, y):
        z = self.mo_getpointvalues(x, y); return [z[9], z[10]]
    def mo_getpe(self, x, y):
        z = self.mo_getpointvalues(x, y); return z[11]
    def mo_getph(self, x, y):
        z = self.mo_getpointvalues(x, y); return z[12]
    def mo_getfill(self, x, y):
        z = self.mo_getpointvalues(x, y); return z[13]

    # ---- ei_* (electrostatics preprocessor) ----
    def ei_addnode(self, *arg): self.callfemm('ei_addnode' + doargs(*arg))
    def ei_addsegment(self, *arg): self.callfemm('ei_addsegment' + doargs(*arg))
    def ei_addarc(self, *arg): self.callfemm('ei_addarc' + doargs(*arg))
    def ei_addblocklabel(self, *arg): self.callfemm('ei_addblocklabel' + doargs(*arg))
    def ei_addmaterial(self, *arg): self.callfemm('ei_addmaterial' + doargs(*arg))
    def ei_addboundprop(self, *arg): self.callfemm('ei_addboundprop' + doargs(*arg))
    def ei_addpointprop(self, *arg): self.callfemm('ei_addpointprop' + doargs(*arg))
    def ei_addconductorprop(self, *arg): self.callfemm('ei_addconductorprop' + doargs(*arg))
    def ei_clearselected(self): self.callfemm('ei_clearselected()')
    def ei_selectnode(self, *arg): return self.callfemm('ei_selectnode' + doargs(*arg))
    def ei_selectsegment(self, *arg): return self.callfemm('ei_selectsegment' + doargs(*arg))
    def ei_selectarcsegment(self, *arg): return self.callfemm('ei_selectarcsegment' + doargs(*arg))
    def ei_selectlabel(self, *arg): return self.callfemm('ei_selectlabel' + doargs(*arg))
    def ei_selectgroup(self, *arg): self.callfemm('ei_selectgroup' + doargs(*arg))
    def ei_selectrectangle(self, *arg): self.callfemm('ei_selectrectangle' + doargs(*arg))
    def ei_selectcircle(self, *arg): self.callfemm('ei_selectcircle' + doargs(*arg))
    def ei_setnodeprop(self, *arg): self.callfemm('ei_setnodeprop' + doargs(*arg))
    def ei_setblockprop(self, *arg): self.callfemm('ei_setblockprop' + doargs(*arg))
    def ei_setsegmentprop(self, *arg): self.callfemm('ei_setsegmentprop' + doargs(*arg))
    def ei_setarcsegmentprop(self, *arg): self.callfemm('ei_setarcsegmentprop' + doargs(*arg))
    def ei_setgroup(self, *arg): self.callfemm('ei_setgroup' + doargs(*arg))
    def ei_deleteselected(self): self.callfemm('ei_deleteselected()')
    def ei_deleteselectednodes(self): self.callfemm('ei_deleteselectednodes()')
    def ei_deleteselectedlabels(self): self.callfemm('ei_deleteselectedlabels()')
    def ei_deleteselectedsegments(self): self.callfemm('ei_deleteselectedsegments()')
    def ei_deleteselectedarcsegments(self): self.callfemm('ei_deleteselectedarcsegments()')
    def ei_movetranslate(self, *arg): self.callfemm('ei_movetranslate' + doargs(*arg))
    def ei_moverotate(self, *arg): self.callfemm('ei_moverotate' + doargs(*arg))
    def ei_copytranslate(self, *arg): self.callfemm('ei_copytranslate' + doargs(*arg))
    def ei_copyrotate(self, *arg): self.callfemm('ei_copyrotate' + doargs(*arg))
    def ei_mirror(self, *arg): self.callfemm('ei_mirror' + doargs(*arg))
    def ei_scale(self, *arg): self.callfemm('ei_scale' + doargs(*arg))
    def ei_modifymaterial(self, *arg): self.callfemm('ei_modifymaterial' + doargs(*arg))
    def ei_modifyboundprop(self, *arg): self.callfemm('ei_modifyboundprop' + doargs(*arg))
    def ei_modifypointprop(self, *arg): self.callfemm('ei_modifypointprop' + doargs(*arg))
    def ei_modifyconductorprop(self, *arg): self.callfemm('ei_modifyconductorprop' + doargs(*arg))
    def ei_deletematerial(self, *arg): self.callfemm('ei_deletematerial' + doargs(*arg))
    def ei_deleteboundprop(self, *arg): self.callfemm('ei_deleteboundprop' + doargs(*arg))
    def ei_deletepointprop(self, *arg): self.callfemm('ei_deletepointprop' + doargs(*arg))
    def ei_deleteconductor(self, *arg): self.callfemm('ei_deleteconductor' + doargs(*arg))
    def ei_probdef(self, *arg): self.callfemm('ei_probdef' + doargs(*arg))
    def ei_saveas(self, *arg): self.callfemm('ei_saveas' + doargs(*arg))
    def ei_analyze(self, *arg): self.callfemm('ei_analyze' + doargs(*arg))
    def ei_analyse(self, *arg): self.ei_analyze(*arg)
    def ei_loadsolution(self): self.callfemm('ei_loadsolution()')
    def ei_createmesh(self): self.callfemm('ei_createmesh()')
    def ei_smartmesh(self, *arg): self.callfemm('ei_smartmesh' + doargs(*arg))
    def ei_setcomment(self, *arg): self.callfemm('ei_setcomment' + doargs(*arg))
    def ei_getmaterial(self, *arg): self.callfemm('ei_getmaterial' + doargs(*arg))
    def ei_gettitle(self): return self.callfemm('ei_gettitle()')
    def ei_defineouterspace(self, *arg): self.callfemm('ei_defineouterspace' + doargs(*arg))
    def ei_attachouterspace(self): self.callfemm('ei_attachouterspace()')
    def ei_detachouterspace(self): self.callfemm('ei_detachouterspace()')
    def ei_attachdefault(self): self.callfemm('ei_attachdefault()')
    def ei_detachdefault(self): self.callfemm('ei_detachdefault()')
    def ei_makeABC(self, *arg): self.callfemm('ei_makeABC' + doargs(*arg))
    def ei_createradius(self, *arg): self.callfemm('ei_createradius' + doargs(*arg))
    def ei_seteditmode(self, *arg): self.callfemm('ei_seteditmode' + doargs(*arg))
    def ei_setfocus(self, *arg): self.callfemm('ei_setfocus' + doargs(*arg))
    def ei_readdxf(self, *arg): self.callfemm('ei_readdxf' + doargs(*arg))
    def ei_savedxf(self, *arg): self.callfemm('ei_savedxf' + doargs(*arg))
    def ei_close(self): self.callfemm('ei_close()')
    def ei_refreshview(self): self.callfemm('ei_refreshview()')
    def ei_zoomnatural(self): self.callfemm('ei_zoomnatural()')
    def ei_zoomin(self): self.callfemm('ei_zoomin()')
    def ei_zoomout(self): self.callfemm('ei_zoomout()')
    def ei_zoom(self, *arg): self.callfemm('ei_zoom' + doargs(*arg))
    def ei_showgrid(self): self.callfemm('ei_showgrid()')
    def ei_hidegrid(self): self.callfemm('ei_hidegrid()')
    def ei_gridsnap(self, *arg): self.callfemm('ei_gridsnap' + doargs(*arg))
    def ei_setgrid(self, *arg): self.callfemm('ei_setgrid' + doargs(*arg))
    def ei_showmesh(self): self.callfemm('ei_showmesh()')
    def ei_purgemesh(self): self.callfemm('ei_purgemesh()')
    def ei_savebitmap(self, *arg): self.callfemm('ei_savebitmap' + doargs(*arg))
    def ei_savemetafile(self, *arg): self.callfemm('ei_savemetafile' + doargs(*arg))
    def ei_shownames(self, *arg): self.callfemm('ei_shownames' + doargs(*arg))
    def ei_hidenames(self): self.callfemm('ei_shownames(0)')
    def ei_snapgridoff(self): self.callfemm('ei_gridsnap("off")')
    def ei_snapgridon(self): self.callfemm('ei_gridsnap("on")')
    def ei_resize(self, *arg): self.callfemm('ei_resize' + doargs(*arg))
    def ei_minimize(self): self.callfemm('ei_minimize()')
    def ei_maximize(self): self.callfemm('ei_maximize()')
    def ei_restore(self): self.callfemm('ei_restore()')

    def ei_drawline(self, x1, y1, x2, y2):
        self.ei_addnode(x1, y1); self.ei_addnode(x2, y2)
        self.ei_addsegment(x1, y1, x2, y2)

    def ei_drawarc(self, x1, y1, x2, y2, angle, maxseg):
        self.ei_addnode(x1, y1); self.ei_addnode(x2, y2)
        self.ei_addarc(x1, y1, x2, y2, angle, maxseg)

    def ei_drawrectangle(self, x1, y1, x2, y2):
        self.ei_drawline(x1, y1, x2, y1); self.ei_drawline(x2, y1, x2, y2)
        self.ei_drawline(x2, y2, x1, y2); self.ei_drawline(x1, y2, x1, y1)

    # ---- eo_* (electrostatics postprocessor) ----
    def eo_getpointvalues(self, *arg): return self.callfemm('eo_getpointvalues' + doargs(*arg))
    def eo_blockintegral(self, *arg): return self.callfemm('eo_blockintegral' + doargs(*arg))
    def eo_lineintegral(self, *arg): return self.callfemm('eo_lineintegral' + doargs(*arg))
    def eo_getprobleminfo(self): return self.callfemm('eo_getprobleminfo()')
    def eo_gettitle(self): return self.callfemm('eo_gettitle()')
    def eo_selectconductor(self, *arg): self.callfemm('eo_selectconductor' + doargs(*arg))
    def eo_getconductorproperties(self, *arg): return self.callfemm('eo_getconductorproperties' + doargs(*arg))
    def eo_addcontour(self, *arg): self.callfemm('eo_addcontour' + doargs(*arg))
    def eo_bendcontour(self, *arg): self.callfemm('eo_bendcontour' + doargs(*arg))
    def eo_clearcontour(self): self.callfemm('eo_clearcontour()')
    def eo_selectblock(self, *arg): self.callfemm('eo_selectblock' + doargs(*arg))
    def eo_groupselectblock(self, *arg): self.callfemm('eo_groupselectblock' + doargs(*arg))
    def eo_clearblock(self): self.callfemm('eo_clearblock()')
    def eo_makeplot(self, *arg): return self.callfemm('eo_makeplot' + doargs(*arg))
    def eo_close(self): self.callfemm('eo_close()')
    def eo_showmesh(self): self.callfemm('eo_showmesh()')
    def eo_hidemesh(self): self.callfemm('eo_hidemesh()')
    def eo_showdensityplot(self, *arg): self.callfemm('eo_showdensityplot' + doargs(*arg))
    def eo_hidedensityplot(self): self.callfemm('eo_hidedensityplot()')
    def eo_showcontourplot(self, *arg): self.callfemm('eo_showcontourplot' + doargs(*arg))
    def eo_hidecontourplot(self): self.callfemm('eo_hidecontourplot()')
    def eo_showvectorplot(self, *arg): self.callfemm('eo_showvectorplot' + doargs(*arg))
    def eo_smooth(self, *arg): self.callfemm('eo_smooth' + doargs(*arg))
    def eo_smoothon(self): self.callfemm('eo_smooth("on")')
    def eo_smoothoff(self): self.callfemm('eo_smooth("off")')
    def eo_refreshview(self): self.callfemm('eo_refreshview()')
    def eo_savebitmap(self, *arg): self.callfemm('eo_savebitmap' + doargs(*arg))
    def eo_savemetafile(self, *arg): self.callfemm('eo_savemetafile' + doargs(*arg))
    def eo_shownames(self, *arg): self.callfemm('eo_shownames' + doargs(*arg))
    def eo_hidenames(self): self.callfemm('eo_shownames(0)')
    def eo_showgrid(self): self.callfemm('eo_showgrid()')
    def eo_hidegrid(self): self.callfemm('eo_hidegrid()')
    def eo_setgrid(self, *arg): self.callfemm('eo_setgrid' + doargs(*arg))
    def eo_showpoints(self): self.callfemm('eo_showpoints()')
    def eo_hidepoints(self): self.callfemm('eo_hidepoints()')
    def eo_reload(self): self.callfemm('eo_reload()')
    def eo_resize(self, *arg): self.callfemm('eo_resize' + doargs(*arg))
    def eo_minimize(self): self.callfemm('eo_minimize()')
    def eo_maximize(self): self.callfemm('eo_maximize()')
    def eo_restore(self): self.callfemm('eo_restore()')
    def eo_seteditmode(self, *arg): self.callfemm('eo_seteditmode' + doargs(*arg))
    def eo_selectpoint(self, *arg): return self.callfemm('eo_selectpoint' + doargs(*arg))
    def eo_numnodes(self): return self.callfemm('eo_numnodes()')
    def eo_numelements(self): return self.callfemm('eo_numelements()')
    def eo_getnode(self, *arg): return self.callfemm('eo_getnode' + doargs(*arg))
    def eo_getelement(self, *arg): return self.callfemm('eo_getelement' + doargs(*arg))
    def eo_snapgrid(self, *arg): self.callfemm('eo_gridsnap' + doargs(*arg))
    def eo_snapgridon(self): self.callfemm('eo_gridsnap("on")')
    def eo_gridsnap(self, *arg): self.callfemm('eo_gridsnap' + doargs(*arg))
    def eo_snapgridoff(self): self.callfemm('eo_gridsnap("off")')
    def eo_zoom(self, *arg): self.callfemm('eo_zoom' + doargs(*arg))
    def eo_zoomin(self): self.callfemm('eo_zoomin()')
    def eo_zoomout(self): self.callfemm('eo_zoomout()')
    def eo_zoomnatural(self): self.callfemm('eo_zoomnatural()')

    # eo_* convenience point value extractors
    # eo_getpointvalues returns: [V, Dx, Dy, Ex, Ey, ex, ey, nrg]
    def eo_getv(self, x, y):
        z = self.eo_getpointvalues(x, y); return z[0]
    def eo_getd(self, x, y):
        z = self.eo_getpointvalues(x, y); return [z[1], z[2]]
    def eo_gete(self, x, y):
        z = self.eo_getpointvalues(x, y); return [z[3], z[4]]
    def eo_getperm(self, x, y):
        z = self.eo_getpointvalues(x, y); return [z[5], z[6]]
    def eo_getenergydensity(self, x, y):
        z = self.eo_getpointvalues(x, y); return z[7]

    # ---- hi_* (heat flow preprocessor) ----
    def hi_addnode(self, *arg): self.callfemm('hi_addnode' + doargs(*arg))
    def hi_addsegment(self, *arg): self.callfemm('hi_addsegment' + doargs(*arg))
    def hi_addarc(self, *arg): self.callfemm('hi_addarc' + doargs(*arg))
    def hi_addblocklabel(self, *arg): self.callfemm('hi_addblocklabel' + doargs(*arg))
    def hi_addmaterial(self, *arg): self.callfemm('hi_addmaterial' + doargs(*arg))
    def hi_addboundprop(self, *arg): self.callfemm('hi_addboundprop' + doargs(*arg))
    def hi_addpointprop(self, *arg): self.callfemm('hi_addpointprop' + doargs(*arg))
    def hi_addconductorprop(self, *arg): self.callfemm('hi_addconductorprop' + doargs(*arg))
    def hi_addtkpoint(self, *arg): self.callfemm('hi_addtkpoint' + doargs(*arg))
    def hi_addtkpoints(self, name, tk, k=None):
        """Add a whole thermal-conductivity table. See mi_addbhpoints."""
        if k is not None:
            tk = list(zip(tk, k))
        for row in tk:
            self.hi_addtkpoint(name, row[0], row[1])

    def hi_cleartkpoints(self, *arg): self.callfemm('hi_cleartkpoints' + doargs(*arg))
    def hi_clearselected(self): self.callfemm('hi_clearselected()')
    def hi_selectnode(self, *arg): return self.callfemm('hi_selectnode' + doargs(*arg))
    def hi_selectsegment(self, *arg): return self.callfemm('hi_selectsegment' + doargs(*arg))
    def hi_selectarcsegment(self, *arg): return self.callfemm('hi_selectarcsegment' + doargs(*arg))
    def hi_selectlabel(self, *arg): return self.callfemm('hi_selectlabel' + doargs(*arg))
    def hi_selectgroup(self, *arg): self.callfemm('hi_selectgroup' + doargs(*arg))
    def hi_selectrectangle(self, *arg): self.callfemm('hi_selectrectangle' + doargs(*arg))
    def hi_selectcircle(self, *arg): self.callfemm('hi_selectcircle' + doargs(*arg))
    def hi_setnodeprop(self, *arg): self.callfemm('hi_setnodeprop' + doargs(*arg))
    def hi_setblockprop(self, *arg): self.callfemm('hi_setblockprop' + doargs(*arg))
    def hi_setsegmentprop(self, *arg): self.callfemm('hi_setsegmentprop' + doargs(*arg))
    def hi_setarcsegmentprop(self, *arg): self.callfemm('hi_setarcsegmentprop' + doargs(*arg))
    def hi_setgroup(self, *arg): self.callfemm('hi_setgroup' + doargs(*arg))
    def hi_deleteselected(self): self.callfemm('hi_deleteselected()')
    def hi_deleteselectednodes(self): self.callfemm('hi_deleteselectednodes()')
    def hi_deleteselectedlabels(self): self.callfemm('hi_deleteselectedlabels()')
    def hi_deleteselectedsegments(self): self.callfemm('hi_deleteselectedsegments()')
    def hi_deleteselectedarcsegments(self): self.callfemm('hi_deleteselectedarcsegments()')
    def hi_movetranslate(self, *arg): self.callfemm('hi_movetranslate' + doargs(*arg))
    def hi_moverotate(self, *arg): self.callfemm('hi_moverotate' + doargs(*arg))
    def hi_copytranslate(self, *arg): self.callfemm('hi_copytranslate' + doargs(*arg))
    def hi_copyrotate(self, *arg): self.callfemm('hi_copyrotate' + doargs(*arg))
    def hi_mirror(self, *arg): self.callfemm('hi_mirror' + doargs(*arg))
    def hi_scale(self, *arg): self.callfemm('hi_scale' + doargs(*arg))
    def hi_modifymaterial(self, *arg): self.callfemm('hi_modifymaterial' + doargs(*arg))
    def hi_modifyboundprop(self, *arg): self.callfemm('hi_modifyboundprop' + doargs(*arg))
    def hi_modifypointprop(self, *arg): self.callfemm('hi_modifypointprop' + doargs(*arg))
    def hi_modifyconductorprop(self, *arg): self.callfemm('hi_modifyconductorprop' + doargs(*arg))
    def hi_deletematerial(self, *arg): self.callfemm('hi_deletematerial' + doargs(*arg))
    def hi_deleteboundprop(self, *arg): self.callfemm('hi_deleteboundprop' + doargs(*arg))
    def hi_deletepointprop(self, *arg): self.callfemm('hi_deletepointprop' + doargs(*arg))
    def hi_deleteconductor(self, *arg): self.callfemm('hi_deleteconductor' + doargs(*arg))
    def hi_probdef(self, *arg): self.callfemm('hi_probdef' + doargs(*arg))
    def hi_saveas(self, *arg): self.callfemm('hi_saveas' + doargs(*arg))
    def hi_analyze(self, *arg): self.callfemm('hi_analyze' + doargs(*arg))
    def hi_analyse(self, *arg): self.hi_analyze(*arg)
    def hi_loadsolution(self): self.callfemm('hi_loadsolution()')
    def hi_createmesh(self): self.callfemm('hi_createmesh()')
    def hi_smartmesh(self, *arg): self.callfemm('hi_smartmesh' + doargs(*arg))
    def hi_setcomment(self, *arg): self.callfemm('hi_setcomment' + doargs(*arg))
    def hi_getmaterial(self, *arg): self.callfemm('hi_getmaterial' + doargs(*arg))
    def hi_gettitle(self): return self.callfemm('hi_gettitle()')
    def hi_defineouterspace(self, *arg): self.callfemm('hi_defineouterspace' + doargs(*arg))
    def hi_attachouterspace(self): self.callfemm('hi_attachouterspace()')
    def hi_detachouterspace(self): self.callfemm('hi_detachouterspace()')
    def hi_attachdefault(self): self.callfemm('hi_attachdefault()')
    def hi_detachdefault(self): self.callfemm('hi_detachdefault()')
    def hi_makeABC(self, *arg): self.callfemm('hi_makeABC' + doargs(*arg))
    def hi_createradius(self, *arg): self.callfemm('hi_createradius' + doargs(*arg))
    def hi_seteditmode(self, *arg): self.callfemm('hi_seteditmode' + doargs(*arg))
    def hi_setfocus(self, *arg): self.callfemm('hi_setfocus' + doargs(*arg))
    def hi_readdxf(self, *arg): self.callfemm('hi_readdxf' + doargs(*arg))
    def hi_savedxf(self, *arg): self.callfemm('hi_savedxf' + doargs(*arg))
    def hi_close(self): self.callfemm('hi_close()')
    def hi_refreshview(self): self.callfemm('hi_refreshview()')
    def hi_zoomnatural(self): self.callfemm('hi_zoomnatural()')
    def hi_zoomin(self): self.callfemm('hi_zoomin()')
    def hi_zoomout(self): self.callfemm('hi_zoomout()')
    def hi_zoom(self, *arg): self.callfemm('hi_zoom' + doargs(*arg))
    def hi_showgrid(self): self.callfemm('hi_showgrid()')
    def hi_hidegrid(self): self.callfemm('hi_hidegrid()')
    def hi_gridsnap(self, *arg): self.callfemm('hi_gridsnap' + doargs(*arg))
    def hi_setgrid(self, *arg): self.callfemm('hi_setgrid' + doargs(*arg))
    def hi_showmesh(self): self.callfemm('hi_showmesh()')
    def hi_purgemesh(self): self.callfemm('hi_purgemesh()')
    def hi_savebitmap(self, *arg): self.callfemm('hi_savebitmap' + doargs(*arg))
    def hi_savemetafile(self, *arg): self.callfemm('hi_savemetafile' + doargs(*arg))
    def hi_shownames(self, *arg): self.callfemm('hi_shownames' + doargs(*arg))
    def hi_hidenames(self): self.callfemm('hi_shownames(0)')
    def hi_snapgridoff(self): self.callfemm('hi_gridsnap("off")')
    def hi_snapgridon(self): self.callfemm('hi_gridsnap("on")')
    def hi_resize(self, *arg): self.callfemm('hi_resize' + doargs(*arg))
    def hi_minimize(self): self.callfemm('hi_minimize()')
    def hi_maximize(self): self.callfemm('hi_maximize()')
    def hi_restore(self): self.callfemm('hi_restore()')

    def hi_drawline(self, x1, y1, x2, y2):
        self.hi_addnode(x1, y1); self.hi_addnode(x2, y2)
        self.hi_addsegment(x1, y1, x2, y2)

    def hi_drawarc(self, x1, y1, x2, y2, angle, maxseg):
        self.hi_addnode(x1, y1); self.hi_addnode(x2, y2)
        self.hi_addarc(x1, y1, x2, y2, angle, maxseg)

    def hi_drawrectangle(self, x1, y1, x2, y2):
        self.hi_drawline(x1, y1, x2, y1); self.hi_drawline(x2, y1, x2, y2)
        self.hi_drawline(x2, y2, x1, y2); self.hi_drawline(x1, y2, x1, y1)

    # ---- ho_* (heat flow postprocessor) ----
    def ho_getpointvalues(self, *arg): return self.callfemm('ho_getpointvalues' + doargs(*arg))
    def ho_blockintegral(self, *arg): return self.callfemm('ho_blockintegral' + doargs(*arg))
    def ho_lineintegral(self, *arg): return self.callfemm('ho_lineintegral' + doargs(*arg))
    def ho_getprobleminfo(self): return self.callfemm('ho_getprobleminfo()')
    def ho_gettitle(self): return self.callfemm('ho_gettitle()')
    def ho_selectconductor(self, *arg): self.callfemm('ho_selectconductor' + doargs(*arg))
    def ho_getconductorproperties(self, *arg): return self.callfemm('ho_getconductorproperties' + doargs(*arg))
    def ho_addcontour(self, *arg): self.callfemm('ho_addcontour' + doargs(*arg))
    def ho_bendcontour(self, *arg): self.callfemm('ho_bendcontour' + doargs(*arg))
    def ho_clearcontour(self): self.callfemm('ho_clearcontour()')
    def ho_selectblock(self, *arg): self.callfemm('ho_selectblock' + doargs(*arg))
    def ho_groupselectblock(self, *arg): self.callfemm('ho_groupselectblock' + doargs(*arg))
    def ho_clearblock(self): self.callfemm('ho_clearblock()')
    def ho_makeplot(self, *arg): return self.callfemm('ho_makeplot' + doargs(*arg))
    def ho_close(self): self.callfemm('ho_close()')
    def ho_showmesh(self): self.callfemm('ho_showmesh()')
    def ho_hidemesh(self): self.callfemm('ho_hidemesh()')
    def ho_showdensityplot(self, *arg): self.callfemm('ho_showdensityplot' + doargs(*arg))
    def ho_hidedensityplot(self): self.callfemm('ho_hidedensityplot()')
    def ho_showcontourplot(self, *arg): self.callfemm('ho_showcontourplot' + doargs(*arg))
    def ho_hidecontourplot(self): self.callfemm('ho_hidecontourplot()')
    def ho_showvectorplot(self, *arg): self.callfemm('ho_showvectorplot' + doargs(*arg))
    def ho_smooth(self, *arg): self.callfemm('ho_smooth' + doargs(*arg))
    def ho_smoothon(self): self.callfemm('ho_smooth("on")')
    def ho_smoothoff(self): self.callfemm('ho_smooth("off")')
    def ho_refreshview(self): self.callfemm('ho_refreshview()')
    def ho_savebitmap(self, *arg): self.callfemm('ho_savebitmap' + doargs(*arg))
    def ho_savemetafile(self, *arg): self.callfemm('ho_savemetafile' + doargs(*arg))
    def ho_shownames(self, *arg): self.callfemm('ho_shownames' + doargs(*arg))
    def ho_hidenames(self): self.callfemm('ho_shownames(0)')
    def ho_showgrid(self): self.callfemm('ho_showgrid()')
    def ho_hidegrid(self): self.callfemm('ho_hidegrid()')
    def ho_setgrid(self, *arg): self.callfemm('ho_setgrid' + doargs(*arg))
    def ho_showpoints(self): self.callfemm('ho_showpoints()')
    def ho_hidepoints(self): self.callfemm('ho_hidepoints()')
    def ho_reload(self): self.callfemm('ho_reload()')
    def ho_resize(self, *arg): self.callfemm('ho_resize' + doargs(*arg))
    def ho_minimize(self): self.callfemm('ho_minimize()')
    def ho_maximize(self): self.callfemm('ho_maximize()')
    def ho_restore(self): self.callfemm('ho_restore()')
    def ho_seteditmode(self, *arg): self.callfemm('ho_seteditmode' + doargs(*arg))
    def ho_selectpoint(self, *arg): return self.callfemm('ho_selectpoint' + doargs(*arg))
    def ho_numnodes(self): return self.callfemm('ho_numnodes()')
    def ho_numelements(self): return self.callfemm('ho_numelements()')
    def ho_getnode(self, *arg): return self.callfemm('ho_getnode' + doargs(*arg))
    def ho_getelement(self, *arg): return self.callfemm('ho_getelement' + doargs(*arg))
    def ho_snapgrid(self, *arg): self.callfemm('ho_gridsnap' + doargs(*arg))
    def ho_snapgridon(self): self.callfemm('ho_gridsnap("on")')
    def ho_gridsnap(self, *arg): self.callfemm('ho_gridsnap' + doargs(*arg))
    def ho_snapgridoff(self): self.callfemm('ho_gridsnap("off")')
    def ho_zoom(self, *arg): self.callfemm('ho_zoom' + doargs(*arg))
    def ho_zoomin(self): self.callfemm('ho_zoomin()')
    def ho_zoomout(self): self.callfemm('ho_zoomout()')
    def ho_zoomnatural(self): self.callfemm('ho_zoomnatural()')

    # ho_* convenience point value extractors
    # ho_getpointvalues returns: [T, F1, F2, G1, G2, kx, ky]
    def ho_gett(self, x, y):
        z = self.ho_getpointvalues(x, y); return z[0]
    def ho_getf(self, x, y):
        z = self.ho_getpointvalues(x, y); return [z[1], z[2]]
    def ho_getg(self, x, y):
        z = self.ho_getpointvalues(x, y); return [z[3], z[4]]
    def ho_getk(self, x, y):
        z = self.ho_getpointvalues(x, y); return [z[5], z[6]]

    # ---- ci_* (current flow preprocessor) ----
    def ci_addnode(self, *arg): self.callfemm('ci_addnode' + doargs(*arg))
    def ci_addsegment(self, *arg): self.callfemm('ci_addsegment' + doargs(*arg))
    def ci_addarc(self, *arg): self.callfemm('ci_addarc' + doargs(*arg))
    def ci_addblocklabel(self, *arg): self.callfemm('ci_addblocklabel' + doargs(*arg))
    def ci_addmaterial(self, *arg): self.callfemm('ci_addmaterial' + doargs(*arg))
    def ci_addboundprop(self, *arg): self.callfemm('ci_addboundprop' + doargs(*arg))
    def ci_addpointprop(self, *arg): self.callfemm('ci_addpointprop' + doargs(*arg))
    def ci_addconductorprop(self, *arg): self.callfemm('ci_addconductorprop' + doargs(*arg))
    def ci_clearselected(self): self.callfemm('ci_clearselected()')
    def ci_selectnode(self, *arg): return self.callfemm('ci_selectnode' + doargs(*arg))
    def ci_selectsegment(self, *arg): return self.callfemm('ci_selectsegment' + doargs(*arg))
    def ci_selectarcsegment(self, *arg): return self.callfemm('ci_selectarcsegment' + doargs(*arg))
    def ci_selectlabel(self, *arg): return self.callfemm('ci_selectlabel' + doargs(*arg))
    def ci_selectgroup(self, *arg): self.callfemm('ci_selectgroup' + doargs(*arg))
    def ci_selectrectangle(self, *arg): self.callfemm('ci_selectrectangle' + doargs(*arg))
    def ci_selectcircle(self, *arg): self.callfemm('ci_selectcircle' + doargs(*arg))
    def ci_setnodeprop(self, *arg): self.callfemm('ci_setnodeprop' + doargs(*arg))
    def ci_setblockprop(self, *arg): self.callfemm('ci_setblockprop' + doargs(*arg))
    def ci_setsegmentprop(self, *arg): self.callfemm('ci_setsegmentprop' + doargs(*arg))
    def ci_setarcsegmentprop(self, *arg): self.callfemm('ci_setarcsegmentprop' + doargs(*arg))
    def ci_setgroup(self, *arg): self.callfemm('ci_setgroup' + doargs(*arg))
    def ci_deleteselected(self): self.callfemm('ci_deleteselected()')
    def ci_deleteselectednodes(self): self.callfemm('ci_deleteselectednodes()')
    def ci_deleteselectedlabels(self): self.callfemm('ci_deleteselectedlabels()')
    def ci_deleteselectedsegments(self): self.callfemm('ci_deleteselectedsegments()')
    def ci_deleteselectedarcsegments(self): self.callfemm('ci_deleteselectedarcsegments()')
    def ci_movetranslate(self, *arg): self.callfemm('ci_movetranslate' + doargs(*arg))
    def ci_moverotate(self, *arg): self.callfemm('ci_moverotate' + doargs(*arg))
    def ci_copytranslate(self, *arg): self.callfemm('ci_copytranslate' + doargs(*arg))
    def ci_copyrotate(self, *arg): self.callfemm('ci_copyrotate' + doargs(*arg))
    def ci_mirror(self, *arg): self.callfemm('ci_mirror' + doargs(*arg))
    def ci_scale(self, *arg): self.callfemm('ci_scale' + doargs(*arg))
    def ci_modifymaterial(self, *arg): self.callfemm('ci_modifymaterial' + doargs(*arg))
    def ci_modifyboundprop(self, *arg): self.callfemm('ci_modifyboundprop' + doargs(*arg))
    def ci_modifypointprop(self, *arg): self.callfemm('ci_modifypointprop' + doargs(*arg))
    def ci_modifyconductorprop(self, *arg): self.callfemm('ci_modifyconductorprop' + doargs(*arg))
    def ci_deletematerial(self, *arg): self.callfemm('ci_deletematerial' + doargs(*arg))
    def ci_deleteboundprop(self, *arg): self.callfemm('ci_deleteboundprop' + doargs(*arg))
    def ci_deletepointprop(self, *arg): self.callfemm('ci_deletepointprop' + doargs(*arg))
    def ci_deleteconductor(self, *arg): self.callfemm('ci_deleteconductor' + doargs(*arg))
    def ci_probdef(self, *arg): self.callfemm('ci_probdef' + doargs(*arg))
    def ci_saveas(self, *arg): self.callfemm('ci_saveas' + doargs(*arg))
    def ci_analyze(self, *arg): self.callfemm('ci_analyze' + doargs(*arg))
    def ci_analyse(self, *arg): self.ci_analyze(*arg)
    def ci_loadsolution(self): self.callfemm('ci_loadsolution()')
    def ci_createmesh(self): self.callfemm('ci_createmesh()')
    def ci_smartmesh(self, *arg): self.callfemm('ci_smartmesh' + doargs(*arg))
    def ci_setcomment(self, *arg): self.callfemm('ci_setcomment' + doargs(*arg))
    def ci_getmaterial(self, *arg): self.callfemm('ci_getmaterial' + doargs(*arg))
    def ci_gettitle(self): return self.callfemm('ci_gettitle()')
    def ci_defineouterspace(self, *arg): self.callfemm('ci_defineouterspace' + doargs(*arg))
    def ci_attachouterspace(self): self.callfemm('ci_attachouterspace()')
    def ci_detachouterspace(self): self.callfemm('ci_detachouterspace()')
    def ci_attachdefault(self): self.callfemm('ci_attachdefault()')
    def ci_detachdefault(self): self.callfemm('ci_detachdefault()')
    def ci_makeABC(self, *arg): self.callfemm('ci_makeABC' + doargs(*arg))
    def ci_createradius(self, *arg): self.callfemm('ci_createradius' + doargs(*arg))
    def ci_seteditmode(self, *arg): self.callfemm('ci_seteditmode' + doargs(*arg))
    def ci_setfocus(self, *arg): self.callfemm('ci_setfocus' + doargs(*arg))
    def ci_readdxf(self, *arg): self.callfemm('ci_readdxf' + doargs(*arg))
    def ci_savedxf(self, *arg): self.callfemm('ci_savedxf' + doargs(*arg))
    def ci_close(self): self.callfemm('ci_close()')
    def ci_refreshview(self): self.callfemm('ci_refreshview()')
    def ci_zoomnatural(self): self.callfemm('ci_zoomnatural()')
    def ci_zoomin(self): self.callfemm('ci_zoomin()')
    def ci_zoomout(self): self.callfemm('ci_zoomout()')
    def ci_zoom(self, *arg): self.callfemm('ci_zoom' + doargs(*arg))
    def ci_showgrid(self): self.callfemm('ci_showgrid()')
    def ci_hidegrid(self): self.callfemm('ci_hidegrid()')
    def ci_gridsnap(self, *arg): self.callfemm('ci_gridsnap' + doargs(*arg))
    def ci_setgrid(self, *arg): self.callfemm('ci_setgrid' + doargs(*arg))
    def ci_showmesh(self): self.callfemm('ci_showmesh()')
    def ci_purgemesh(self): self.callfemm('ci_purgemesh()')
    def ci_savebitmap(self, *arg): self.callfemm('ci_savebitmap' + doargs(*arg))
    def ci_savemetafile(self, *arg): self.callfemm('ci_savemetafile' + doargs(*arg))
    def ci_shownames(self, *arg): self.callfemm('ci_shownames' + doargs(*arg))
    def ci_hidenames(self): self.callfemm('ci_shownames(0)')
    def ci_snapgridoff(self): self.callfemm('ci_gridsnap("off")')
    def ci_snapgridon(self): self.callfemm('ci_gridsnap("on")')
    def ci_resize(self, *arg): self.callfemm('ci_resize' + doargs(*arg))
    def ci_minimize(self): self.callfemm('ci_minimize()')
    def ci_maximize(self): self.callfemm('ci_maximize()')
    def ci_restore(self): self.callfemm('ci_restore()')

    def ci_drawline(self, x1, y1, x2, y2):
        self.ci_addnode(x1, y1); self.ci_addnode(x2, y2)
        self.ci_addsegment(x1, y1, x2, y2)

    def ci_drawarc(self, x1, y1, x2, y2, angle, maxseg):
        self.ci_addnode(x1, y1); self.ci_addnode(x2, y2)
        self.ci_addarc(x1, y1, x2, y2, angle, maxseg)

    def ci_drawrectangle(self, x1, y1, x2, y2):
        self.ci_drawline(x1, y1, x2, y1); self.ci_drawline(x2, y1, x2, y2)
        self.ci_drawline(x2, y2, x1, y2); self.ci_drawline(x1, y2, x1, y1)

    # ---- co_* (current flow postprocessor) ----
    def co_getpointvalues(self, *arg): return self.callfemm('co_getpointvalues' + doargs(*arg))
    def co_blockintegral(self, *arg): return self.callfemm('co_blockintegral' + doargs(*arg))
    def co_lineintegral(self, *arg): return self.callfemm('co_lineintegral' + doargs(*arg))
    def co_getprobleminfo(self): return self.callfemm('co_getprobleminfo()')
    def co_gettitle(self): return self.callfemm('co_gettitle()')
    def co_selectconductor(self, *arg): self.callfemm('co_selectconductor' + doargs(*arg))
    def co_getconductorproperties(self, *arg): return self.callfemm('co_getconductorproperties' + doargs(*arg))
    def co_addcontour(self, *arg): self.callfemm('co_addcontour' + doargs(*arg))
    def co_bendcontour(self, *arg): self.callfemm('co_bendcontour' + doargs(*arg))
    def co_clearcontour(self): self.callfemm('co_clearcontour()')
    def co_selectblock(self, *arg): self.callfemm('co_selectblock' + doargs(*arg))
    def co_groupselectblock(self, *arg): self.callfemm('co_groupselectblock' + doargs(*arg))
    def co_clearblock(self): self.callfemm('co_clearblock()')
    def co_makeplot(self, *arg): return self.callfemm('co_makeplot' + doargs(*arg))
    def co_close(self): self.callfemm('co_close()')
    def co_showmesh(self): self.callfemm('co_showmesh()')
    def co_hidemesh(self): self.callfemm('co_hidemesh()')
    def co_showdensityplot(self, *arg): self.callfemm('co_showdensityplot' + doargs(*arg))
    def co_hidedensityplot(self): self.callfemm('co_hidedensityplot()')
    def co_showcontourplot(self, *arg): self.callfemm('co_showcontourplot' + doargs(*arg))
    def co_hidecontourplot(self): self.callfemm('co_hidecontourplot()')
    def co_showvectorplot(self, *arg): self.callfemm('co_showvectorplot' + doargs(*arg))
    def co_smooth(self, *arg): self.callfemm('co_smooth' + doargs(*arg))
    def co_smoothon(self): self.callfemm('co_smooth("on")')
    def co_smoothoff(self): self.callfemm('co_smooth("off")')
    def co_refreshview(self): self.callfemm('co_refreshview()')
    def co_savebitmap(self, *arg): self.callfemm('co_savebitmap' + doargs(*arg))
    def co_savemetafile(self, *arg): self.callfemm('co_savemetafile' + doargs(*arg))
    def co_shownames(self, *arg): self.callfemm('co_shownames' + doargs(*arg))
    def co_hidenames(self): self.callfemm('co_shownames(0)')
    def co_showgrid(self): self.callfemm('co_showgrid()')
    def co_hidegrid(self): self.callfemm('co_hidegrid()')
    def co_setgrid(self, *arg): self.callfemm('co_setgrid' + doargs(*arg))
    def co_showpoints(self): self.callfemm('co_showpoints()')
    def co_hidepoints(self): self.callfemm('co_hidepoints()')
    def co_reload(self): self.callfemm('co_reload()')
    def co_resize(self, *arg): self.callfemm('co_resize' + doargs(*arg))
    def co_minimize(self): self.callfemm('co_minimize()')
    def co_maximize(self): self.callfemm('co_maximize()')
    def co_restore(self): self.callfemm('co_restore()')
    def co_seteditmode(self, *arg): self.callfemm('co_seteditmode' + doargs(*arg))
    def co_selectpoint(self, *arg): return self.callfemm('co_selectpoint' + doargs(*arg))
    def co_numnodes(self): return self.callfemm('co_numnodes()')
    def co_numelements(self): return self.callfemm('co_numelements()')
    def co_getnode(self, *arg): return self.callfemm('co_getnode' + doargs(*arg))
    def co_getelement(self, *arg): return self.callfemm('co_getelement' + doargs(*arg))
    def co_snapgrid(self, *arg): self.callfemm('co_gridsnap' + doargs(*arg))
    def co_snapgridon(self): self.callfemm('co_gridsnap("on")')
    def co_gridsnap(self, *arg): self.callfemm('co_gridsnap' + doargs(*arg))
    def co_snapgridoff(self): self.callfemm('co_gridsnap("off")')
    def co_zoom(self, *arg): self.callfemm('co_zoom' + doargs(*arg))
    def co_zoomin(self): self.callfemm('co_zoomin()')
    def co_zoomout(self): self.callfemm('co_zoomout()')
    def co_zoomnatural(self): self.callfemm('co_zoomnatural()')

    # co_* convenience point value extractors
    # co_getpointvalues returns: [V, Jx, Jy, Kx, Ky, Ex, Ey]
    def co_getv(self, x, y):
        z = self.co_getpointvalues(x, y); return z[0]
    def co_getj(self, x, y):
        z = self.co_getpointvalues(x, y); return [z[1], z[2]]
    def co_getk(self, x, y):
        z = self.co_getpointvalues(x, y); return [z[3], z[4]]
    def co_gete(self, x, y):
        z = self.co_getpointvalues(x, y); return [z[5], z[6]]

    # ---- global commands ----
    def opendocument(self, fn): self.callfemm('open(' + quote(_fixpath(fn)) + ')')
    def main_maximize(self): self.callfemm('main_maximize()')
    def main_minimize(self): self.callfemm('main_minimize()')
    def main_resize(self, *arg): self.callfemm('main_resize' + doargs(*arg))
    def main_restore(self): self.callfemm('main_restore()')
    def smartmesh(self, *arg): self.callfemm('smartmesh' + doargs(*arg))
    def messagebox(self, *arg): self.callfemm('messagebox' + doargs(*arg))
    def hideconsole(self): self.callfemm('hideconsole()')
    def showconsole(self): self.callfemm('showconsole()')
    # In OF these toggle the floating "FEMM Properties" window that always
    # hangs over the preprocessor. femm-qt has no such persistent floating
    # window, so there is nothing to toggle -- kept as no-ops for
    # legacy-script compatibility.
    # Dropped in 0a5be31; restored for octavefemm and luafemm in c6fe935.
    def showpointprops(self): pass
    def hidepointprops(self): pass


# ================================================================
# Default global instance and module-level forwarding functions
# ================================================================

_default = None

def openfemm(*arg):
    """Connect to femm-qt. Returns the FemmSession object.

    openfemm()  or openfemm(0) -- launch GUI (femm_gui)
    openfemm(1) -- launch headless (femm_headless)
    """
    global _default
    headless = len(arg) > 0 and arg[0] == 1
    _default = FemmSession(headless=headless)
    return _default

def closefemm():
    global _default
    if _default:
        _default.close()
        _default = None

def callfemm(s):          return _default.callfemm(s)
def callfemm_noeval(s):   return _default.callfemm_noeval(s)
def newdocument(k):       _default.newdocument(k)

# ---- global command forwarders ----
def opendocument(fn):     _default.opendocument(fn)
def main_maximize():      _default.main_maximize()
def main_minimize():      _default.main_minimize()
def main_resize(*a):      _default.main_resize(*a)
def main_restore():       _default.main_restore()
def smartmesh(*a):         _default.smartmesh(*a)
def messagebox(*a):       _default.messagebox(*a)
def hideconsole():        _default.hideconsole()
def showconsole():        _default.showconsole()
def showpointprops():     pass          # see FemmSession.showpointprops
def hidepointprops():     pass

# ---- mi_* forwarders ----
def mi_addnode(*a):       _default.mi_addnode(*a)
def mi_addsegment(*a):    _default.mi_addsegment(*a)
def mi_addarc(*a):        _default.mi_addarc(*a)
def mi_addblocklabel(*a): _default.mi_addblocklabel(*a)
def mi_addmaterial(*a):   _default.mi_addmaterial(*a)
def mi_addboundprop(*a):  _default.mi_addboundprop(*a)
def mi_addpointprop(*a):  _default.mi_addpointprop(*a)
def mi_addcircprop(*a):   _default.mi_addcircprop(*a)
def mi_addbhpoint(*a):    _default.mi_addbhpoint(*a)
def mi_addbhpoints(*a):   _default.mi_addbhpoints(*a)
def mi_clearbhpoints(*a): _default.mi_clearbhpoints(*a)
def mi_clearselected():   _default.mi_clearselected()
def mi_selectnode(*a):    return _default.mi_selectnode(*a)
def mi_selectsegment(*a): return _default.mi_selectsegment(*a)
def mi_selectarcsegment(*a): return _default.mi_selectarcsegment(*a)
def mi_selectlabel(*a):   return _default.mi_selectlabel(*a)
def mi_selectgroup(*a):   _default.mi_selectgroup(*a)
def mi_selectrectangle(*a): _default.mi_selectrectangle(*a)
def mi_selectcircle(*a):  _default.mi_selectcircle(*a)
def mi_setnodeprop(*a):   _default.mi_setnodeprop(*a)
def mi_setblockprop(*a):  _default.mi_setblockprop(*a)
def mi_setsegmentprop(*a): _default.mi_setsegmentprop(*a)
def mi_setarcsegmentprop(*a): _default.mi_setarcsegmentprop(*a)
def mi_setgroup(*a):      _default.mi_setgroup(*a)
def mi_setcurrent(name, x): _default.mi_setcurrent(name, x)
def mi_deleteselected():  _default.mi_deleteselected()
def mi_deleteselectednodes(): _default.mi_deleteselectednodes()
def mi_deleteselectedlabels(): _default.mi_deleteselectedlabels()
def mi_deleteselectedsegments(): _default.mi_deleteselectedsegments()
def mi_deleteselectedarcsegments(): _default.mi_deleteselectedarcsegments()
def mi_movetranslate(*a): _default.mi_movetranslate(*a)
def mi_moverotate(*a):    _default.mi_moverotate(*a)
def mi_copytranslate(*a): _default.mi_copytranslate(*a)
def mi_copyrotate(*a):    _default.mi_copyrotate(*a)
def mi_mirror(*a):        _default.mi_mirror(*a)
def mi_scale(*a):         _default.mi_scale(*a)
def mi_modifymaterial(*a): _default.mi_modifymaterial(*a)
def mi_modifyboundprop(*a): _default.mi_modifyboundprop(*a)
def mi_modifypointprop(*a): _default.mi_modifypointprop(*a)
def mi_modifycircprop(*a): _default.mi_modifycircprop(*a)
def mi_deletematerial(*a): _default.mi_deletematerial(*a)
def mi_deleteboundprop(*a): _default.mi_deleteboundprop(*a)
def mi_deletepointprop(*a): _default.mi_deletepointprop(*a)
def mi_deletecircuit(*a): _default.mi_deletecircuit(*a)
def mi_probdef(*a):       _default.mi_probdef(*a)
def mi_saveas(*a):        _default.mi_saveas(*a)
def mi_analyze(*a):       _default.mi_analyze(*a)
def mi_analyse(*a):       _default.mi_analyze(*a)
def mi_loadsolution():    _default.mi_loadsolution()
def mi_createmesh():      _default.mi_createmesh()
def mi_smartmesh(*a):     _default.mi_smartmesh(*a)
def mi_getmaterial(*a):   _default.mi_getmaterial(*a)
def mi_getprobleminfo():  return _default.mi_getprobleminfo()
def mi_getboundingbox():  return _default.mi_getboundingbox()
def mi_defineouterspace(*a): _default.mi_defineouterspace(*a)
def mi_attachouterspace(): _default.mi_attachouterspace()
def mi_detachouterspace(): _default.mi_detachouterspace()
def mi_attachdefault():   _default.mi_attachdefault()
def mi_detachdefault():   _default.mi_detachdefault()
def mi_makeABC(*a):       _default.mi_makeABC(*a)
def mi_createradius(*a):  _default.mi_createradius(*a)
def mi_seteditmode(*a):   _default.mi_seteditmode(*a)
def mi_setcomment(*a):    _default.mi_setcomment(*a)
def mi_setprevious(*a):   _default.mi_setprevious(*a)
def mi_readdxf(*a):       _default.mi_readdxf(*a)
def mi_savedxf(*a):       _default.mi_savedxf(*a)
def mi_newdocument():     _default.mi_newdocument()
def mi_close():           _default.mi_close()
def mi_refreshview():     _default.mi_refreshview()
def mi_zoomnatural():     _default.mi_zoomnatural()
def mi_zoomin():          _default.mi_zoomin()
def mi_zoomout():         _default.mi_zoomout()
def mi_zoom(*a):          _default.mi_zoom(*a)
def mi_showgrid():        _default.mi_showgrid()
def mi_hidegrid():        _default.mi_hidegrid()
def mi_gridsnap(*a):      _default.mi_gridsnap(*a)
def mi_setgrid(*a):       _default.mi_setgrid(*a)
def mi_showmesh():        _default.mi_showmesh()
def mi_purgemesh():       _default.mi_purgemesh()
def mi_savebitmap(*a):    _default.mi_savebitmap(*a)
def mi_savemetafile(*a):  _default.mi_savemetafile(*a)
def mi_shownames(*a):     _default.mi_shownames(*a)
def mi_hidenames():       _default.mi_hidenames()
def mi_snapgridoff():     _default.mi_snapgridoff()
def mi_snapgridon():      _default.mi_snapgridon()
def mi_resize(*a):        _default.mi_resize(*a)
def mi_minimize():        _default.mi_minimize()
def mi_maximize():        _default.mi_maximize()
def mi_restore():         _default.mi_restore()
def mi_setfocus(*a):      _default.mi_setfocus(*a)
def mi_gettitle():        return _default.mi_gettitle()
def mi_drawline(x1, y1, x2, y2): _default.mi_drawline(x1, y1, x2, y2)
def mi_drawarc(x1, y1, x2, y2, a, m): _default.mi_drawarc(x1, y1, x2, y2, a, m)
def mi_drawrectangle(x1, y1, x2, y2): _default.mi_drawrectangle(x1, y1, x2, y2)

# ---- mo_* forwarders ----
def mo_getpointvalues(*a): return _default.mo_getpointvalues(*a)
def mo_blockintegral(*a): return _default.mo_blockintegral(*a)
def mo_lineintegral(*a):  return _default.mo_lineintegral(*a)
def mo_getprobleminfo():  return _default.mo_getprobleminfo()
def mo_gettitle():       return _default.mo_gettitle()
def mo_getcircuitproperties(*a): return _default.mo_getcircuitproperties(*a)
def mo_addcontour(*a):    _default.mo_addcontour(*a)
def mo_bendcontour(*a):   _default.mo_bendcontour(*a)
def mo_clearcontour():    _default.mo_clearcontour()
def mo_selectblock(*a):   _default.mo_selectblock(*a)
def mo_groupselectblock(*a): _default.mo_groupselectblock(*a)
def mo_clearblock():      _default.mo_clearblock()
def mo_makeplot(*a):      return _default.mo_makeplot(*a)
def mo_getgapb(*a):       return _default.mo_getgapb(*a)
def mo_getgapa(*a):       return _default.mo_getgapa(*a)
def mo_gapintegral(*a):   return _default.mo_gapintegral(*a)
def mo_getgapharmonics(*a): return _default.mo_getgapharmonics(*a)
def mo_getallelements():  return _default.mo_getallelements()
def mo_getallnodes():     return _default.mo_getallnodes()
def mo_getallb():         return _default.mo_getallb()
def mo_getalla():         return _default.mo_getalla()
def eo_getallnodes():        return _default.eo_getallnodes()
def eo_getallelements():     return _default.eo_getallelements()
def eo_getalld():            return _default.eo_getalld()
def eo_getallv():            return _default.eo_getallv()
def ho_getallnodes():        return _default.ho_getallnodes()
def ho_getallelements():     return _default.ho_getallelements()
def ho_getallf():            return _default.ho_getallf()
def ho_getallt():            return _default.ho_getallt()
def co_getallnodes():        return _default.co_getallnodes()
def co_getallelements():     return _default.co_getallelements()
def co_getallj():            return _default.co_getallj()
def co_getallv():            return _default.co_getallv()
def mo_getallgapb(*a):    return _default.mo_getallgapb(*a)
def mo_getallgapa(*a):    return _default.mo_getallgapa(*a)
def mo_getallgapharmonics(*a): return _default.mo_getallgapharmonics(*a)
def mo_close():           _default.mo_close()
def mo_showmesh():        _default.mo_showmesh()
def mo_hidemesh():        _default.mo_hidemesh()
def mo_showdensityplot(*a): _default.mo_showdensityplot(*a)
def mo_hidedensityplot(): _default.mo_hidedensityplot()
def mo_showcontourplot(*a): _default.mo_showcontourplot(*a)
def mo_hidecontourplot(): _default.mo_hidecontourplot()
def mo_showvectorplot(*a): _default.mo_showvectorplot(*a)
def mo_smooth(*a):        _default.mo_smooth(*a)
def mo_smoothon():        _default.mo_smoothon()
def mo_smoothoff():       _default.mo_smoothoff()
def mo_refreshview():     _default.mo_refreshview()
def mo_savebitmap(*a):    _default.mo_savebitmap(*a)
def mo_savemetafile(*a):  _default.mo_savemetafile(*a)
def mo_shownames(*a):     _default.mo_shownames(*a)
def mo_hidenames():       _default.mo_hidenames()
def mo_showgrid():        _default.mo_showgrid()
def mo_hidegrid():        _default.mo_hidegrid()
def mo_setgrid(*a):       _default.mo_setgrid(*a)
def mo_showpoints():      _default.mo_showpoints()
def mo_hidepoints():      _default.mo_hidepoints()
def mo_reload():          _default.mo_reload()
def mo_resize(*a):        _default.mo_resize(*a)
def mo_minimize():        _default.mo_minimize()
def mo_maximize():        _default.mo_maximize()
def mo_restore():         _default.mo_restore()
def mo_seteditmode(*a):   _default.mo_seteditmode(*a)
def mo_selectpoint(*a):   return _default.mo_selectpoint(*a)
def mo_numnodes():        return _default.mo_numnodes()
def mo_numelements():     return _default.mo_numelements()
def mo_getnode(*a):       return _default.mo_getnode(*a)
def mo_getelement(*a):    return _default.mo_getelement(*a)
def mo_setweightingscheme(*a): _default.mo_setweightingscheme(*a)
def mo_snapgrid(*a):      _default.mo_snapgrid(*a)
def mo_snapgridon():      _default.mo_snapgridon()
def mo_gridsnap(*a):      _default.mo_gridsnap(*a)
def mo_snapgridoff():     _default.mo_snapgridoff()
def mo_zoom(*a):          _default.mo_zoom(*a)
def mo_zoomin():          _default.mo_zoomin()
def mo_zoomout():         _default.mo_zoomout()
def mo_zoomnatural():     _default.mo_zoomnatural()
def mo_geta(x, y):        return _default.mo_geta(x, y)
def mo_getb(x, y):        return _default.mo_getb(x, y)
def mo_getconductivity(x, y): return _default.mo_getconductivity(x, y)
def mo_getenergydensity(x, y): return _default.mo_getenergydensity(x, y)
def mo_geth(x, y):        return _default.mo_geth(x, y)
def mo_getj(x, y):        return _default.mo_getj(x, y)
def mo_getmu(x, y):       return _default.mo_getmu(x, y)
def mo_getpe(x, y):       return _default.mo_getpe(x, y)
def mo_getph(x, y):       return _default.mo_getph(x, y)
def mo_getfill(x, y):     return _default.mo_getfill(x, y)

# ---- ei_* forwarders ----
def ei_addnode(*a):       _default.ei_addnode(*a)
def ei_addsegment(*a):    _default.ei_addsegment(*a)
def ei_addarc(*a):        _default.ei_addarc(*a)
def ei_addblocklabel(*a): _default.ei_addblocklabel(*a)
def ei_addmaterial(*a):   _default.ei_addmaterial(*a)
def ei_addboundprop(*a):  _default.ei_addboundprop(*a)
def ei_addpointprop(*a):  _default.ei_addpointprop(*a)
def ei_addconductorprop(*a): _default.ei_addconductorprop(*a)
def ei_clearselected():   _default.ei_clearselected()
def ei_selectnode(*a):    return _default.ei_selectnode(*a)
def ei_selectsegment(*a): return _default.ei_selectsegment(*a)
def ei_selectarcsegment(*a): return _default.ei_selectarcsegment(*a)
def ei_selectlabel(*a):   return _default.ei_selectlabel(*a)
def ei_selectgroup(*a):   _default.ei_selectgroup(*a)
def ei_selectrectangle(*a): _default.ei_selectrectangle(*a)
def ei_selectcircle(*a):  _default.ei_selectcircle(*a)
def ei_setnodeprop(*a):   _default.ei_setnodeprop(*a)
def ei_setblockprop(*a):  _default.ei_setblockprop(*a)
def ei_setsegmentprop(*a): _default.ei_setsegmentprop(*a)
def ei_setarcsegmentprop(*a): _default.ei_setarcsegmentprop(*a)
def ei_setgroup(*a):      _default.ei_setgroup(*a)
def ei_deleteselected():  _default.ei_deleteselected()
def ei_deleteselectednodes(): _default.ei_deleteselectednodes()
def ei_deleteselectedlabels(): _default.ei_deleteselectedlabels()
def ei_deleteselectedsegments(): _default.ei_deleteselectedsegments()
def ei_deleteselectedarcsegments(): _default.ei_deleteselectedarcsegments()
def ei_movetranslate(*a): _default.ei_movetranslate(*a)
def ei_moverotate(*a):    _default.ei_moverotate(*a)
def ei_copytranslate(*a): _default.ei_copytranslate(*a)
def ei_copyrotate(*a):    _default.ei_copyrotate(*a)
def ei_mirror(*a):        _default.ei_mirror(*a)
def ei_scale(*a):         _default.ei_scale(*a)
def ei_modifymaterial(*a): _default.ei_modifymaterial(*a)
def ei_modifyboundprop(*a): _default.ei_modifyboundprop(*a)
def ei_modifypointprop(*a): _default.ei_modifypointprop(*a)
def ei_modifyconductorprop(*a): _default.ei_modifyconductorprop(*a)
def ei_deletematerial(*a): _default.ei_deletematerial(*a)
def ei_deleteboundprop(*a): _default.ei_deleteboundprop(*a)
def ei_deletepointprop(*a): _default.ei_deletepointprop(*a)
def ei_deleteconductor(*a): _default.ei_deleteconductor(*a)
def ei_probdef(*a):       _default.ei_probdef(*a)
def ei_saveas(*a):        _default.ei_saveas(*a)
def ei_analyze(*a):       _default.ei_analyze(*a)
def ei_analyse(*a):       _default.ei_analyze(*a)
def ei_loadsolution():    _default.ei_loadsolution()
def ei_createmesh():      _default.ei_createmesh()
def ei_smartmesh(*a):     _default.ei_smartmesh(*a)
def ei_setcomment(*a):    _default.ei_setcomment(*a)
def ei_getmaterial(*a):   _default.ei_getmaterial(*a)
def ei_gettitle():       return _default.ei_gettitle()
def ei_defineouterspace(*a): _default.ei_defineouterspace(*a)
def ei_attachouterspace(): _default.ei_attachouterspace()
def ei_detachouterspace(): _default.ei_detachouterspace()
def ei_attachdefault():   _default.ei_attachdefault()
def ei_detachdefault():   _default.ei_detachdefault()
def ei_makeABC(*a):       _default.ei_makeABC(*a)
def ei_createradius(*a):  _default.ei_createradius(*a)
def ei_seteditmode(*a):   _default.ei_seteditmode(*a)
def ei_setfocus(*a):      _default.ei_setfocus(*a)
def ei_readdxf(*a):       _default.ei_readdxf(*a)
def ei_savedxf(*a):       _default.ei_savedxf(*a)
def ei_close():           _default.ei_close()
def ei_refreshview():     _default.ei_refreshview()
def ei_zoomnatural():     _default.ei_zoomnatural()
def ei_zoomin():          _default.ei_zoomin()
def ei_zoomout():         _default.ei_zoomout()
def ei_zoom(*a):          _default.ei_zoom(*a)
def ei_showgrid():        _default.ei_showgrid()
def ei_hidegrid():        _default.ei_hidegrid()
def ei_gridsnap(*a):      _default.ei_gridsnap(*a)
def ei_setgrid(*a):       _default.ei_setgrid(*a)
def ei_showmesh():        _default.ei_showmesh()
def ei_purgemesh():       _default.ei_purgemesh()
def ei_savebitmap(*a):    _default.ei_savebitmap(*a)
def ei_savemetafile(*a):  _default.ei_savemetafile(*a)
def ei_shownames(*a):     _default.ei_shownames(*a)
def ei_hidenames():       _default.ei_hidenames()
def ei_snapgridoff():     _default.ei_snapgridoff()
def ei_snapgridon():      _default.ei_snapgridon()
def ei_resize(*a):        _default.ei_resize(*a)
def ei_minimize():        _default.ei_minimize()
def ei_maximize():        _default.ei_maximize()
def ei_restore():         _default.ei_restore()
def ei_drawline(x1, y1, x2, y2): _default.ei_drawline(x1, y1, x2, y2)
def ei_drawarc(x1, y1, x2, y2, a, m): _default.ei_drawarc(x1, y1, x2, y2, a, m)
def ei_drawrectangle(x1, y1, x2, y2): _default.ei_drawrectangle(x1, y1, x2, y2)

# ---- eo_* forwarders ----
def eo_getpointvalues(*a): return _default.eo_getpointvalues(*a)
def eo_blockintegral(*a): return _default.eo_blockintegral(*a)
def eo_lineintegral(*a):  return _default.eo_lineintegral(*a)
def eo_getprobleminfo():  return _default.eo_getprobleminfo()
def eo_gettitle():       return _default.eo_gettitle()
def eo_selectconductor(*a): _default.eo_selectconductor(*a)
def eo_getconductorproperties(*a): return _default.eo_getconductorproperties(*a)
def eo_addcontour(*a):    _default.eo_addcontour(*a)
def eo_bendcontour(*a):   _default.eo_bendcontour(*a)
def eo_clearcontour():    _default.eo_clearcontour()
def eo_selectblock(*a):   _default.eo_selectblock(*a)
def eo_groupselectblock(*a): _default.eo_groupselectblock(*a)
def eo_clearblock():      _default.eo_clearblock()
def eo_makeplot(*a):      return _default.eo_makeplot(*a)
def eo_close():           _default.eo_close()
def eo_showmesh():        _default.eo_showmesh()
def eo_hidemesh():        _default.eo_hidemesh()
def eo_showdensityplot(*a): _default.eo_showdensityplot(*a)
def eo_hidedensityplot(): _default.eo_hidedensityplot()
def eo_showcontourplot(*a): _default.eo_showcontourplot(*a)
def eo_hidecontourplot(): _default.eo_hidecontourplot()
def eo_showvectorplot(*a): _default.eo_showvectorplot(*a)
def eo_smooth(*a):        _default.eo_smooth(*a)
def eo_smoothon():        _default.eo_smoothon()
def eo_smoothoff():       _default.eo_smoothoff()
def eo_refreshview():     _default.eo_refreshview()
def eo_savebitmap(*a):    _default.eo_savebitmap(*a)
def eo_savemetafile(*a):  _default.eo_savemetafile(*a)
def eo_shownames(*a):     _default.eo_shownames(*a)
def eo_hidenames():       _default.eo_hidenames()
def eo_showgrid():        _default.eo_showgrid()
def eo_hidegrid():        _default.eo_hidegrid()
def eo_setgrid(*a):       _default.eo_setgrid(*a)
def eo_showpoints():      _default.eo_showpoints()
def eo_hidepoints():      _default.eo_hidepoints()
def eo_reload():          _default.eo_reload()
def eo_resize(*a):        _default.eo_resize(*a)
def eo_minimize():        _default.eo_minimize()
def eo_maximize():        _default.eo_maximize()
def eo_restore():         _default.eo_restore()
def eo_seteditmode(*a):   _default.eo_seteditmode(*a)
def eo_selectpoint(*a):   return _default.eo_selectpoint(*a)
def eo_numnodes():        return _default.eo_numnodes()
def eo_numelements():     return _default.eo_numelements()
def eo_getnode(*a):       return _default.eo_getnode(*a)
def eo_getelement(*a):    return _default.eo_getelement(*a)
def eo_snapgrid(*a):      _default.eo_snapgrid(*a)
def eo_snapgridon():      _default.eo_snapgridon()
def eo_gridsnap(*a):      _default.eo_gridsnap(*a)
def eo_snapgridoff():     _default.eo_snapgridoff()
def eo_zoom(*a):          _default.eo_zoom(*a)
def eo_zoomin():          _default.eo_zoomin()
def eo_zoomout():         _default.eo_zoomout()
def eo_zoomnatural():     _default.eo_zoomnatural()
def eo_getv(x, y):        return _default.eo_getv(x, y)
def eo_getd(x, y):        return _default.eo_getd(x, y)
def eo_gete(x, y):        return _default.eo_gete(x, y)
def eo_getperm(x, y):     return _default.eo_getperm(x, y)
def eo_getenergydensity(x, y): return _default.eo_getenergydensity(x, y)

# ---- hi_* forwarders ----
def hi_addnode(*a):       _default.hi_addnode(*a)
def hi_addsegment(*a):    _default.hi_addsegment(*a)
def hi_addarc(*a):        _default.hi_addarc(*a)
def hi_addblocklabel(*a): _default.hi_addblocklabel(*a)
def hi_addmaterial(*a):   _default.hi_addmaterial(*a)
def hi_addboundprop(*a):  _default.hi_addboundprop(*a)
def hi_addpointprop(*a):  _default.hi_addpointprop(*a)
def hi_addconductorprop(*a): _default.hi_addconductorprop(*a)
def hi_addtkpoint(*a):    _default.hi_addtkpoint(*a)
def hi_addtkpoints(*a):   _default.hi_addtkpoints(*a)
def hi_cleartkpoints(*a): _default.hi_cleartkpoints(*a)
def hi_clearselected():   _default.hi_clearselected()
def hi_selectnode(*a):    return _default.hi_selectnode(*a)
def hi_selectsegment(*a): return _default.hi_selectsegment(*a)
def hi_selectarcsegment(*a): return _default.hi_selectarcsegment(*a)
def hi_selectlabel(*a):   return _default.hi_selectlabel(*a)
def hi_selectgroup(*a):   _default.hi_selectgroup(*a)
def hi_selectrectangle(*a): _default.hi_selectrectangle(*a)
def hi_selectcircle(*a):  _default.hi_selectcircle(*a)
def hi_setnodeprop(*a):   _default.hi_setnodeprop(*a)
def hi_setblockprop(*a):  _default.hi_setblockprop(*a)
def hi_setsegmentprop(*a): _default.hi_setsegmentprop(*a)
def hi_setarcsegmentprop(*a): _default.hi_setarcsegmentprop(*a)
def hi_setgroup(*a):      _default.hi_setgroup(*a)
def hi_deleteselected():  _default.hi_deleteselected()
def hi_deleteselectednodes(): _default.hi_deleteselectednodes()
def hi_deleteselectedlabels(): _default.hi_deleteselectedlabels()
def hi_deleteselectedsegments(): _default.hi_deleteselectedsegments()
def hi_deleteselectedarcsegments(): _default.hi_deleteselectedarcsegments()
def hi_movetranslate(*a): _default.hi_movetranslate(*a)
def hi_moverotate(*a):    _default.hi_moverotate(*a)
def hi_copytranslate(*a): _default.hi_copytranslate(*a)
def hi_copyrotate(*a):    _default.hi_copyrotate(*a)
def hi_mirror(*a):        _default.hi_mirror(*a)
def hi_scale(*a):         _default.hi_scale(*a)
def hi_modifymaterial(*a): _default.hi_modifymaterial(*a)
def hi_modifyboundprop(*a): _default.hi_modifyboundprop(*a)
def hi_modifypointprop(*a): _default.hi_modifypointprop(*a)
def hi_modifyconductorprop(*a): _default.hi_modifyconductorprop(*a)
def hi_deletematerial(*a): _default.hi_deletematerial(*a)
def hi_deleteboundprop(*a): _default.hi_deleteboundprop(*a)
def hi_deletepointprop(*a): _default.hi_deletepointprop(*a)
def hi_deleteconductor(*a): _default.hi_deleteconductor(*a)
def hi_probdef(*a):       _default.hi_probdef(*a)
def hi_saveas(*a):        _default.hi_saveas(*a)
def hi_analyze(*a):       _default.hi_analyze(*a)
def hi_analyse(*a):       _default.hi_analyze(*a)
def hi_loadsolution():    _default.hi_loadsolution()
def hi_createmesh():      _default.hi_createmesh()
def hi_smartmesh(*a):     _default.hi_smartmesh(*a)
def hi_setcomment(*a):    _default.hi_setcomment(*a)
def hi_getmaterial(*a):   _default.hi_getmaterial(*a)
def hi_gettitle():       return _default.hi_gettitle()
def hi_defineouterspace(*a): _default.hi_defineouterspace(*a)
def hi_attachouterspace(): _default.hi_attachouterspace()
def hi_detachouterspace(): _default.hi_detachouterspace()
def hi_attachdefault():   _default.hi_attachdefault()
def hi_detachdefault():   _default.hi_detachdefault()
def hi_makeABC(*a):       _default.hi_makeABC(*a)
def hi_createradius(*a):  _default.hi_createradius(*a)
def hi_seteditmode(*a):   _default.hi_seteditmode(*a)
def hi_setfocus(*a):      _default.hi_setfocus(*a)
def hi_readdxf(*a):       _default.hi_readdxf(*a)
def hi_savedxf(*a):       _default.hi_savedxf(*a)
def hi_close():           _default.hi_close()
def hi_refreshview():     _default.hi_refreshview()
def hi_zoomnatural():     _default.hi_zoomnatural()
def hi_zoomin():          _default.hi_zoomin()
def hi_zoomout():         _default.hi_zoomout()
def hi_zoom(*a):          _default.hi_zoom(*a)
def hi_showgrid():        _default.hi_showgrid()
def hi_hidegrid():        _default.hi_hidegrid()
def hi_gridsnap(*a):      _default.hi_gridsnap(*a)
def hi_setgrid(*a):       _default.hi_setgrid(*a)
def hi_showmesh():        _default.hi_showmesh()
def hi_purgemesh():       _default.hi_purgemesh()
def hi_savebitmap(*a):    _default.hi_savebitmap(*a)
def hi_savemetafile(*a):  _default.hi_savemetafile(*a)
def hi_shownames(*a):     _default.hi_shownames(*a)
def hi_hidenames():       _default.hi_hidenames()
def hi_snapgridoff():     _default.hi_snapgridoff()
def hi_snapgridon():      _default.hi_snapgridon()
def hi_resize(*a):        _default.hi_resize(*a)
def hi_minimize():        _default.hi_minimize()
def hi_maximize():        _default.hi_maximize()
def hi_restore():         _default.hi_restore()
def hi_drawline(x1, y1, x2, y2): _default.hi_drawline(x1, y1, x2, y2)
def hi_drawarc(x1, y1, x2, y2, a, m): _default.hi_drawarc(x1, y1, x2, y2, a, m)
def hi_drawrectangle(x1, y1, x2, y2): _default.hi_drawrectangle(x1, y1, x2, y2)

# ---- ho_* forwarders ----
def ho_getpointvalues(*a): return _default.ho_getpointvalues(*a)
def ho_blockintegral(*a): return _default.ho_blockintegral(*a)
def ho_lineintegral(*a):  return _default.ho_lineintegral(*a)
def ho_getprobleminfo():  return _default.ho_getprobleminfo()
def ho_gettitle():       return _default.ho_gettitle()
def ho_selectconductor(*a): _default.ho_selectconductor(*a)
def ho_getconductorproperties(*a): return _default.ho_getconductorproperties(*a)
def ho_addcontour(*a):    _default.ho_addcontour(*a)
def ho_bendcontour(*a):   _default.ho_bendcontour(*a)
def ho_clearcontour():    _default.ho_clearcontour()
def ho_selectblock(*a):   _default.ho_selectblock(*a)
def ho_groupselectblock(*a): _default.ho_groupselectblock(*a)
def ho_clearblock():      _default.ho_clearblock()
def ho_makeplot(*a):      return _default.ho_makeplot(*a)
def ho_close():           _default.ho_close()
def ho_showmesh():        _default.ho_showmesh()
def ho_hidemesh():        _default.ho_hidemesh()
def ho_showdensityplot(*a): _default.ho_showdensityplot(*a)
def ho_hidedensityplot(): _default.ho_hidedensityplot()
def ho_showcontourplot(*a): _default.ho_showcontourplot(*a)
def ho_hidecontourplot(): _default.ho_hidecontourplot()
def ho_showvectorplot(*a): _default.ho_showvectorplot(*a)
def ho_smooth(*a):        _default.ho_smooth(*a)
def ho_smoothon():        _default.ho_smoothon()
def ho_smoothoff():       _default.ho_smoothoff()
def ho_refreshview():     _default.ho_refreshview()
def ho_savebitmap(*a):    _default.ho_savebitmap(*a)
def ho_savemetafile(*a):  _default.ho_savemetafile(*a)
def ho_shownames(*a):     _default.ho_shownames(*a)
def ho_hidenames():       _default.ho_hidenames()
def ho_showgrid():        _default.ho_showgrid()
def ho_hidegrid():        _default.ho_hidegrid()
def ho_setgrid(*a):       _default.ho_setgrid(*a)
def ho_showpoints():      _default.ho_showpoints()
def ho_hidepoints():      _default.ho_hidepoints()
def ho_reload():          _default.ho_reload()
def ho_resize(*a):        _default.ho_resize(*a)
def ho_minimize():        _default.ho_minimize()
def ho_maximize():        _default.ho_maximize()
def ho_restore():         _default.ho_restore()
def ho_seteditmode(*a):   _default.ho_seteditmode(*a)
def ho_selectpoint(*a):   return _default.ho_selectpoint(*a)
def ho_numnodes():        return _default.ho_numnodes()
def ho_numelements():     return _default.ho_numelements()
def ho_getnode(*a):       return _default.ho_getnode(*a)
def ho_getelement(*a):    return _default.ho_getelement(*a)
def ho_snapgrid(*a):      _default.ho_snapgrid(*a)
def ho_snapgridon():      _default.ho_snapgridon()
def ho_gridsnap(*a):      _default.ho_gridsnap(*a)
def ho_snapgridoff():     _default.ho_snapgridoff()
def ho_zoom(*a):          _default.ho_zoom(*a)
def ho_zoomin():          _default.ho_zoomin()
def ho_zoomout():         _default.ho_zoomout()
def ho_zoomnatural():     _default.ho_zoomnatural()
def ho_gett(x, y):        return _default.ho_gett(x, y)
def ho_getf(x, y):        return _default.ho_getf(x, y)
def ho_getg(x, y):        return _default.ho_getg(x, y)
def ho_getk(x, y):        return _default.ho_getk(x, y)

# ---- ci_* forwarders ----
def ci_addnode(*a):       _default.ci_addnode(*a)
def ci_addsegment(*a):    _default.ci_addsegment(*a)
def ci_addarc(*a):        _default.ci_addarc(*a)
def ci_addblocklabel(*a): _default.ci_addblocklabel(*a)
def ci_addmaterial(*a):   _default.ci_addmaterial(*a)
def ci_addboundprop(*a):  _default.ci_addboundprop(*a)
def ci_addpointprop(*a):  _default.ci_addpointprop(*a)
def ci_addconductorprop(*a): _default.ci_addconductorprop(*a)
def ci_clearselected():   _default.ci_clearselected()
def ci_selectnode(*a):    return _default.ci_selectnode(*a)
def ci_selectsegment(*a): return _default.ci_selectsegment(*a)
def ci_selectarcsegment(*a): return _default.ci_selectarcsegment(*a)
def ci_selectlabel(*a):   return _default.ci_selectlabel(*a)
def ci_selectgroup(*a):   _default.ci_selectgroup(*a)
def ci_selectrectangle(*a): _default.ci_selectrectangle(*a)
def ci_selectcircle(*a):  _default.ci_selectcircle(*a)
def ci_setnodeprop(*a):   _default.ci_setnodeprop(*a)
def ci_setblockprop(*a):  _default.ci_setblockprop(*a)
def ci_setsegmentprop(*a): _default.ci_setsegmentprop(*a)
def ci_setarcsegmentprop(*a): _default.ci_setarcsegmentprop(*a)
def ci_setgroup(*a):      _default.ci_setgroup(*a)
def ci_deleteselected():  _default.ci_deleteselected()
def ci_deleteselectednodes(): _default.ci_deleteselectednodes()
def ci_deleteselectedlabels(): _default.ci_deleteselectedlabels()
def ci_deleteselectedsegments(): _default.ci_deleteselectedsegments()
def ci_deleteselectedarcsegments(): _default.ci_deleteselectedarcsegments()
def ci_movetranslate(*a): _default.ci_movetranslate(*a)
def ci_moverotate(*a):    _default.ci_moverotate(*a)
def ci_copytranslate(*a): _default.ci_copytranslate(*a)
def ci_copyrotate(*a):    _default.ci_copyrotate(*a)
def ci_mirror(*a):        _default.ci_mirror(*a)
def ci_scale(*a):         _default.ci_scale(*a)
def ci_modifymaterial(*a): _default.ci_modifymaterial(*a)
def ci_modifyboundprop(*a): _default.ci_modifyboundprop(*a)
def ci_modifypointprop(*a): _default.ci_modifypointprop(*a)
def ci_modifyconductorprop(*a): _default.ci_modifyconductorprop(*a)
def ci_deletematerial(*a): _default.ci_deletematerial(*a)
def ci_deleteboundprop(*a): _default.ci_deleteboundprop(*a)
def ci_deletepointprop(*a): _default.ci_deletepointprop(*a)
def ci_deleteconductor(*a): _default.ci_deleteconductor(*a)
def ci_probdef(*a):       _default.ci_probdef(*a)
def ci_saveas(*a):        _default.ci_saveas(*a)
def ci_analyze(*a):       _default.ci_analyze(*a)
def ci_analyse(*a):       _default.ci_analyze(*a)
def ci_loadsolution():    _default.ci_loadsolution()
def ci_createmesh():      _default.ci_createmesh()
def ci_smartmesh(*a):     _default.ci_smartmesh(*a)
def ci_setcomment(*a):    _default.ci_setcomment(*a)
def ci_getmaterial(*a):   _default.ci_getmaterial(*a)
def ci_gettitle():       return _default.ci_gettitle()
def ci_defineouterspace(*a): _default.ci_defineouterspace(*a)
def ci_attachouterspace(): _default.ci_attachouterspace()
def ci_detachouterspace(): _default.ci_detachouterspace()
def ci_attachdefault():   _default.ci_attachdefault()
def ci_detachdefault():   _default.ci_detachdefault()
def ci_makeABC(*a):       _default.ci_makeABC(*a)
def ci_createradius(*a):  _default.ci_createradius(*a)
def ci_seteditmode(*a):   _default.ci_seteditmode(*a)
def ci_setfocus(*a):      _default.ci_setfocus(*a)
def ci_readdxf(*a):       _default.ci_readdxf(*a)
def ci_savedxf(*a):       _default.ci_savedxf(*a)
def ci_close():           _default.ci_close()
def ci_refreshview():     _default.ci_refreshview()
def ci_zoomnatural():     _default.ci_zoomnatural()
def ci_zoomin():          _default.ci_zoomin()
def ci_zoomout():         _default.ci_zoomout()
def ci_zoom(*a):          _default.ci_zoom(*a)
def ci_showgrid():        _default.ci_showgrid()
def ci_hidegrid():        _default.ci_hidegrid()
def ci_gridsnap(*a):      _default.ci_gridsnap(*a)
def ci_setgrid(*a):       _default.ci_setgrid(*a)
def ci_showmesh():        _default.ci_showmesh()
def ci_purgemesh():       _default.ci_purgemesh()
def ci_savebitmap(*a):    _default.ci_savebitmap(*a)
def ci_savemetafile(*a):  _default.ci_savemetafile(*a)
def ci_shownames(*a):     _default.ci_shownames(*a)
def ci_hidenames():       _default.ci_hidenames()
def ci_snapgridoff():     _default.ci_snapgridoff()
def ci_snapgridon():      _default.ci_snapgridon()
def ci_resize(*a):        _default.ci_resize(*a)
def ci_minimize():        _default.ci_minimize()
def ci_maximize():        _default.ci_maximize()
def ci_restore():         _default.ci_restore()
def ci_drawline(x1, y1, x2, y2): _default.ci_drawline(x1, y1, x2, y2)
def ci_drawarc(x1, y1, x2, y2, a, m): _default.ci_drawarc(x1, y1, x2, y2, a, m)
def ci_drawrectangle(x1, y1, x2, y2): _default.ci_drawrectangle(x1, y1, x2, y2)

# ---- co_* forwarders ----
def co_getpointvalues(*a): return _default.co_getpointvalues(*a)
def co_blockintegral(*a): return _default.co_blockintegral(*a)
def co_lineintegral(*a):  return _default.co_lineintegral(*a)
def co_getprobleminfo():  return _default.co_getprobleminfo()
def co_gettitle():       return _default.co_gettitle()
def co_selectconductor(*a): _default.co_selectconductor(*a)
def co_getconductorproperties(*a): return _default.co_getconductorproperties(*a)
def co_addcontour(*a):    _default.co_addcontour(*a)
def co_bendcontour(*a):   _default.co_bendcontour(*a)
def co_clearcontour():    _default.co_clearcontour()
def co_selectblock(*a):   _default.co_selectblock(*a)
def co_groupselectblock(*a): _default.co_groupselectblock(*a)
def co_clearblock():      _default.co_clearblock()
def co_makeplot(*a):      return _default.co_makeplot(*a)
def co_close():           _default.co_close()
def co_showmesh():        _default.co_showmesh()
def co_hidemesh():        _default.co_hidemesh()
def co_showdensityplot(*a): _default.co_showdensityplot(*a)
def co_hidedensityplot(): _default.co_hidedensityplot()
def co_showcontourplot(*a): _default.co_showcontourplot(*a)
def co_hidecontourplot(): _default.co_hidecontourplot()
def co_showvectorplot(*a): _default.co_showvectorplot(*a)
def co_smooth(*a):        _default.co_smooth(*a)
def co_smoothon():        _default.co_smoothon()
def co_smoothoff():       _default.co_smoothoff()
def co_refreshview():     _default.co_refreshview()
def co_savebitmap(*a):    _default.co_savebitmap(*a)
def co_savemetafile(*a):  _default.co_savemetafile(*a)
def co_shownames(*a):     _default.co_shownames(*a)
def co_hidenames():       _default.co_hidenames()
def co_showgrid():        _default.co_showgrid()
def co_hidegrid():        _default.co_hidegrid()
def co_setgrid(*a):       _default.co_setgrid(*a)
def co_showpoints():      _default.co_showpoints()
def co_hidepoints():      _default.co_hidepoints()
def co_reload():          _default.co_reload()
def co_resize(*a):        _default.co_resize(*a)
def co_minimize():        _default.co_minimize()
def co_maximize():        _default.co_maximize()
def co_restore():         _default.co_restore()
def co_seteditmode(*a):   _default.co_seteditmode(*a)
def co_selectpoint(*a):   return _default.co_selectpoint(*a)
def co_numnodes():        return _default.co_numnodes()
def co_numelements():     return _default.co_numelements()
def co_getnode(*a):       return _default.co_getnode(*a)
def co_getelement(*a):    return _default.co_getelement(*a)
def co_snapgrid(*a):      _default.co_snapgrid(*a)
def co_snapgridon():      _default.co_snapgridon()
def co_gridsnap(*a):      _default.co_gridsnap(*a)
def co_snapgridoff():     _default.co_snapgridoff()
def co_zoom(*a):          _default.co_zoom(*a)
def co_zoomin():          _default.co_zoomin()
def co_zoomout():         _default.co_zoomout()
def co_zoomnatural():     _default.co_zoomnatural()
def co_getv(x, y):        return _default.co_getv(x, y)
def co_getj(x, y):        return _default.co_getj(x, y)
def co_getk(x, y):        return _default.co_getk(x, y)
def co_gete(x, y):        return _default.co_gete(x, y)
