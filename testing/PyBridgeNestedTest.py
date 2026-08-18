# The nested-RPC guard, driven through the path that can genuinely produce one.
#
#     python3 PyBridgeNestedTest.py <path to femm_gui>
#
# A bridge call blocks in C++ and pumps Qt events while it waits.  That pump
# can dispatch a TCP command, which executes script in the same interpreter --
# so a second _pybridge.rpc() can begin while the first is still in flight.
# The protocol is strictly one response per request, so two overlapping calls
# would desynchronise the stream and every later reply would be answering the
# wrong question.
#
# Two things are asserted, and the second matters more than the first:
#   * the nested call fails fast, with a message that says why;
#   * the OUTER call still completes normally -- rejecting the intruder must
#     not damage the conversation already in progress.
#
# This needs the GUI: femm_headless has no event pump, so nothing can re-enter
# script execution mid-call there.  A window appears for a few seconds.
import os
import socket
import subprocess
import sys
import tempfile
import time

GUI = sys.argv[1] if len(sys.argv) > 1 else "build/bin/femm_gui.exe"
PORT = 48123  # not the default, so a running femm-qt is not disturbed

SCRIPT = '''
import _pybridge, json, struct

def note(name, text):
    f = open(r"{d}/" + name, "w")
    f.write(text)
    f.close()

def frame(msg):
    body = b"J" + json.dumps(msg).encode()
    return struct.pack("<I", len(body)) + body

SLEEP = frame({{"id": 5, "op": "eval", "mode": "exec",
                "code": "import time; time.sleep(6)"}})
PING = frame({{"id": 6, "op": "eval", "mode": "eval", "code": "40 + 2"}})

nested = "not attempted"

def try_nested():
    # Called over TCP while the outer rpc below is blocked.
    global nested
    try:
        _pybridge.rpc(PING)
        nested = "no error"
    except OSError as e:
        nested = str(e)
    return 1

try:
    _pybridge.ensure_started()
except OSError as e:
    note("skip.txt", str(e))
    raise SystemExit(0)

note("ready.txt", "go")

outer = "ok"
try:
    reply = _pybridge.rpc(SLEEP)
    if not reply:
        outer = "empty reply"
except OSError as e:
    outer = "failed: " + str(e)

# The stream must still be usable after the intruder was turned away.
after = "unusable"
try:
    reply = _pybridge.rpc(PING)
    msg = json.loads(reply[5:].decode())
    after = "ok" if msg.get("v") == 42 else "wrong answer %r" % (msg.get("v"),)
except OSError as e:
    after = "failed: " + str(e)

note("result.txt", "OUTER %s\\nNESTED %s\\nAFTER %s" % (outer, nested, after))
'''


def wait_for_file(d, names, proc, timeout):
    deadline = time.time() + timeout
    while time.time() < deadline:
        for n in names:
            p = os.path.join(d, n)
            if os.path.exists(p):
                time.sleep(0.05)
                with open(p) as f:
                    return n, f.read()
        time.sleep(0.1)
    return None, ""


def send_command(sock, line, timeout=20):
    """One IPC line. The caller owns the socket and must keep it open: femm-qt
    treats a client disconnect as "my controlling client left" and exits, which
    would end the script mid-test."""
    sock.sendall((line + "\n").encode())
    sock.settimeout(timeout)
    data = b""
    while b"\n" not in data:
        chunk = sock.recv(4096)
        if not chunk:
            break
        data += chunk
    return data.decode(errors="replace").strip()


def main():
    d = tempfile.mkdtemp(prefix="pybridge_nested_")
    path = os.path.join(d, "wedge.py")
    with open(path, "w") as f:
        f.write(SCRIPT.format(d=d.replace("\\", "/")))

    proc = subprocess.Popen([GUI, "-port", str(PORT), "-run", path],
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            text=True)
    try:
        which, text = wait_for_file(d, ["ready.txt", "skip.txt"], proc, 90)
        if which == "skip.txt":
            print("PyBridgeNested SKIP (no usable Python)")
            return 0
        if which is None:
            print("PyBridgeNested FAIL: the GUI script never started")
            return 1

        time.sleep(1.0)  # the outer rpc is now blocked, pumping events
        try:
            sock = socket.create_connection(("127.0.0.1", PORT), timeout=20)
        except OSError as e:
            print("PyBridgeNested FAIL: could not reach the GUI's TCP "
                  "server: %s" % e)
            return 1
        try:
            reply = send_command(sock, "try_nested()")
            which, text = wait_for_file(d, ["result.txt"], proc, 60)
        finally:
            sock.close()
        if which is None:
            print("PyBridgeNested FAIL: the script never reported "
                  "(tcp said %r)" % reply)
            return 1

        got = dict(l.split(" ", 1) for l in text.splitlines() if " " in l)
        problems = []
        if "already in progress" not in got.get("NESTED", ""):
            problems.append("nested call was not rejected: %r"
                            % got.get("NESTED"))
        if got.get("OUTER") != "ok":
            problems.append("outer call was damaged: %r" % got.get("OUTER"))
        if got.get("AFTER") != "ok":
            problems.append("stream unusable afterwards: %r"
                            % got.get("AFTER"))
        if problems:
            print("PyBridgeNested FAIL")
            for p in problems:
                print("  " + p)
            return 1
        print("PyBridgeNested OK")
        return 0
    finally:
        if proc.poll() is None:
            proc.kill()
        try:
            proc.wait(10)
        except Exception:
            pass


if __name__ == "__main__":
    sys.exit(main())
