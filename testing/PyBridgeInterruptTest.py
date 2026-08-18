# The two ways a bridge call can end badly, driven from outside femm-qt:
#
#   A. the worker dies mid-call  -> OSError, worker discarded, next use respawns
#   B. the user interrupts       -> same, and Esc actually reaches the wait
#
#     python3 PyBridgeInterruptTest.py <path to femm_headless>
#
# B is the one the design document calls non-optional, because the hazard it
# covers is an escape hatch that cannot be reached: Esc only *schedules* a
# MicroPython exception, which fires at the next VM branch -- and there is no
# next branch while execution sits inside a blocking C++ call.  Without the
# host polling for it, a wedged worker leaves the GUI responsive, the status
# bar promising "press Esc to stop", and Esc doing nothing at all.
#
# femm_headless routes SIGINT to the same py_request_interrupt() the GUI's Esc
# key calls, so a signal here exercises exactly the GUI path.
#
# Synchronisation is through files, not stdout: the child's stdout is a pipe
# and therefore block-buffered, so a marker printed before a 600-second wedge
# would not reach the driver until the wedge ended.
import os
import subprocess
import sys
import tempfile
import time

HEADLESS = sys.argv[1] if len(sys.argv) > 1 else "build/bin/femm_headless.exe"
IS_WIN = os.name == "nt"

SCRIPT = '''
import _pybridge, json, struct

def note(name, text):
    f = open(r"{d}/" + name, "w")
    f.write(text)
    f.close()

def frame(msg):
    body = b"J" + json.dumps(msg).encode()
    return struct.pack("<I", len(body)) + body

WEDGE = frame({{"id": 7, "op": "eval", "mode": "exec",
                "code": "import time; time.sleep(600)"}})

try:
    _pybridge.ensure_started()
except OSError as e:
    note("skip.txt", str(e))
    raise SystemExit(0)

info = json.loads(_pybridge.info())["v"]
session1 = _pybridge.session()
note("ready.txt", "%d %d" % (info["pid"], session1))

outcome = "no error"
interrupted = False

def attempt():
    global outcome
    try:
        _pybridge.rpc(WEDGE)
    except OSError as e:
        outcome = str(e)
    except KeyboardInterrupt:
        outcome = "KeyboardInterrupt reached the script"

{guard}

# Which of the two arrives is a race by design: the host raises OSError, and
# the KeyboardInterrupt the user asked for fires at the very next VM branch --
# often inside the except clause, before it has finished recording anything.
# Both mean the same thing, and the invariants below hold either way.
if outcome == "no error" and interrupted:
    outcome = "KeyboardInterrupt (preempted the OSError)"

# Past this point any pending KeyboardInterrupt has fired and cleared, so the
# report cannot be cut short.
lines = ["OUTCOME %s" % outcome,
         "RUNNING %s" % _pybridge.running(),
         "SESSION1 %d" % session1,
         "SESSION2 %d" % _pybridge.session()]
try:
    _pybridge.ensure_started()
    lines.append("RESPAWN ok")
except OSError as e:
    lines.append("RESPAWN failed: %s" % e)
note("result.txt", "\\n".join(lines))
'''


def write_script(d, guard_kbd):
    # A signal-driven run has a pending KeyboardInterrupt to absorb before the
    # report; the kill-driven one has nothing to absorb.
    guard = ("try:\n    attempt()\nexcept KeyboardInterrupt:\n"
             "    interrupted = True" if guard_kbd else "attempt()")
    src = SCRIPT.format(d=d.replace("\\", "/"), guard=guard)
    path = os.path.join(d, "wedge.py")
    with open(path, "w") as f:
        f.write(src)
    return path


def wait_for_file(d, names, proc, timeout):
    """First of `names` to appear, or None. Gives up early if the child dies."""
    deadline = time.time() + timeout
    while time.time() < deadline:
        for n in names:
            p = os.path.join(d, n)
            if os.path.exists(p):
                time.sleep(0.05)  # let the write complete
                with open(p) as f:
                    return n, f.read()
        if proc.poll() is not None and time.time() > deadline - timeout + 2:
            return None, ""
        time.sleep(0.1)
    return None, ""


def interrupt_child(pid):
    """Interrupt the child the way a user's keyboard would.

    Windows will not deliver CTRL_C_EVENT to another process group at all, so
    the only console signal that reaches a child without also hitting this
    driver is CTRL_BREAK -- which is why the child is started in its own
    process group, and why femm_headless handles SIGBREAK alongside SIGINT.
    Both land on the same py_request_interrupt() the GUI's Esc key calls.
    """
    import signal
    os.kill(pid, signal.CTRL_BREAK_EVENT if IS_WIN else signal.SIGINT)
    return True


def kill_worker(proc, worker_pid):
    try:
        if IS_WIN:
            return subprocess.call(
                ["taskkill", "/F", "/PID", str(worker_pid)],
                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL) == 0
        os.kill(worker_pid, 9)
        return True
    except Exception:
        return False


def run_case(guard_kbd, disturb):
    d = tempfile.mkdtemp(prefix="pybridge_")
    path = write_script(d, guard_kbd)
    # Its own process group, so a CTRL_BREAK reaches the child and not us.
    flags = subprocess.CREATE_NEW_PROCESS_GROUP if IS_WIN else 0
    proc = subprocess.Popen([HEADLESS, "-run", path], stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, text=True,
                            creationflags=flags)
    try:
        which, text = wait_for_file(d, ["ready.txt", "skip.txt"], proc, 90)
        if which == "skip.txt":
            return "SKIP", text.splitlines()[:1]
        if which is None:
            return "worker never started", []
        worker_pid, session1 = (int(x) for x in text.split())

        time.sleep(1.5)  # let the wedging rpc get properly under way
        if not disturb(proc, worker_pid):
            return "could not deliver the disturbance", []

        t0 = time.time()
        which, text = wait_for_file(d, ["result.txt"], proc, 30)
        if which is None:
            return "script never reported", []
        elapsed = time.time() - t0
        got = dict(l.split(" ", 1) for l in text.splitlines() if " " in l)

        problems = []
        outcome = got.get("OUTCOME", "")
        # Any of the three is a correct end: the host's OSError, the same
        # thing seen as the KeyboardInterrupt that preempted it, or the
        # worker-death message.
        if not any(k in outcome for k in ("discarded", "exited unexpectedly",
                                          "KeyboardInterrupt")):
            problems.append("outcome was %r" % outcome)
        # The worker was told to sleep for 600 s. Anything but a prompt return
        # here means the wait was not actually abandoned.
        if elapsed > 10:
            problems.append("took %.1fs to abandon the call" % elapsed)
        if got.get("RUNNING") != "False":
            problems.append("worker was not discarded")
        if int(got.get("SESSION2", 0)) <= session1:
            problems.append("session did not bump")
        if got.get("RESPAWN") != "ok":
            problems.append("respawn: %s" % got.get("RESPAWN"))
        return ("ok" if not problems else "; ".join(problems)), \
               text.splitlines()
    finally:
        if proc.poll() is None:
            proc.kill()
        try:
            proc.wait(10)
        except Exception:
            pass


def main():
    cases = [
        ("worker killed mid-call", False, kill_worker),
        ("Ctrl+C during a wedged call", True,
         lambda proc, wpid: interrupt_child(proc.pid)),
    ]
    bad = skipped = 0
    for name, guard, disturb in cases:
        verdict, detail = run_case(guard, disturb)
        if verdict == "SKIP":
            skipped += 1
            print("%-32s SKIP (no usable Python)" % name)
            continue
        print("%-32s %s" % (name, verdict))
        if verdict != "ok":
            bad += 1
            for line in detail:
                print("    | " + line)
    if skipped == len(cases):
        print("PyBridgeInterrupt SKIP")
        return 0
    print("PyBridgeInterrupt " + ("OK" if not bad else "FAIL"))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
