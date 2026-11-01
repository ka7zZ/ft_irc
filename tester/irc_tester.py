#!/usr/bin/env python3
"""
irc_tester.py - robustness tester for an ft_irc style server.

Usage:
    python3 irc_tester.py [--host 127.0.0.1] [--port 6667] [--pass pass]
                          [--pid <server pid>] [--flood 20000] [--only name,name]

Needs only Python 3 (standard library). Start your server first (ideally under
valgrind). Pass --pid to also report the server's fd count and RSS memory.

Assumptions: the server sends numeric 001 when registration completes and
accepts "\r\n" line endings. Adjust WELCOME / CRLF below if yours differs.
"""
import argparse
import itertools
import os
import re
import select
import socket
import struct
import sys
import time

CRLF = "\r\n"
WELCOME = " 001 "

GREEN, RED, YELLOW, RESET = "\033[32m", "\033[31m", "\033[33m", "\033[0m"
if not sys.stdout.isatty():
    GREEN = RED = YELLOW = RESET = ""

CFG = None
_counter = itertools.count(1)


class Fail(Exception):
    pass


class Warn(Exception):
    pass


def uniq(prefix="t"):
    # IRC nicks are short (often max 9), so keep them compact and unique.
    return f"{prefix}{os.getpid() % 1000}{next(_counter)}"[:9]


class Client:
    def __init__(self, rcvbuf=None):
        self.s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        if rcvbuf:
            self.s.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, rcvbuf)
        self.s.settimeout(5)
        self.s.connect((CFG.host, CFG.port))
        self.buf = ""
        self.closed = False

    def send(self, data):
        if isinstance(data, str):
            data = data.encode("latin-1", "replace")
        self.s.sendall(data)

    def _read_once(self, timeout):
        if self.closed:
            return False
        r, _, _ = select.select([self.s], [], [], timeout)
        if not r:
            return False
        try:
            d = self.s.recv(65536)
        except (ConnectionResetError, OSError):
            self.closed = True
            return False
        if not d:
            self.closed = True
            return False
        self.buf += d.decode("latin-1")
        return True

    def expect(self, needle, timeout=3.0):
        end = time.time() + timeout
        while time.time() < end:
            if needle in self.buf:
                return True
            self._read_once(min(0.2, max(0, end - time.time())))
            if self.closed:
                break
        return needle in self.buf

    def has_code(self, code):
        """True if a numeric like 433 appears as its own token. Accepts
        ':srv 433 ...', '433 ...' and '433: ...' reply formats."""
        return re.search(r"(?:^|[\s:])" + code + r"(?:[\s:]|$)", self.buf, re.M) is not None

    def expect_code(self, code, timeout=3.0):
        end = time.time() + timeout
        while time.time() < end:
            if self.has_code(code):
                return True
            self._read_once(min(0.2, max(0, end - time.time())))
            if self.closed:
                break
        return self.has_code(code)

    def drain(self, idle=0.5, maxtime=60):
        end = time.time() + maxtime
        while time.time() < end and self._read_once(idle):
            pass

    def register(self, nick=None, password=None):
        nick = nick or uniq()
        pw = CFG.password if password is None else password
        self.nick = nick
        self.send(f"PASS {pw}{CRLF}NICK {nick}{CRLF}USER {nick} 0 * :{nick}{CRLF}")
        return self.expect_code("001", 3)

    def hard_kill(self):
        """Close with RST, like a client process being kill -9'd."""
        try:
            self.s.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER,
                              struct.pack("ii", 1, 0))
            self.s.close()
        except OSError:
            pass
        self.closed = True

    def close(self):
        try:
            self.s.close()
        except OSError:
            pass
        self.closed = True


def need_registered(nick=None, rcvbuf=None):
    c = Client(rcvbuf=rcvbuf)
    if not c.register(nick):
        raise Fail("client could not register (no 001 received)")
    return c


def check_alive(label=""):
    """A brand new client must be able to connect, register and get PONG."""
    try:
        c = Client()
    except OSError as e:
        raise Fail(f"server not accepting connections {label}: {e}")
    try:
        if not c.register():
            raise Fail(f"server alive but new client cannot register {label}")
        tok = uniq("p")
        c.send(f"PING {tok}{CRLF}")
        if not c.expect("PONG", 3):
            raise Warn("registration works but no PONG reply to PING")
    finally:
        c.close()


# ----------------------------------------------------------------- tests
def t_partial_command():
    """PAS / S pass / NICK in pieces; another client works meanwhile."""
    a = Client()
    nick = uniq()
    pieces = ["PAS", f"S {CFG.password}", f"{CRLF}NI", f"CK {nick}{CRLF}US",
              f"ER {nick} 0 * :x", CRLF]
    for i, p in enumerate(pieces):
        a.send(p)
        time.sleep(0.3)
        if a.has_code("001") and i < len(pieces) - 1:
            raise Fail("registered before the full command arrived")
        if i == 2:
            b = need_registered()   # must work while A is mid-command
            b.close()
    if not a.expect_code("001", 3):
        raise Fail("split commands never produced a welcome (001)")
    if a.has_code("421"):
        raise Fail("server treated a fragment as an unknown command (421)")
    a.close()


def t_split_crlf():
    """\\r and \\n arrive in different packets."""
    a = Client()
    nick = uniq()
    a.send(f"PASS {CFG.password}\r")
    time.sleep(0.5)
    a.send(f"\nNICK {nick}\r\n")
    time.sleep(0.5)
    a.send(f"USER {nick} 0 * :x\r\n")
    if not a.expect_code("001", 3):
        raise Fail("CRLF split across packets broke registration")
    a.close()


def t_multi_commands_one_packet():
    """Several commands in a single send()."""
    a = Client()
    nick = uniq()
    a.send(f"PASS {CFG.password}{CRLF}NICK {nick}{CRLF}USER {nick} 0 * :x{CRLF}"
           f"JOIN #multi{CRLF}PING zzz{CRLF}")
    if not a.expect_code("001", 3):
        raise Fail("no welcome")
    if not a.expect("PONG", 3):
        raise Fail("commands after the first one in a packet were not processed")
    if "#multi" not in a.buf:
        raise Fail("JOIN in the same packet was not processed")
    a.close()


def t_bare_newline():
    """nc without -C sends only \\n. Many subjects want that accepted."""
    a = Client()
    nick = uniq()
    a.send(f"PASS {CFG.password}\nNICK {nick}\nUSER {nick} 0 * :x\n")
    if not a.expect_code("001", 3):
        raise Warn("server does not accept bare \\n line endings (nc default)")
    a.close()


def t_kill_client():
    """Hard-kill a client that is in a channel."""
    a = need_registered()
    b = need_registered()
    a.send(f"JOIN #kill{CRLF}")
    b.send(f"JOIN #kill{CRLF}")
    time.sleep(0.3)
    nick = a.nick
    a.hard_kill()
    time.sleep(0.5)
    b.send(f"PRIVMSG #kill :still here{CRLF}")
    b.send(f"PING x{CRLF}")
    if not b.expect("PONG", 3):
        raise Fail("remaining client no longer served after a kill")
    check_alive("after kill")
    c = Client()
    if not c.register(nick):
        raise Fail("nick of killed client was not freed")
    c.close()
    b.close()


def t_kill_half_command():
    """Kill clients mid-command, repeatedly, then check for odd state."""
    for _ in range(30):
        a = Client()
        a.send(f"PASS {CFG.password}{CRLF}NICK {uniq()}{CRLF}USER u 0 * :u{CRLF}"
               f"JOIN #half{CRLF}PRIVMSG #ha")
        time.sleep(0.02)
        a.hard_kill()
    time.sleep(0.5)
    check_alive("after killing clients with half commands")
    # leftover bytes must not leak into a fresh client's first command
    c = need_registered()
    c.send(f"PING clean{CRLF}")
    if not c.expect("PONG", 3):
        raise Fail("fresh client got a bad reply (stale buffer?)")
    if c.has_code("421"):
        raise Fail("fresh client got 421, leftover half-command probably reused")
    c.close()


def t_stopped_client_flood():
    """Equivalent of Ctrl-Z: client A stops reading, B floods the channel."""
    n = CFG.flood
    a = Client(rcvbuf=4096)
    if not a.register(uniq("slow")):
        raise Fail("A could not register")
    a.send(f"JOIN #flood{CRLF}")
    a.expect("#flood", 2)
    a.buf = ""
    # From here A never reads = stopped process.
    b = need_registered()
    b.send(f"JOIN #flood{CRLF}")
    time.sleep(0.3)
    b.s.settimeout(15)
    mem0 = server_stats()
    t0 = time.time()
    try:
        for i in range(1, n + 1):
            b.send(f"PRIVMSG #flood :flood {i}{CRLF}")
    except (socket.timeout, OSError) as e:
        raise Fail(f"flooder blocked or was dropped, server probably hung: {e}")
    print(f"      sent {n} messages in {time.time() - t0:.1f}s")
    # server must still serve others instantly while A is stopped
    t0 = time.time()
    check_alive("while client A is stopped")
    dt = time.time() - t0
    if dt > 2:
        raise Fail(f"server answered new client slowly ({dt:.1f}s) during stall")
    time.sleep(1.5)
    mem1 = server_stats()
    # resume A
    a.drain(idle=1.5)
    got = [int(x) for x in re.findall(r"flood (\d+)", a.buf)]
    mem2 = server_stats()
    print(f"      server stats  before: {mem0}  stalled: {mem1}  after: {mem2}")
    if a.closed and len(got) < n:
        raise Warn(f"A was disconnected after {len(got)}/{n} msgs "
                   "(OK only if you implemented a SendQ limit)")
    if len(got) != n:
        raise Fail(f"A received {len(got)}/{n} queued messages")
    if got != sorted(got):
        raise Fail("queued messages arrived out of order")
    a.send(f"PING back{CRLF}")
    if not a.expect("PONG", 3):
        raise Fail("A does not work normally after resuming")
    a.close()
    b.close()


def t_stopped_client_pm_flood():
    """Same stall, but through private messages."""
    n = max(1000, CFG.flood // 4)
    a = Client(rcvbuf=4096)
    nick = uniq("slow")
    if not a.register(nick):
        raise Fail("A could not register")
    b = need_registered()
    b.s.settimeout(15)
    try:
        for i in range(1, n + 1):
            b.send(f"PRIVMSG {nick} :pm {i}{CRLF}")
    except (socket.timeout, OSError) as e:
        raise Fail(f"flooder blocked, server probably hung on PM path: {e}")
    check_alive("during PM stall")
    a.drain(idle=1.5)
    got = len(re.findall(r"pm \d+", a.buf))
    if a.closed and got < n:
        raise Warn(f"A disconnected after {got}/{n} PMs (SendQ limit?)")
    if got != n:
        raise Fail(f"A received {got}/{n} PMs")
    a.close()
    b.close()


def t_long_line():
    """100 KB line without breaks."""
    a = need_registered()
    try:
        a.send("A" * 100000 + CRLF)
        a.send(f"PING after{CRLF}")
    except OSError:
        pass  # server disconnecting us is acceptable
    time.sleep(0.5)
    a.close()
    check_alive("after long line")


def t_long_line_no_newline():
    """Huge data that never ends a line: memory must stay bounded."""
    a = need_registered()
    before = server_stats()
    try:
        for _ in range(100):
            a.send("B" * 100000)
    except OSError:
        pass
    time.sleep(0.5)
    after = server_stats()
    a.close()
    check_alive("after 10 MB without newline")
    if before.get("rss_kb") and after.get("rss_kb"):
        grew = after["rss_kb"] - before["rss_kb"]
        if grew > 8000:
            raise Warn(f"server RSS grew {grew} KB on a never-ending line; "
                       "consider capping the input buffer (512 bytes)")


def t_garbage():
    a = Client()
    a.send(f"{CRLF}{CRLF}   {CRLF}FOOBAR{CRLF}\x00\x01\xff{CRLF}:{CRLF}"
           f"NICK{CRLF}USER{CRLF}JOIN{CRLF}PRIVMSG{CRLF}")
    time.sleep(0.5)
    a.close()
    check_alive("after garbage input")


def t_before_registration():
    a = Client()
    a.send(f"JOIN #test{CRLF}PRIVMSG bob :hi{CRLF}MODE #test +i{CRLF}")
    if not a.expect_code("451", 2):
        raise Warn("no 451 (not registered) for commands before registration")
    a.close()
    check_alive("after unregistered commands")


def t_wrong_password():
    a = Client()
    ok = a.register(password="definitely-wrong")
    a.close()
    if ok:
        raise Fail("registration succeeded with a wrong password")
    c = need_registered()
    c.close()


def t_duplicate_nick():
    a = need_registered()
    b = Client()
    b.send(f"PASS {CFG.password}{CRLF}NICK {a.nick}{CRLF}USER x 0 * :x{CRLF}")
    if not b.expect_code("433", 2):
        raise Fail("no 433 for a duplicate nickname")
    if b.has_code("001"):
        raise Fail("duplicate nick was allowed to register")
    # B can still pick a new nick
    b.send(f"NICK {uniq()}{CRLF}")
    if not b.expect_code("001", 2):
        raise Warn("client could not register after fixing a duplicate nick")
    a.close()
    b.close()


def t_connect_disconnect_fast():
    before = server_stats()
    for _ in range(300):
        try:
            c = Client()
            c.close()
        except OSError:
            break
    time.sleep(1)
    after = server_stats()
    check_alive("after 300 connect/disconnects")
    if before.get("fds") and after.get("fds") and after["fds"] > before["fds"] + 3:
        raise Fail(f"fd leak: {before['fds']} -> {after['fds']} open fds")


def t_many_clients():
    clients = []
    try:
        for i in range(200):
            try:
                c = Client()
            except OSError:
                break
            c.send(f"PASS {CFG.password}{CRLF}NICK m{i}x{os.getpid() % 100}{CRLF}"
                   f"USER m 0 * :m{CRLF}JOIN #many{CRLF}")
            clients.append(c)
        ok = sum(1 for c in clients if c.expect_code("001", 3))
        print(f"      {ok}/{len(clients)} clients registered")
        if ok < len(clients):
            raise Fail("some of the simultaneous clients were not welcomed")
        check_alive("with many clients connected")
    finally:
        for c in clients:
            c.close()
    time.sleep(1)
    check_alive("after many clients left")


def t_silent_client():
    s = Client()  # connects, sends nothing
    time.sleep(1)
    check_alive("with a silent connection open")
    s.close()


def t_channel_reuse():
    a = need_registered()
    a.send(f"JOIN #solo{CRLF}")
    a.send(f"TOPIC #solo :old topic{CRLF}")
    time.sleep(0.2)
    a.send(f"PART #solo{CRLF}")
    time.sleep(0.2)
    b = need_registered()
    b.send(f"JOIN #solo{CRLF}")
    b.expect("#solo", 2)
    if "old topic" in b.buf:
        raise Warn("channel kept its old topic after everyone left")
    a.close()
    b.close()


def t_quit_during_flood():
    a = need_registered()
    b = need_registered()
    c = need_registered()
    for x in (a, b, c):
        x.send(f"JOIN #race{CRLF}")
    time.sleep(0.3)
    try:
        for i in range(3000):
            a.send(f"PRIVMSG #race :x{i}{CRLF}")
            if i == 1000:
                c.hard_kill()
            if i == 2000:
                b.send(f"QUIT :bye{CRLF}")
    except OSError as e:
        raise Fail(f"server dropped the flooder: {e}")
    time.sleep(0.5)
    check_alive("after members left mid-flood")
    a.close()


TESTS = [
    ("partial_command", t_partial_command),
    ("split_crlf", t_split_crlf),
    ("multi_commands", t_multi_commands_one_packet),
    ("bare_newline", t_bare_newline),
    ("garbage", t_garbage),
    ("before_registration", t_before_registration),
    ("wrong_password", t_wrong_password),
    ("duplicate_nick", t_duplicate_nick),
    ("silent_client", t_silent_client),
    ("kill_client", t_kill_client),
    ("kill_half_command", t_kill_half_command),
    ("long_line", t_long_line),
    ("long_line_no_newline", t_long_line_no_newline),
    ("connect_disconnect", t_connect_disconnect_fast),
    ("many_clients", t_many_clients),
    ("channel_reuse", t_channel_reuse),
    ("quit_during_flood", t_quit_during_flood),
    ("stopped_flood", t_stopped_client_flood),
    ("stopped_pm_flood", t_stopped_client_pm_flood),
]


def server_stats():
    if not CFG or not CFG.pid:
        return {}
    out = {}
    try:
        out["fds"] = len(os.listdir(f"/proc/{CFG.pid}/fd"))
        with open(f"/proc/{CFG.pid}/status") as f:
            for line in f:
                if line.startswith("VmRSS:"):
                    out["rss_kb"] = int(line.split()[1])
    except OSError:
        pass
    return out


def main():
    global CFG
    p = argparse.ArgumentParser()
    p.add_argument("--host", default="127.0.0.1")
    p.add_argument("--port", type=int, default=6667)
    p.add_argument("--pass", dest="password", default="pass")
    p.add_argument("--pid", type=int, help="server PID (enables fd/RSS stats)")
    p.add_argument("--flood", type=int, default=20000,
                   help="messages for the stopped-client flood test")
    p.add_argument("--only", help="comma separated test names to run")
    CFG = p.parse_args()

    try:
        check_alive("at start")
    except (Fail, Warn, OSError) as e:
        print(f"{RED}Cannot talk to the server at {CFG.host}:{CFG.port}: {e}{RESET}")
        sys.exit(2)

    selected = set(CFG.only.split(",")) if CFG.only else None
    results = {"PASS": 0, "WARN": 0, "FAIL": 0}
    start = server_stats()

    for name, fn in TESTS:
        if selected and name not in selected:
            continue
        print(f"[ .. ] {name}: {(fn.__doc__ or '').strip().splitlines()[0] if fn.__doc__ else ''}")
        try:
            fn()
            status, msg = "PASS", ""
        except Warn as e:
            status, msg = "WARN", str(e)
        except Fail as e:
            status, msg = "FAIL", str(e)
        except Exception as e:  # network errors etc. usually mean a server problem
            status, msg = "FAIL", f"{type(e).__name__}: {e}"
        results[status] += 1
        color = {"PASS": GREEN, "WARN": YELLOW, "FAIL": RED}[status]
        print(f"[{color}{status}{RESET}] {name} {msg}")
        if status == "FAIL":
            try:
                check_alive("after failed test")
            except Exception:
                print(f"{RED}      server is no longer responding, stopping.{RESET}")
                break

    time.sleep(1)
    end = server_stats()
    print("\n=== Summary ===")
    print(f"{GREEN}{results['PASS']} passed{RESET}, "
          f"{YELLOW}{results['WARN']} warnings{RESET}, "
          f"{RED}{results['FAIL']} failed{RESET}")
    if start and end:
        print(f"server fds: {start.get('fds')} -> {end.get('fds')}   "
              f"RSS: {start.get('rss_kb')} KB -> {end.get('rss_kb')} KB")
        print("(fds should return to baseline, RSS should not keep growing)")
    print("Now stop the server with Ctrl-C and read the valgrind summary.")
    sys.exit(1 if results["FAIL"] else 0)


if __name__ == "__main__":
    main()


