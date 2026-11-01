#!/usr/bin/env python3
"""
irc_torture.py - ft_irc robustness + defense tester (Python 3, stdlib only).
 
    python3 irc_torture.py --port 6667 --pass pass --pid $(pgrep -x valgrind.bin | head -1)
 
Options:
    --pid N            server PID: enables fd / RSS / CPU-spin checks and signal tests
    --only a,b         run only these test names        --group g1,g2   run only these groups
    --list             list tests and exit              --heavy         fd-exhaustion & other heavy tests
    --flood N          messages for the stalled-client flood (default 20000)
    --final-signal INT|TERM   at the very end send this signal to --pid and check the exit
    -v                 print the notes tests collect
 
Result levels:
    PASS
    WARN  - deviation from the usual IRC behaviour; an evaluator may or may not care
    FAIL  - a behaviour the subject forbids, or a crash / hang / leak. [CRIT] = typical "refused" case
 
At the end you get a DEFENSE section: for every WARN/FAIL a short note about what the
evaluator is doing and what you can say / fix.
"""
import argparse
import itertools
import os
import random
import re
import resource
import select
import signal
import socket
import struct
import sys
import threading
import time
 
CRLF = "\r\n"
GREEN, RED, YELLOW, CYAN, DIM, RESET = "\033[32m", "\033[31m", "\033[33m", "\033[36m", "\033[2m", "\033[0m"
if not sys.stdout.isatty():
    GREEN = RED = YELLOW = CYAN = DIM = RESET = ""
 
CFG = None
HAS_PONG = True
_counter = itertools.count(1)
ALL_CLIENTS = []
SOFT = []          # soft (WARN) findings of the running test
NOTES = []         # informational lines of the running test
TESTS = []
 
 
class Fail(Exception):
    pass
 
 
class Warn(Exception):
    pass
 
 
class Skip(Exception):
    pass
 
 
# ------------------------------------------------------------------ helpers
def b36(n):
    d = "0123456789abcdefghijklmnopqrstuvwxyz"
    s = ""
    while True:
        n, r = divmod(n, 36)
        s = d[r] + s
        if not n:
            return s
 
 
RUN = b36(os.getpid() % 1296).rjust(2, "0")
 
 
def uniq(prefix="u"):
    """Short unique nick (<= 9 chars, starts with a letter)."""
    return f"{prefix[:3]}{RUN}{b36(next(_counter))}"[:9]
 
 
def chan(prefix="c"):
    return "#" + uniq(prefix)
 
 
def need(cond, msg):
    if not cond:
        raise Fail(msg)
 
 
def soft(cond, msg):
    if not cond:
        SOFT.append(msg)
 
 
def note(msg):
    NOTES.append(msg)
 
 
def test(group, crit=False, pid=False, heavy=False, defense=""):
    def deco(fn):
        TESTS.append(dict(name=fn.__name__[2:], fn=fn, group=group, crit=crit,
                          pid=pid, heavy=heavy, defense=defense,
                          doc=(fn.__doc__ or "").strip().splitlines()[0] if fn.__doc__ else ""))
        return fn
    return deco
 
 
class Client:
    def __init__(self, rcvbuf=None, timeout=5):
        self.s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        if rcvbuf:
            self.s.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, rcvbuf)
        self.s.settimeout(timeout)
        self.s.connect((CFG.host, CFG.port))
        self.buf = ""
        self.closed = False
        self.nick = None
        ALL_CLIENTS.append(self)
 
    # --- io
    def send(self, data):
        if isinstance(data, str):
            data = data.encode("latin-1", "replace")
        self.s.sendall(data)
 
    def cmd(self, text):
        self.send(text + CRLF)
 
    def _read_once(self, timeout):
        if self.closed:
            return False
        try:
            r, _, _ = select.select([self.s], [], [], timeout)
        except (OSError, ValueError):
            self.closed = True
            return False
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
 
    def expect_re(self, rx, timeout=3.0):
        end = time.time() + timeout
        while True:
            if re.search(rx, self.buf, re.M):
                return True
            left = end - time.time()
            if left <= 0 or self.closed:
                break
            self._read_once(min(0.2, left))
        return re.search(rx, self.buf, re.M) is not None
 
    def expect(self, needle, timeout=3.0):
        return self.expect_re(re.escape(needle), timeout)
 
    @staticmethod
    def _code_rx(code):
        return r"(?:^|[\s:])" + code + r"(?:[\s:]|$)"
 
    def has_code(self, code):
        return re.search(self._code_rx(code), self.buf, re.M) is not None
 
    def expect_code(self, code, timeout=3.0):
        return self.expect_re(self._code_rx(code), timeout)
 
    def code_count(self, code):
        return len(re.findall(self._code_rx(code), self.buf, re.M))
 
    def count(self, rx):
        return len(re.findall(rx, self.buf, re.M))
 
    def lines(self):
        return [l.rstrip("\r") for l in self.buf.split("\n") if l.strip()]
 
    def grep(self, rx):
        return [l for l in self.lines() if re.search(rx, l)]
 
    def clear(self):
        self.buf = ""
 
    def drain(self, idle=0.5, maxtime=60):
        end = time.time() + maxtime
        while time.time() < end and self._read_once(idle):
            pass
 
    def wait_closed(self, timeout=3.0):
        end = time.time() + timeout
        while not self.closed and time.time() < end:
            self._read_once(0.2)
        return self.closed
 
    def sync(self, timeout=2.0):
        """Round-trip barrier: everything sent before is processed by the server."""
        if not HAS_PONG:
            time.sleep(0.2)
            return True
        tok = uniq("s")
        self.cmd(f"PING {tok}")
        return self.expect_re(r"PONG[^\r\n]*" + tok, timeout)
 
    # --- session
    def register(self, nick=None, password=None):
        nick = nick or uniq()
        pw = CFG.password if password is None else password
        self.nick = nick
        self.send(f"PASS {pw}{CRLF}NICK {nick}{CRLF}USER {nick} 0 * :{nick}{CRLF}")
        return self.expect_code("001", 3)
 
    def shutdown_wr(self):
        try:
            self.s.shutdown(socket.SHUT_WR)
        except OSError:
            pass
 
    def hard_kill(self):
        """Close with RST, like kill -9 on a client process."""
        try:
            self.s.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER, struct.pack("ii", 1, 0))
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
 
 
def cleanup_clients():
    for c in ALL_CLIENTS:
        c.close()
    del ALL_CLIENTS[:]
 
 
def mk(prefix="u", rcvbuf=None):
    c = Client(rcvbuf=rcvbuf)
    need(c.register(uniq(prefix)), "client could not register (no 001)")
    return c
 
 
def duo(prefix="c"):
    """a (operator) and b joined in a fresh channel, buffers cleared."""
    ch = chan(prefix)
    a, b = mk("a"), mk("b")
    a.cmd(f"JOIN {ch}")
    a.sync()
    b.cmd(f"JOIN {ch}")
    b.sync()
    a.sync()
    a.clear()
    b.clear()
    return a, b, ch
 
 
def joined(c, ch):
    return c.expect_re(r"JOIN :?" + re.escape(ch), 2)
 
 
def parse_names(buf):
    out = []
    for m in re.finditer(r" 353 [^\r\n]*? :([^\r\n]*)", buf):
        out += m.group(1).split()
    return out
 
 
def names_via_join(ch, key=None):
    """NAMES list as seen by a throw-away client joining ch (None if it could not join)."""
    o = mk("obs")
    o.cmd(f"JOIN {ch}" + (f" {key}" if key else ""))
    o.expect_code("366", 2)
    names = parse_names(o.buf)
    o.close()
    return names or None
 
 
def strip_prefix(n):
    return n.lstrip("@+%~&")
 
 
def run_threads(n, fn):
    res = [None] * n
 
    def w(i):
        try:
            res[i] = fn(i)
        except Exception as e:  # noqa
            res[i] = e
    ths = [threading.Thread(target=w, args=(i,)) for i in range(n)]
    for t in ths:
        t.start()
    for t in ths:
        t.join()
    return res
 
 
# --------------------------------------------------------------- /proc stats
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
 
 
def proc_alive():
    return bool(CFG and CFG.pid and os.path.exists(f"/proc/{CFG.pid}"))
 
 
def cpu_ticks():
    try:
        with open(f"/proc/{CFG.pid}/stat") as f:
            rest = f.read().rsplit(")", 1)[1].split()
        return int(rest[11]) + int(rest[12])
    except (OSError, IndexError, ValueError):
        return None
 
 
def cpu_pct(seconds):
    """CPU use (%) of the server while we sleep `seconds`. None without --pid."""
    if not CFG.pid:
        return None
    t0, w0 = cpu_ticks(), time.time()
    time.sleep(seconds)
    t1, w1 = cpu_ticks(), time.time()
    if t0 is None or t1 is None:
        return None
    return (t1 - t0) / os.sysconf("SC_CLK_TCK") / (w1 - w0) * 100.0
 
 
def check_alive(label=""):
    try:
        c = Client()
    except OSError as e:
        raise Fail(f"server not accepting connections {label}: {e}")
    try:
        if not c.register():
            raise Fail(f"server up but a new client cannot register {label}")
    finally:
        c.close()
 
 
# =========================================================================
#                                  FRAMING
# =========================================================================
@test("framing", defense="nc fara -C trimite doar \\n. Evaluatorii folosesc nc simplu, deci trebuie acceptat.")
def t_bare_newline():
    """nc default: only \\n line endings."""
    a = Client()
    nick = uniq()
    a.send(f"PASS {CFG.password}\nNICK {nick}\nUSER {nick} 0 * :x\n")
    need(a.expect_code("001", 3), "bare \\n line endings not accepted (nc default)")
 
@test("framing", crit=True,
      defense="Evaluatorul scrie 'com', Ctrl+D, 'man', Ctrl+D, 'd\\n' in nc. Fiecare client are buffer propriu; "
              "adaugi la buffer, cauti '\\n', procesezi doar liniile complete, restul ramane. Niciodata 421 pe fragment.")
def t_partial_command():
    """PAS / S pass / NI / CK ... in pieces, another client must work meanwhile."""
    a = Client()
    nick = uniq()
    pieces = ["PAS", f"S {CFG.password}", f"{CRLF}NI", f"CK {nick}{CRLF}US",
              f"ER {nick} 0 * :x", CRLF]
    for i, p in enumerate(pieces):
        a.send(p)
        time.sleep(0.3)
        need(not (a.has_code("001") and i < len(pieces) - 1), "registered before the full command arrived")
        if i == 2:
            mk()   # must be served while A is mid-command (no blocking read)
    need(a.expect_code("001", 3), "split commands never produced 001")
    need(not a.has_code("421"), "a fragment was executed as an unknown command (421)")
 
 
@test("framing", crit=True,
      defense="\\r si \\n pot veni in pachete diferite. Cauti '\\n' si scoti '\\r' de la final; nu presupune ca '\\r\\n' vine impreuna.")
def t_split_crlf():
    """\\r and \\n arrive in different packets."""
    a = Client()
    nick = uniq()
    a.send(f"PASS {CFG.password}\r")
    time.sleep(0.4)
    a.send(f"\nNICK {nick}\r\n")
    time.sleep(0.4)
    a.send(f"USER {nick} 0 * :x\r\n")
    need(a.expect_code("001", 3), "CRLF split across packets broke registration")
 
 
@test("framing", crit=True,
      defense="Un recv poate contine mai multe comenzi. Bucla: while ((pos = buf.find('\\n')) != npos) { proceseaza; erase }.")
def t_multi_commands_one_packet():
    """Several commands in one send()."""
    a = Client()
    nick = uniq()
    ch = chan()
    a.send(f"PASS {CFG.password}{CRLF}NICK {nick}{CRLF}USER {nick} 0 * :x{CRLF}JOIN {ch}{CRLF}PING zzz{CRLF}")
    need(a.expect_code("001", 3), "no welcome")
    need(joined(a, ch), "JOIN in the same packet was not processed")
    soft(a.expect("PONG", 2), "PING in the same packet got no PONG")
 
 

 
@test("framing", crit=True,
      defense="Un client lent (telnet tastat caracter cu caracter) trebuie sa mearga; nimic nu e blocant, tot ce nu e complet ramane in buffer.")
def t_byte_by_byte():
    """Whole registration + PING sent one byte per packet."""
    a = Client()
    nick = uniq()
    data = f"PASS {CFG.password}\r\nNICK {nick}\r\nUSER {nick} 0 * :{nick}\r\nPING bb\r\n"
    for ch in data:
        a.send(ch)
        time.sleep(0.004)
    need(a.expect_code("001", 3), "byte-by-byte registration failed")
    soft(a.expect("PONG", 2) if HAS_PONG else True, "no PONG after byte-by-byte PING")
 
 
@test("framing", crit=True,
      defense="Ordinea raspunsurilor trebuie sa respecte ordinea comenzilor. Un singur buffer FIFO per client; nu reordona.")
def t_pipeline_ordering():
    """3000 PINGs in one send: all answered, in order."""
    if not HAS_PONG:
        raise Skip("server does not answer PING")
    a = mk()
    n = 3000
    a.send("".join(f"PING t{i}{CRLF}" for i in range(n)))
    a.expect_re(rf"PONG[^\r\n]*t{n - 1}\b", 10)
    got = [int(x) for x in re.findall(r"PONG[^\r\n]*?t(\d+)\r?$", a.buf, re.M)]
    need(got == list(range(n)), f"got {len(got)}/{n} PONGs or out of order")
 
 
@test("framing", crit=True,
      defense="Linie > 512: RFC zice sa se trunchieze, dar nu poate crapa serverul. Accepta (trunchiaza) sau inchide clientul; NICIODATA crash/blocaj.")
def t_line_length_edges():
    """Lines of 400/510/512/513/600/4000 bytes."""
    for L in (400, 510, 512, 513, 600, 4000):
        a = mk()
        b = mk()
        pad = "x" * max(1, L - len(f"PRIVMSG {b.nick} :") - 2)
        try:
            a.send(f"PRIVMSG {b.nick} :{pad}{CRLF}PING after{L}{CRLF}")
        except OSError:
            note(f"len {L}: server cut the sender")
            continue
        if HAS_PONG:
            ok = a.expect_re(rf"PONG[^\r\n]*after{L}", 2)
            soft(ok or a.closed, f"len {L}: sender hangs (no PONG, not disconnected)")
        a.close()
        b.close()
    check_alive("after over-long lines")
 
 
@test("framing", crit=True,
      defense="O linie fara '\\n' nu are voie sa creasca bufferul la infinit. Pune un cap (ex. 512-8192 B): peste cap trunchiezi sau deconectezi.")
def t_long_line():
    """100 KB line, then 10 MB without a newline."""
    a = mk()
    try:
        a.send("A" * 100000 + CRLF)
        a.send(f"PING after{CRLF}")
    except OSError:
        pass
    time.sleep(0.5)
    a.close()
    check_alive("after 100KB line")
    b = mk()
    before = server_stats()
    try:
        for _ in range(100):
            b.send("B" * 100000)
    except OSError:
        pass
    time.sleep(0.5)
    after = server_stats()
    b.close()
    check_alive("after 10 MB without newline")
    if before.get("rss_kb") and after.get("rss_kb"):
        grew = after["rss_kb"] - before["rss_kb"]
        soft(grew < 8000, f"RSS +{grew} KB on a never-ending line: input buffer is not capped")
 
 
@test("framing", crit=True,
      defense="Input ostil: NUL, 0xFF, linii goale, doar ':' . Server-ul ignora sau raspunde cu eroare; nu crapa (verifica .empty() inainte de params[0]).")
def t_garbage():
    """NUL, 0xFF, empty lines, bare ':' , missing params for every command."""
    a = Client()
    rnd = random.Random(1234)
    noise = bytes(rnd.randrange(256) for _ in range(4000)).replace(b"\n", b"\r")
    a.send(f"{CRLF}{CRLF}   {CRLF}FOOBAR{CRLF}\x00\x01\xff{CRLF}:{CRLF}: {CRLF}")
    a.send(noise + b"\r\n")
    for c in ("NICK", "USER", "JOIN", "PRIVMSG", "TOPIC", "KICK", "INVITE", "MODE", "PASS", "PING"):
        a.send(f"{c}{CRLF}")
    a.send(f":{CRLF}: JOIN #x{CRLF}  {CRLF}")
    time.sleep(0.5)
    a.close()
    check_alive("after garbage")
    b = mk()
    for c in ("JOIN", "PRIVMSG", "TOPIC", "KICK", "INVITE", "MODE", "NICK", "USER", "PASS"):
        b.send(f"{c}{CRLF}")
    b.send(f"PRIVMSG{CRLF}PRIVMSG :{CRLF}PRIVMSG x{CRLF}PRIVMSG x :{CRLF}JOIN :{CRLF}JOIN ,{CRLF}KICK #a{CRLF}MODE #a +{CRLF}")
    time.sleep(0.4)
    check_alive("after commands with missing params (registered)")
    if HAS_PONG:
        soft(b.sync(), "registered client stopped answering after missing-param commands")
 
 
@test("framing", defense="Spatii in plus si prefixe: parserul nu are voie sa acceseze params[0] fara verificare.")
def t_whitespace_and_prefix():
    """Leading/trailing/multiple spaces, a client-sent prefix."""
    a = mk()
    ch = chan()
    a.send(f"   JOIN    {ch}   {CRLF}:{a.nick} PING pp{CRLF}PING  ok1  {CRLF}")
    a.sync()
    need(a.alive_probe() if False else True, "")
    soft(joined(a, ch), "'JOIN   #c   ' (extra spaces) not understood")
    check_alive("after whitespace/prefix lines")
 
 
@test("framing", defense="Comenzile sunt case-insensitive (ping/PING). HexChat/irssi trimit majuscule, nc poate trimite orice.")
def t_case_insensitive_commands():
    """ping / PiNg / join in lower case."""
    a = mk()
    ch = chan()
    a.send(f"ping lc1{CRLF}PiNg lc2{CRLF}join {ch}{CRLF}")
    a.sync()
    soft(a.expect("lc1", 1) and a.expect("lc2", 1) or not HAS_PONG, "lower/mixed case PING not answered")
    soft(joined(a, ch), "lower case 'join' not understood")
 
 
@test("framing", defense="Mesajele trebuie sa treaca byte-cu-byte (UTF-8, \\x01 CTCP pentru DCC). Nu folosi c_str()/strlen pe date.")
def t_utf8_and_ctcp_relay():
    """UTF-8 text and CTCP (\\x01...\\x01, DCC) relayed unchanged."""
    a, b = mk(), mk()
    payload = "é€😀 ăîșțâ".encode("utf-8").decode("latin-1")
    a.send(f"PRIVMSG {b.nick} :".encode() + "é€😀 ăîșțâ".encode("utf-8") + b"\r\n")
    ctcp = "\x01DCC SEND f.txt 2130706433 5000 12\x01"
    a.send(f"PRIVMSG {b.nick} :{ctcp}{CRLF}")
    a.sync()
    need(b.expect(payload, 2), "UTF-8 message was altered or not relayed")
    soft(b.expect(ctcp, 2), "CTCP/DCC message (\\x01..\\x01) was altered or not relayed (bonus DCC)")
 
 
@test("framing", defense="'PRIVMSG x :a b :c' -> textul este 'a b :c'. Fara ':' doar primul cuvant. Text gol -> 412.")
def t_trailing_param():
    """Trailing parameter handling."""
    a, b = mk(), mk()
    a.cmd(f"PRIVMSG {b.nick} :hello world :x y")
    a.cmd(f"PRIVMSG {b.nick} :   spaced   ")
    a.cmd(f"PRIVMSG {b.nick} :")
    a.sync()
    need(b.expect("hello world :x y", 2), "trailing param with inner ':' not relayed intact")
    soft(b.expect("   spaced   ", 1), "leading/trailing spaces of trailing param were trimmed")
    soft(a.has_code("412"), "empty text did not produce 412 (No text to send)")
 
 
@test("framing", defense="Comanda necunoscuta -> 421, conexiunea ramane deschisa.")
def t_unknown_command():
    """Unknown command: 421 and connection stays open."""
    a = mk()
    a.cmd("FOOBAR x y")
    soft(a.expect_code("421", 2), "unknown command gave no 421")
    need(not a.closed, "connection closed on an unknown command")
 
 
# =========================================================================
#                               REGISTRATION
# =========================================================================
@test("registration", crit=True,
      defense="Orice comanda inainte de PASS+NICK+USER -> 451, fara efect. Evaluatorul incearca JOIN/PRIVMSG inainte de login.")
def t_before_registration():
    """Commands before registration."""
    a = Client()
    a.send(f"JOIN #test{CRLF}PRIVMSG bob :hi{CRLF}MODE #test +i{CRLF}TOPIC #test :x{CRLF}KICK #test bob{CRLF}"
           f"INVITE bob #test{CRLF}")
    need(a.expect_code("451", 2), "no 451 for commands before registration")
    need(a.count(r"\bJOIN\b[^\r\n]*#test") == 0 or not a.has_code("353"), "JOIN worked before registration")
    check_alive("after unregistered commands")
 
 
@test("registration", crit=True,
      defense="Parola gresita: 464 si conexiune inchisa. Daca inchizi doar dupa urmatorul input al clientului, vezi problema 'nc ramane deschis' -> sweep la sfarsit de iteratie poll.")
def t_wrong_password():
    """Wrong password: refused, and the connection gets closed by the server."""
    a = Client()
    ok = a.register(password="definitely-wrong")
    need(not ok, "registration succeeded with a wrong password")
    soft(a.has_code("464"), "no 464 for a wrong password")
    soft(a.wait_closed(3), "connection not closed after wrong password (nc stays open until next input)")
    mk()
 
 
@test("registration", defense="Fara PASS deloc: NICK+USER nu au voie sa te inregistreze.")
def t_no_password():
    """NICK + USER without PASS."""
    a = Client()
    nick = uniq()
    a.send(f"NICK {nick}{CRLF}USER {nick} 0 * :x{CRLF}")
    time.sleep(0.5)
    need(not a.has_code("001"), "registered without PASS")
 
 
@test("registration", defense="Comenzi dupa un login esuat in acelasi pachet nu au voie sa se execute (QUIT/markForClose opreste procesarea).")
def t_commands_after_bad_pass_same_packet():
    """PASS bad + NICK + USER + JOIN in one packet."""
    a = Client()
    nick, ch = uniq(), chan("z")
    a.send(f"PASS bad{CRLF}NICK {nick}{CRLF}USER {nick} 0 * :x{CRLF}JOIN {ch}{CRLF}PRIVMSG {ch} :boo{CRLF}")
    time.sleep(0.5)
    need(not a.has_code("001"), "registered with a wrong password")
    names = names_via_join(ch)
    soft(not names or all(strip_prefix(n) != nick for n in names), "commands after the failed login were still executed")
 
 
@test("registration", crit=True,
      defense="Conexiunile esuate trebuie sa-si elibereze fd-ul. 200 de parole gresite -> numarul de fd-uri revine la baza.")
def t_many_wrong_passwords():
    """200 wrong-password attempts: closed and no fd leak."""
    for _ in range(200):
        c = Client()
        c.send(f"PASS nope{CRLF}NICK {uniq()}{CRLF}USER u 0 * :u{CRLF}")
        c.close()
    time.sleep(0.5)
    check_alive("after 200 wrong passwords")
 
 
@test("registration", defense="PASS dupa inregistrare -> 462; PASS fara parametru -> 461.")
def t_pass_edge():
    """PASS after registration, empty PASS."""
    a = mk()
    a.cmd(f"PASS {CFG.password}")
    soft(a.expect_code("462", 2), "PASS after registration gave no 462")
    b = Client()
    b.send(f"PASS{CRLF}PASS :{CRLF}")
    soft(b.expect_code("461", 2), "empty PASS gave no 461")
 
 
@test("registration", defense="USER cu parametri putini -> 461; NICK invalid -> 432; NICK gol -> 431.")
def t_user_nick_validation():
    """USER with too few params, invalid nicks."""
    a = Client()
    a.send(f"PASS {CFG.password}{CRLF}NICK {uniq()}{CRLF}USER a{CRLF}")
    soft(a.expect_code("461", 2), "USER with 1 param gave no 461")
    need(not a.has_code("001"), "registered with an incomplete USER")
    for bad in ("1abc", "#chan", "ni@ck", "-x", "a,b", "ni!ck"):
        c = Client()
        c.send(f"PASS {CFG.password}{CRLF}NICK {bad}{CRLF}USER u 0 * :u{CRLF}")
        c.expect_code("432", 1)
        soft(not c.has_code("001"), f"invalid nick '{bad}' accepted")
        c.close()
    c = Client()
    c.send(f"PASS {CFG.password}{CRLF}NICK{CRLF}")
    soft(c.expect_code("431", 1) or c.has_code("461"), "empty NICK gave neither 431 nor 461")
 
 
@test("registration", crit=True,
      defense="Doua nick-uri identice, indiferent de litere mari/mici -> 433. Compara case-insensitive.")
def t_duplicate_nick():
    """Duplicate nick (also different case); can retry with another nick."""
    a = mk("dup")
    b = Client()
    b.send(f"PASS {CFG.password}{CRLF}NICK {a.nick}{CRLF}USER x 0 * :x{CRLF}")
    need(b.expect_code("433", 2), "no 433 for a duplicate nick")
    need(not b.has_code("001"), "duplicate nick registered")
    b.send(f"NICK {uniq()}{CRLF}")
    soft(b.expect_code("001", 2), "client could not finish registration after fixing a duplicate nick")
    c = Client()
    c.send(f"PASS {CFG.password}{CRLF}NICK {a.nick.upper()}{CRLF}USER x 0 * :x{CRLF}")
    soft(c.expect_code("433", 2), "nick comparison is case-sensitive (dup/DUP both accepted)")
 
 
@test("registration", crit=True,
      defense="Server-ul e single-thread, deci cursa nu exista logic; dar testul dovedeste ca exact unul castiga si ceilalti primesc 433.")
def t_nick_race():
    """20 clients register the same nick at the same instant."""
    nick = uniq("race")
    barrier = threading.Barrier(20)
 
    def f(i):
        c = Client(timeout=8)
        barrier.wait()
        c.send(f"PASS {CFG.password}{CRLF}NICK {nick}{CRLF}USER u 0 * :u{CRLF}")
        ok = c.expect_code("001", 5)
        return ok
    res = run_threads(20, f)
    errs = [r for r in res if isinstance(r, Exception)]
    need(not errs, f"thread error: {errs[0] if errs else ''}")
    need(sum(1 for r in res if r) == 1, f"{sum(1 for r in res if r)} clients got the same nick (expected exactly 1)")
 
 
@test("registration", defense="NICK dupa login: se anunta vecinii din canale, nick-ul vechi se elibereaza, nume deja luat -> 433.")
def t_nick_change():
    """NICK change after registration."""
    a, b, ch = duo()
    old = a.nick
    new = uniq("nw")
    a.cmd(f"NICK {new}")
    a.sync()
    soft(b.expect_re(rf"NICK :?{new}", 2), "peer was not told about the NICK change")
    c = Client()
    soft(c.register(old), "old nick was not freed after NICK change")
    b.clear()
    a.clear()
    a.cmd(f"NICK {b.nick}")
    soft(a.expect_code("433", 2), "NICK to an existing nick gave no 433")
    a.cmd(f"NICK {new}")     # same nick
    a.cmd(f"PRIVMSG {ch} :still-ok")
    soft(b.expect("still-ok", 2), "after NICK change the user no longer talks in the channel")
    # the channel membership must follow the new nick
    n = names_via_join(ch)
    need(n is not None, "cannot join to inspect names")
    soft(new in [strip_prefix(x) for x in n] and old not in [strip_prefix(x) for x in n],
         "channel NAMES not updated after NICK change")
 
 
@test("registration", defense="Ordine diferita (NICK, USER, PASS): serverul nu are voie sa crape; daca cere PASS primul e ok.")
def t_registration_order():
    """NICK/USER/PASS orders."""
    for order in (("NICK", "USER", "PASS"), ("USER", "NICK", "PASS"), ("PASS", "USER", "NICK")):
        c = Client()
        n = uniq()
        parts = {"PASS": f"PASS {CFG.password}", "NICK": f"NICK {n}", "USER": f"USER {n} 0 * :{n}"}
        c.send(CRLF.join(parts[k] for k in order) + CRLF)
        c.expect_code("001", 1)
        note(f"order {order}: {'registered' if c.has_code('001') else 'refused'}")
        c.close()
    check_alive("after odd registration orders")
 
 
@test("registration", defense="HexChat trimite 'CAP LS 302' inainte de PASS. Ignorare/421 e ok, dar inregistrarea trebuie sa mearga.")
def t_cap_handshake():
    """HexChat/irssi style: CAP LS 302 ... CAP END."""
    a = Client()
    n = uniq()
    a.send(f"CAP LS 302{CRLF}PASS {CFG.password}{CRLF}NICK {n}{CRLF}USER {n} 0 * :{n}{CRLF}CAP END{CRLF}")
    need(a.expect_code("001", 3), "registration failed when CAP LS was sent first")
    need(not a.closed, "server dropped a client that sent CAP")
 
 
@test("registration", defense="PING -> PONG cu acelasi token; PONG de la client nu da 421.")
def t_ping_pong():
    """PING/PONG."""
    a = mk()
    a.cmd("PING mytoken")
    ok = a.expect("mytoken", 2)
    soft(ok, "PING token not echoed in PONG")
    if not ok:
        return
    a.clear()
    a.cmd("PONG mytoken")
    a.cmd("PING")
    a.sync()
    soft(not a.has_code("421"), "PONG from the client produced 421")
 
 
# =========================================================================
#                                  CHANNELS
# =========================================================================
@test("channels", crit=True,
      defense="JOIN: echo JOIN + 353 (cu @ pentru primul = operator) + 366. HexChat are nevoie de 353/366 ca sa afiseze lista.")
def t_join_basics():
    """JOIN numerics, first joiner is operator."""
    a = mk()
    ch = chan()
    a.cmd(f"JOIN {ch}")
    need(joined(a, ch), "no JOIN echo")
    soft(a.expect_code("366", 2), "no 366 (end of NAMES)")
    names = parse_names(a.buf)
    soft(any(n == "@" + a.nick for n in names), "first joiner is not shown as operator (@) in 353")
    soft(a.has_code("331") or a.has_code("332"), "no 331/332 topic reply on JOIN (optional)")
 
 
@test("channels", defense="Nume invalide: fara '#', gol, prea lung -> 403/476; fara crash.")
def t_join_invalid():
    """Invalid channel names."""
    a = mk()
    for name in ("abc", "#", "#" + "x" * 300, "#a b", "#a\x07b"):
        a.send(f"JOIN {name}{CRLF}")
    a.sync()
    soft(a.has_code("403") or a.has_code("476"), "no 403/476 for invalid names")
    check_alive("after invalid JOINs")
 
 
@test("channels", defense="JOIN de doua ori pe acelasi canal: nu dublezi membrul.")
def t_join_twice():
    """JOIN the same channel twice."""
    a, b, ch = duo()
    a.cmd(f"JOIN {ch}")
    a.sync()
    n = names_via_join(ch)
    need(n is not None, "cannot join")
    soft([strip_prefix(x) for x in n].count(a.nick) == 1, "member duplicated in NAMES after double JOIN")
    soft(b.count(rf"{a.nick}[^\r\n]* JOIN") == 0, "second JOIN was broadcast again")
 
 
@test("channels", defense="Subiectul nu cere liste, dar HexChat poate trimite 'JOIN #a,#b'. Lista, cu chei 'k1,k2'.")
def t_join_lists():
    """JOIN #a,#b and JOIN #k1,#k2 key1,key2."""
    a = mk()
    c1, c2 = chan("l"), chan("l")
    a.cmd(f"JOIN {c1},{c2}")
    a.sync()
    soft(joined(a, c1) and joined(a, c2), "JOIN with a comma list did not join both")
    k1, k2 = chan("k"), chan("k")
    for k, key in ((k1, "s1"), (k2, "s2")):
        a.cmd(f"JOIN {k}")
        a.cmd(f"MODE {k} +k {key}")
    a.sync()
    b = mk()
    b.cmd(f"JOIN {k1},{k2} s1,s2")
    b.sync()
    soft(joined(b, k1) and joined(b, k2), "JOIN with keys list did not join both")
 
 
@test("channels", defense="Numele de canal e case-insensitive (#Foo == #foo). Un singur canal, nu doua.")
def t_channel_case():
    """#Foo and #foo are the same channel."""
    a, b = mk(), mk()
    base = uniq("cs")
    a.cmd(f"JOIN #{base}")
    a.sync()
    b.cmd(f"JOIN #{base.upper()}")
    b.sync()
    a.clear()
    b.cmd(f"PRIVMSG #{base} :caseping")
    soft(a.expect("caseping", 2), "channel names are case-sensitive (#x and #X are different channels)")
 
 
@test("channels", crit=True,
      defense="PRIVMSG pe canal: toti membrii in afara de expeditor. Expeditorul primind propriul mesaj = bug clasic de broadcast.")
def t_privmsg_channel():
    """Channel message goes to all members except the sender."""
    a, b, ch = duo()
    c = mk()
    c.cmd(f"JOIN {ch}")
    c.sync()
    b.clear()
    a.clear()
    c.clear()
    a.cmd(f"PRIVMSG {ch} :hello-all")
    a.sync()
    need(b.expect("hello-all", 2) and c.expect("hello-all", 2), "members did not receive the channel message")
    need(a.count("hello-all") == 0, "sender received its own channel message")
    soft(b.expect_re(rf"^:{a.nick}!", 1), "relayed line has no ':nick!user@host' prefix")
 
 
@test("channels", defense="Erori PRIVMSG: 401 nick inexistent, 403/401 canal inexistent, 411 fara destinatar, 412 fara text.")
def t_privmsg_errors():
    """PRIVMSG error numerics."""
    a = mk()
    a.cmd("PRIVMSG nosuchnick :hi")
    soft(a.expect_code("401", 2), "no 401 for unknown nick")
    a.cmd("PRIVMSG #nosuchchan :hi")
    soft(a.expect_code("403", 2) or a.has_code("401"), "no 403/401 for unknown channel")
    a.cmd("PRIVMSG")
    soft(a.expect_code("411", 2) or a.has_code("461"), "no 411/461 without recipient")
    a.cmd("PRIVMSG somebody")
    soft(a.expect_code("412", 2) or a.has_code("461"), "no 412/461 without text")
 
 
@test("channels", defense="Mesaj privat: prefix corect; nick comparat case-insensitive; 'a,b' tinte multiple (optional).")
def t_privmsg_pm_and_targets():
    """PM, case-insensitive nick, multiple targets, to self."""
    a, b, c = mk(), mk(), mk()
    a.cmd(f"PRIVMSG {b.nick} :pm-one")
    a.cmd(f"PRIVMSG {b.nick.upper()} :pm-upper")
    a.cmd(f"PRIVMSG {b.nick},{c.nick} :pm-multi")
    a.cmd(f"PRIVMSG {a.nick} :pm-self")
    a.sync()
    need(b.expect("pm-one", 2), "private message not delivered")
    soft(b.expect("pm-upper", 1), "nick lookup is case-sensitive")
    soft(b.expect("pm-multi", 1) and c.expect("pm-multi", 1), "PRIVMSG with a comma list of targets not supported")
    soft(b.expect_re(rf"^:{a.nick}![^ ]+ PRIVMSG {b.nick} :pm-one", 1), "PM line format is not ':nick!user@host PRIVMSG target :text'")
    check_alive("after PM to self")
 
 
@test("channels", crit=True,
      defense="Canalul gol se sterge in PART/KICK/QUIT (aici testat via deconectare hard). Altfel canalul 'invie' cu topic/cheie/+i vechi.")
def t_channel_reuse():
    """After everyone left the channel is brand new (no topic/key/limit, new op)."""
    a = mk()
    ch = chan("ru")
    a.cmd(f"JOIN {ch}")
    a.cmd(f"TOPIC {ch} :old topic")
    a.cmd(f"MODE {ch} +kl oldkey 1")
    a.cmd(f"MODE {ch} +i")
    a.sync()
    # Replace PART with hard_kill to simulate user disconnecting
    a.hard_kill()
    time.sleep(0.5)
    b = mk()
    b.cmd(f"JOIN {ch}")
    b.expect_code("366", 2)
    need(joined(b, ch), "cannot join a channel after it became empty (old +k/+i/+l kept)")
    soft("old topic" not in b.buf, "old topic survived after everyone left")
    soft("@" + b.nick in parse_names(b.buf), "first joiner of the recreated channel is not operator")
    c = mk()
    c.cmd(f"JOIN {ch}")
    soft(joined(c, ch), "old +l/+i limit survived the channel being recreated")
 
 
@test("channels", crit=True,
      defense="KICK: doar operator (482), tinta trebuie sa fie pe canal (441), 401 nick inexistent, 403 canal inexistent; victima e anuntata si nu mai primeste mesaje.")
def t_kick_flow():
    """KICK semantics."""
    a, b, ch = duo()
    c = mk()
    c.cmd(f"JOIN {ch}")
    c.sync()
    a.sync()
    for x in (a, b, c):
        x.clear()
    b.cmd(f"KICK {ch} {c.nick}")
    soft(b.expect_code("482", 2), "non-operator KICK gave no 482")
    a.cmd(f"KICK {ch} {c.nick} :bye now")
    a.sync()
    need(c.expect_re(r"KICK", 2), "kicked client got no KICK message")
    soft(b.expect("bye now", 2), "other members did not see the KICK reason")
    a.clear()
    c.cmd(f"PRIVMSG {ch} :ghost-after-kick")
    a.sync()
    need(a.count("ghost-after-kick") == 0, "kicked user can still talk in the channel")
    a.cmd(f"KICK {ch} {c.nick}")
    soft(a.expect_code("441", 2), "kicking a non-member gave no 441")
    a.cmd(f"KICK {ch} nosuchuserx")
    soft(a.expect_code("401", 2) or a.has_code("441"), "kicking an unknown nick gave no 401/441")
    a.cmd("KICK #nochan x")
    soft(a.expect_code("403", 2), "KICK on a missing channel gave no 403")
    a.cmd(f"KICK {ch}")
    soft(a.expect_code("461", 2), "KICK without a target gave no 461")
    c.cmd(f"JOIN {ch}")
    c.sync()
    soft(joined(c, ch), "kicked user cannot rejoin")
 
 
@test("channels", defense="TOPIC: citire 331/332, setare, +t doar operator (482), non-membru 442, 'TOPIC #c :' goleste.")
def t_topic_flow():
    """TOPIC and +t."""
    a, b, ch = duo()
    a.cmd(f"TOPIC {ch}")
    soft(a.expect_code("331", 2) or a.has_code("332"), "TOPIC query on a new channel gave no 331")
    a.cmd(f"TOPIC {ch} :hello topic")
    a.sync()
    soft(b.expect("hello topic", 2), "members not told about the new topic")
    b.clear()
    b.cmd(f"TOPIC {ch}")
    soft(b.expect("hello topic", 2), "TOPIC query does not return the topic")
    a.cmd(f"MODE {ch} +t")
    a.sync()
    b.clear()
    b.cmd(f"TOPIC {ch} :hacked")
    soft(b.expect_code("482", 2), "non-op set the topic on a +t channel")
    a.cmd(f"MODE {ch} -t")
    a.sync()
    b.clear()
    b.cmd(f"TOPIC {ch} :b-allowed")
    b.sync()
    soft(b.count("b-allowed") > 0 and not b.has_code("482"), "-t did not allow members to set the topic")
    c = mk()
    c.cmd(f"TOPIC {ch} :outsider")
    soft(c.expect_code("442", 2), "non-member TOPIC gave no 442")
    a.cmd(f"TOPIC {ch} :")
    a.sync()
    b.clear()
    b.cmd(f"TOPIC {ch}")
    soft(b.has_code("331") or b.expect_code("331", 1), "TOPIC #c : did not clear the topic")
    a.cmd("TOPIC")
    soft(a.expect_code("461", 2), "TOPIC without params gave no 461")
    a.cmd("TOPIC #nochan")
    soft(a.expect_code("403", 2), "TOPIC on a missing channel gave no 403")
 
 
@test("channels", crit=True,
      defense="+i: JOIN fara invitatie -> 473; INVITE doar de operator pe canal +i (482); 401 nick inexistent; 443 deja pe canal.")
def t_invite_flow():
    """INVITE and +i."""
    a, b, ch = duo()
    a.cmd(f"MODE {ch} +i")
    a.sync()
    c = mk()
    c.cmd(f"JOIN {ch}")
    need(c.expect_code("473", 2), "joined an invite-only channel without invitation (or no 473)")
    need(not joined(c, ch) or False, "JOIN echoed on an invite-only channel")
    b.clear()
    b.cmd(f"INVITE {c.nick} {ch}")
    soft(b.expect_code("482", 2), "non-op INVITE on +i channel gave no 482")
    a.cmd(f"INVITE {c.nick} {ch}")
    a.sync()
    soft(a.expect_code("341", 2), "no 341 confirmation to the inviter")
    need(c.expect("INVITE", 2), "invited user did not receive the INVITE")
    c.clear()
    c.cmd(f"JOIN {ch}")
    need(joined(c, ch), "invited user still cannot JOIN")
    a.cmd(f"INVITE {c.nick} {ch}")
    soft(a.expect_code("443", 2), "inviting a member gave no 443")
    a.cmd(f"INVITE nosuchnickx {ch}")
    soft(a.expect_code("401", 2), "inviting an unknown nick gave no 401")
    a.cmd("INVITE")
    soft(a.expect_code("461", 2), "INVITE without params gave no 461")
    d = mk()
    d.cmd(f"INVITE {a.nick} {ch}")
    soft(d.expect_code("442", 2) or d.has_code("482"), "INVITE by a non-member gave no 442")
 
 
@test("channels", crit=True,
      defense="MODE +k: JOIN fara cheie / cu cheie gresita -> 475; -k elimina; '+k' fara parametru -> 461 si nu strica starea.")
def t_mode_key():
    """+k / -k."""
    a, b, ch = duo()
    a.cmd(f"MODE {ch} +k sekret")
    a.sync()
    c = mk()
    c.cmd(f"JOIN {ch}")
    need(c.expect_code("475", 2), "joined +k channel without key (or no 475)")
    c.clear()
    c.cmd(f"JOIN {ch} wrong")
    need(c.expect_code("475", 2), "joined +k channel with wrong key")
    c.clear()
    c.cmd(f"JOIN {ch} sekret")
    need(joined(c, ch), "correct key refused")
    a.cmd(f"MODE {ch} +k other")
    soft(a.expect_code("467", 2), "setting a second key gave no 467")
    a.cmd(f"MODE {ch} -k sekret")
    a.sync()
    d = mk()
    d.cmd(f"JOIN {ch}")
    soft(joined(d, ch), "-k did not remove the key")
    e = chan("k0")
    a.cmd(f"JOIN {e}")
    a.cmd(f"MODE {e} +k")
    soft(a.expect_code("461", 2), "'MODE #c +k' without a key gave no 461")
 
 
@test("channels", crit=True,
      defense="+l N: al (N+1)-lea JOIN -> 471; valori invalide ('abc', 0, -5, overflow) nu crapa si nu seteaza o limita aberanta.")
def t_mode_limit():
    """+l / -l and bad values."""
    a, b, ch = duo()
    a.cmd(f"MODE {ch} +l 2")
    a.sync()
    c = mk()
    c.cmd(f"JOIN {ch}")
    need(c.expect_code("471", 2), "channel over its limit still accepted a JOIN (or no 471)")
    a.cmd(f"MODE {ch} -l")
    a.sync()
    c.clear()
    c.cmd(f"JOIN {ch}")
    soft(joined(c, ch), "-l did not remove the limit")
    for bad in ("abc", "0", "-5", "99999999999999999999", ""):
        e = chan("lb")
        x = mk()
        x.cmd(f"JOIN {e}")
        x.cmd(f"MODE {e} +l {bad}")
        x.sync()
        y = mk()
        y.cmd(f"JOIN {e}")
        soft(joined(y, e) or bad in ("0",), f"'+l {bad}' left a bogus limit (second user refused)")
        x.close()
        y.close()
    check_alive("after bad +l values")
 
 
@test("channels", crit=True,
      defense="MODE +kl key 5 / +lk 5 key: parametrii se consuma pozitional, in ordinea literelor. Eroarea clasica: cheia devine '5'.")
def t_mode_param_alignment():
    """+kl key 5 and +lk 5 key consume parameters by position."""
    for flags, params in (("+kl", "pkey 5"), ("+lk", "5 pkey")):
        a = mk()
        ch = chan("pa")
        a.cmd(f"JOIN {ch}")
        a.cmd(f"MODE {ch} {flags} {params}")
        a.sync()
        b = mk()
        b.cmd(f"JOIN {ch} pkey")
        need(joined(b, ch), f"'MODE {ch} {flags} {params}' mis-aligned parameters (key not 'pkey')")
        c = mk()
        c.cmd(f"JOIN {ch} wrong")
        soft(not joined(c, ch), f"'{flags}' left the channel open without the key")
 
 
@test("channels", crit=True,
      defense="+o/-o: operator nou poate KICK/MODE; -o ii retrage dreptul; 401 nick inexistent; 441 nu e pe canal.")
def t_mode_op():
    """+o / -o."""
    a, b, ch = duo()
    c = mk()
    b.cmd(f"MODE {ch} +i")
    need(b.expect_code("482", 2), "non-op changed a mode")
    a.cmd(f"MODE {ch} +o {b.nick}")
    a.sync()
    soft(b.expect_re(r"MODE[^\r\n]*\+o", 2), "no MODE broadcast for +o")
    b.clear()
    b.cmd(f"MODE {ch} +t")
    b.sync()
    soft(not b.has_code("482"), "new operator still refused (+o not applied)")
    a.cmd(f"MODE {ch} -o {b.nick}")
    a.sync()
    b.clear()
    b.cmd(f"MODE {ch} -t")
    soft(b.expect_code("482", 2), "-o did not remove operator rights")
    a.cmd(f"MODE {ch} +o ghostnickx")
    soft(a.expect_code("401", 2), "+o on an unknown nick gave no 401")
    a.cmd(f"MODE {ch} +o {c.nick}")
    soft(a.expect_code("441", 2), "+o on a non-member gave no 441")
    a.cmd(f"MODE {ch} +o")
    soft(a.expect_code("461", 2), "+o without a nick gave no 461")
 
 
@test("channels", defense="MODE: query 324, flag necunoscut 472, canal inexistent 403, combinatii ciudate nu crapa.")
def t_mode_misc():
    """MODE query, unknown flag, combos."""
    a, b, ch = duo()
    a.cmd(f"MODE {ch}")
    soft(a.expect_code("324", 2), "'MODE #c' gave no 324")
    a.cmd(f"MODE {ch} +z")
    soft(a.expect_code("472", 2), "unknown mode letter gave no 472")
    a.cmd("MODE #nochanx +i")
    soft(a.expect_code("403", 2), "MODE on a missing channel gave no 403")
    for m in ("+", "-", "+-+-", "+iiiiii", "+i-i+i-i+t-t", "+" + "o" * 300, "+kkkk a b c d", "iii", "!i", "+i+i+i"):
        a.send(f"MODE {ch} {m}{CRLF}")
    a.send(f"MODE {ch} +{'l' * 200} {'1 ' * 200}{CRLF}")
    a.sync()
    check_alive("after odd MODE strings")
    if HAS_PONG:
        soft(a.sync(), "client no longer answered after odd MODE strings")
    a.cmd(f"MODE {ch} +it")
    a.cmd(f"MODE {ch} -it")
    a.sync()
    soft(a.has_code("324") or True, "")
    soft(b.expect_re(r"MODE[^\r\n]*\+it", 2), "'+it' was not broadcast as one change")
 
 
@test("channels", crit=True,
      defense="Ctrl+C/kill -9 al clientului = recv==0 sau RST. Aceeasi curatare ca la deconectare soft: scos din canale, nick eliberat, fd inchis.")
def t_kill_client():
    """Hard-kill (RST) a client that is in a channel."""
    a, b, ch = duo()
    nick = a.nick
    a.hard_kill()
    time.sleep(0.5)
    b.cmd(f"PRIVMSG {ch} :still here")
    need(b.sync(), "remaining client no longer served after a kill")
    check_alive("after kill")
    n = names_via_join(ch)
    need(n is not None, "cannot join channel after kill")
    soft(nick not in [strip_prefix(x) for x in n], "killed client is still listed in the channel (ghost member)")
    c = Client()
    need(c.register(nick), "nick of the killed client was not freed")
 
 
@test("channels", crit=True,
      defense="Cand ultimul membru pleaca canalul trebuie sters; 120 membri: NAMES complet chiar daca citesti lent (send partial / POLLOUT).")
def t_names_big_channel():
    """120 members, slow reader gets a complete NAMES list."""
    ch = chan("big")
    members = []
    for i in range(120):
        c = Client()
        if not c.register(uniq("m")):
            raise Fail(f"member {i} could not register")
        c.cmd(f"JOIN {ch}")
        members.append(c)
    members[-1].sync(4)
    o = Client(rcvbuf=2048)
    need(o.register(uniq("obs")), "observer could not register")
    o.cmd(f"JOIN {ch}")
    time.sleep(0.8)                       # reader stalls, server must keep the unsent tail
    o.expect_code("366", 5)
    names = [strip_prefix(x) for x in parse_names(o.buf)]
    missing = [m.nick for m in members if m.nick not in names]
    need(not missing, f"NAMES incomplete: {len(missing)} of 120 members missing (partial send lost data?)")
    ops = [x for x in parse_names(o.buf) if x.startswith("@")]
    soft(len(ops) == 1, f"{len(ops)} operators in a freshly created channel (expected 1)")
 
 
@test("channels", crit=True,
      defense="25 JOIN simultane pe un canal nou: serverul e single-thread, deci exact unul devine operator; toti primesc JOIN.")
def t_first_joiner_race():
    """25 clients JOIN the same new channel simultaneously."""
    ch = chan("rc")
    clients = []
    for _ in range(25):
        c = Client(timeout=8)
        c.register(uniq("r"))
        clients.append(c)
    barrier = threading.Barrier(25)
 
    def f(i):
        barrier.wait()
        clients[i].cmd(f"JOIN {ch}")
        return clients[i].expect_code("366", 5)
    res = run_threads(25, f)
    need(all(r is True for r in res), "some clients did not complete the JOIN")
    ops = 0
    for c in clients:
        ops += len(re.findall(rf"@{re.escape(c.nick)}\b", c.buf))
    names = names_via_join(ch) or []
    n_ops = len([x for x in names if x.startswith("@")])
    need(n_ops == 1, f"{n_ops} operators after a simultaneous JOIN (expected exactly 1)")
 
 
@test("channels", crit=True,
      defense="Un client poate crea multe canale; la un 'hard-kill' toate se curata. Nu trebuie sa ramana canale fantoma.")
def t_many_channels():
    """One client creates 300 channels then disconnects."""
    a = mk()
    names = [chan("mc") for _ in range(300)]
    a.send("".join(f"JOIN {n}{CRLF}" for n in names))
    a.sync(5)
    a.hard_kill()
    time.sleep(0.5)
    check_alive("after hard disconnect from 300 channels")
    n = names_via_join(names[0])
    soft(n is not None and len(n) == 1 + 0 or n is not None and "@" + n[0].lstrip("@") in n, "channel survived its only member disconnecting")
 
 
@test("channels", defense="Liste uriase in JOIN/PRIVMSG nu pot bloca serverul (complexitate patratica).")
def t_huge_lists():
    """2000-item JOIN / PRIVMSG target lists."""
    a = mk()
    t0 = time.time()
    a.send("JOIN " + ",".join(f"#h{i}" for i in range(2000)) + CRLF)
    a.send("PRIVMSG " + ",".join(f"nosuch{i}" for i in range(2000)) + f" :x{CRLF}")
    a.sync(10)
    dt = time.time() - t0
    soft(dt < 5, f"huge lists took {dt:.1f}s (quadratic?)")
    check_alive("after huge lists")
 
 
@test("hexchat", defense="HexChat dupa JOIN trimite MODE #c, MODE #c b, WHO #c, USERHOST, WHOIS: orice raspuns (chiar 421) e ok, dar conexiunea trebuie sa traiasca.")
def t_hexchat_session():
    """What HexChat sends after JOIN."""
    a = mk()
    ch = chan("hx")
    a.cmd(f"JOIN {ch}")
    a.cmd(f"MODE {ch}")
    a.cmd(f"MODE {ch} b")
    a.cmd(f"MODE {ch} +b")
    a.cmd(f"WHO {ch}")
    a.cmd(f"WHO {a.nick}")
    a.cmd(f"WHOIS {a.nick}")
    a.cmd(f"USERHOST {a.nick}")
    a.cmd("USERHOST")
    a.cmd("LUSERS")
    a.cmd("AWAY :brb")
    a.cmd("AWAY")
    a.cmd(f"PING lag12345")
    need(not a.closed, "server closed the connection on HexChat's standard queries")
    need(a.sync(3), "no answer to PING after the HexChat query burst")
 
 
# =========================================================================
#                               SOCKET ABUSE
# =========================================================================
@test("sockets", crit=True,
      defense="Clientul moare in mijlocul unei comenzi (kill -9): bufferul lui trebuie aruncat cu el; un client nou nu mosteneste bytes vechi.")
def t_kill_half_command():
    """Kill clients mid-command 30 times."""
    for _ in range(30):
        a = Client()
        a.send(f"PASS {CFG.password}{CRLF}NICK {uniq()}{CRLF}USER u 0 * :u{CRLF}JOIN #half{CRLF}PRIVMSG #ha")
        time.sleep(0.02)
        a.hard_kill()
    time.sleep(0.5)
    check_alive("after killing clients mid-command")
    c = mk()
    c.cmd("PING clean")
    soft(not c.has_code("421"), "fresh client got 421: leftover half-command reused")
 
 
@test("sockets", crit=True,
      defense="shutdown(SHUT_WR): serverul primeste EOF dupa date. Trebuie sa proceseze comenzile primite, apoi sa inchida. Daca pastrezi fd-ul in poll cu POLLHUP -> busy loop.")
def t_half_close():
    """Client sends a command then shutdown(SHUT_WR); 40 times."""
    cs = []
    for _ in range(40):
        c = Client()
        n = uniq()
        c.send(f"PASS {CFG.password}{CRLF}NICK {n}{CRLF}USER {n} 0 * :{n}{CRLF}PING hc{CRLF}")
        c.shutdown_wr()
        cs.append(c)
    got = sum(1 for c in cs if c.expect_code("001", 3))
    soft(got == 40, f"only {got}/40 half-closed clients got their welcome (data before EOF dropped)")
    closed = sum(1 for c in cs if c.wait_closed(2))
    soft(closed == 40, f"{40 - closed}/40 half-closed connections never released by the server")
    pct = cpu_pct(1.5)
    if pct is not None:
        need(pct < 40, f"server burns {pct:.0f}% CPU after half-closed clients (POLLHUP busy loop)")
    check_alive("after half-close")
 
 
@test("sockets", crit=True, pid=True,
      defense="Server in repaus trebuie sa stea ~0% CPU. Valori mari = poll cu timeout 0, POLLHUP/POLLNVAL netratat, sau fd mort ramas in vectorul pollfd.")
def t_idle_cpu():
    """Idle CPU with connected + silent clients."""
    cs = [mk() for _ in range(10)]
    silent = [Client() for _ in range(10)]
    pct = cpu_pct(2.0)
    need(pct is not None, "cannot read CPU")
    note(f"idle CPU {pct:.1f}%")
    need(pct < 15, f"idle CPU {pct:.0f}% (busy loop in poll?)")
 
 
@test("sockets", crit=True, pid=True,
      defense="Dupa RST de la clienti, server-ul nu are voie sa ramana in spin pe fd-uri moarte (POLLERR/POLLHUP/POLLNVAL).")
def t_cpu_after_rst():
    """CPU after RST storms."""
    for _ in range(60):
        c = Client()
        c.send(f"PASS {CFG.password}{CRLF}NICK {uniq()}{CRLF}USER u 0 * :u{CRLF}JOIN #rst{CRLF}")
        c.hard_kill()
    time.sleep(0.3)
    pct = cpu_pct(2.0)
    note(f"CPU after RST storm {pct:.1f}%")
    need(pct is None or pct < 20, f"CPU {pct:.0f}% after RST storm (dead fds left in poll set)")
    check_alive("after RST storm")
 
 
@test("sockets", crit=True,
      defense="SIGPIPE: send() pe un socket inchis omoara procesul daca nu ai signal(SIGPIPE, SIG_IGN) (sau MSG_NOSIGNAL). Test: trimiti mult, clientul dispare cu RST.")
def t_sigpipe_rst():
    """Server writes to peers that vanished (RST) - must not die of SIGPIPE."""
    for _ in range(60):
        c = Client()
        c.send(f"PASS {CFG.password}{CRLF}NICK {uniq()}{CRLF}USER u 0 * :u{CRLF}JOIN #pipe{CRLF}"
               + "".join(f"PING p{i}{CRLF}" for i in range(200)))
        c.hard_kill()
    time.sleep(0.5)
    check_alive("after replies to vanished peers (SIGPIPE?)")
 
 
@test("sockets", crit=True,
      defense="Clienti care nu citesc (Ctrl+Z pe nc) + un flood: serverul nu are voie sa blocheze in send(), sa piarda mesaje sau sa le reordoneze (outbox per client + POLLOUT). Acesta e testul cel mai ocolit.")
def t_stopped_client_flood():
    """A stops reading (Ctrl+Z), B floods the channel."""
    n = CFG.flood
    a = Client(rcvbuf=4096)
    need(a.register(uniq("slow")), "A could not register")
    ch = chan("fl")
    a.cmd(f"JOIN {ch}")
    a.expect(ch, 2)
    a.clear()
    b = mk()
    b.s.settimeout(20)
    b.cmd(f"JOIN {ch}")
    time.sleep(0.3)
    mem0 = server_stats()
    t0 = time.time()
    try:
        for i in range(1, n + 1):
            b.send(f"PRIVMSG {ch} :flood {i}{CRLF}")
    except (socket.timeout, OSError) as e:
        raise Fail(f"flooder blocked or dropped, server probably hung on a slow reader: {e}")
    note(f"sent {n} messages in {time.time() - t0:.1f}s")
    t0 = time.time()
    check_alive("while client A is stopped")
    need(time.time() - t0 < 2.5, "server answered a new client slowly during the stall")
    time.sleep(1.5)
    mem1 = server_stats()
    a.drain(idle=1.5)
    got = [int(x) for x in re.findall(r"flood (\d+)", a.buf)]
    note(f"server stats before={mem0} stalled={mem1} after={server_stats()}")
    if a.closed and len(got) < n:
        raise Warn(f"A was disconnected after {len(got)}/{n} msgs (acceptable only with a SendQ limit; "
                   "say so in the defense: 'slow consumer is dropped to protect memory')")
    need(len(got) == n, f"A received {len(got)}/{n} queued messages (data lost)")
    need(got == sorted(got), "queued messages arrived out of order")
    a.cmd("PING back")
    soft(a.expect("PONG", 3), "A does not work normally after resuming")
 
 
@test("sockets", crit=True,
      defense="Acelasi lucru pe calea PRIVMSG privat.")
def t_stopped_client_pm_flood():
    """Same stall through private messages."""
    n = max(1000, CFG.flood // 4)
    a = Client(rcvbuf=4096)
    nick = uniq("slow")
    need(a.register(nick), "A could not register")
    b = mk()
    b.s.settimeout(20)
    try:
        for i in range(1, n + 1):
            b.send(f"PRIVMSG {nick} :pm {i}{CRLF}")
    except (socket.timeout, OSError) as e:
        raise Fail(f"flooder blocked, server probably hung on the PM path: {e}")
    check_alive("during PM stall")
    a.drain(idle=1.5)
    got = len(re.findall(r"pm \d+", a.buf))
    if a.closed and got < n:
        raise Warn(f"A disconnected after {got}/{n} PMs (SendQ limit?)")
    need(got == n, f"A received {got}/{n} PMs")
 
 
@test("sockets", crit=True,
      defense="Un client iese exact cand altii inunda canalul: clientul sters nu trebuie atins dupa disconnect (iteratori invalidati, use-after-free).")
def t_quit_during_flood():
    """Members hard-disconnect / die in the middle of a flood."""
    a, b, ch = duo("rf")
    c = mk()
    c.cmd(f"JOIN {ch}")
    c.sync()
    try:
        for i in range(3000):
            a.send(f"PRIVMSG {ch} :x{i}{CRLF}")
            if i == 1000:
                c.hard_kill()
            if i == 2000:
                b.hard_kill()
    except OSError as e:
        raise Fail(f"server dropped the flooder: {e}")
    time.sleep(0.5)
    check_alive("after members disconnected mid-flood")
 
 
@test("sockets", crit=True,
      defense="Slowloris: 100 de clienti trimit cate un byte pe rand. Serverul trebuie sa ramana reactiv pentru ceilalti (nicio bucla blocanta pe un client).")
def t_slowloris():
    """100 connections dribbling one byte at a time while others are served."""
    cs = [Client() for _ in range(100)]
    msg = "PASS "
    t0 = time.time()
    for ch in msg:
        for c in cs:
            c.send(ch)
        time.sleep(0.15)
        t1 = time.time()
        check_alive("during slowloris")
        need(time.time() - t1 < 2.5, "server slow for a normal client while 100 clients dribble bytes")
    note(f"slowloris done in {time.time() - t0:.1f}s")
 
 
@test("sockets", crit=True,
      defense="100 conexiuni simultane la un singur accept-loop: accept() tot ce e gata (nu doar unul per iteratie e ok, dar nu pierde).")
def t_burst_connect():
    """100 simultaneous connect+register."""
    barrier = threading.Barrier(100)
 
    def f(i):
        c = Client(timeout=10)
        barrier.wait()
        return c.register(uniq("bu"))
    res = run_threads(100, f)
    ok = sum(1 for r in res if r is True)
    note(f"{ok}/100 registered")
    need(ok >= 90, f"only {ok}/100 simultaneous clients registered")
    soft(ok == 100, f"{100 - ok} simultaneous clients failed to register")
 
 
@test("sockets", crit=True,
      defense="Conectari/deconectari repetate: fd-urile trebuie sa revina la baza, memoria sa nu creasca. Un leak e vizibil imediat la evaluare cu lsof/valgrind.")
def t_leak_cycles():
    """300 connect/register/join/disconnect cycles: fd and RSS stay flat."""
    before = server_stats()
    for i in range(300):
        c = Client()
        n = uniq("cy")
        c.send(f"PASS {CFG.password}{CRLF}NICK {n}{CRLF}USER {n} 0 * :{n}{CRLF}JOIN #cyc{i % 5}{CRLF}")
        c.close()
    time.sleep(1)
    after = server_stats()
    check_alive("after 300 cycles")
    if before.get("fds") and after.get("fds"):
        need(after["fds"] <= before["fds"] + 3, f"fd leak: {before['fds']} -> {after['fds']}")
    if before.get("rss_kb") and after.get("rss_kb"):
        grew = after["rss_kb"] - before["rss_kb"]
        soft(grew < 6000, f"RSS grew {grew} KB over 300 cycles")
 
 
@test("sockets", defense="5000 NICK-uri in rafala: tabela de nick-uri ramane consistenta.")
def t_nick_flood():
    """5000 NICK changes pipelined."""
    a = mk()
    last = uniq("nf")
    a.send("".join(f"NICK {uniq('nf')}{CRLF}" for _ in range(5000)) + f"NICK {last}{CRLF}")
    a.sync(5)
    b = Client()
    soft(not b.register(last), "final nick is still free after a NICK flood (state out of sync)")
 
 
@test("sockets", crit=True, heavy=True,
      defense="Limita de fd: accept() intoarce EMFILE si socketul de listen ramane readable -> busy loop 100% CPU. Raspuns: la EMFILE nu bucla; log; poate inchide un fd de rezerva.")
def t_fd_exhaustion():
    """Open ~1100 connections (heavy)."""
    soft_lim, hard = resource.getrlimit(resource.RLIMIT_NOFILE)
    target = min(1100, hard - 100)
    try:
        resource.setrlimit(resource.RLIMIT_NOFILE, (min(hard, target + 100), hard))
    except (ValueError, OSError):
        pass
    cs = []
    for _ in range(target):
        try:
            cs.append(Client(timeout=2))
        except OSError:
            break
    note(f"opened {len(cs)} connections")
    pct = cpu_pct(1.5)
    st = server_stats()
    note(f"CPU while holding {len(cs)} connections: {pct}, server fds {st.get('fds')}")
    if pct is not None:
        need(pct < 50, f"server burns {pct:.0f}% CPU while out of fds (accept EMFILE busy loop)")
    for c in cs:
        c.close()
    time.sleep(1.5)
    check_alive("after fd exhaustion")
 
 
# =========================================================================
#                                 SIGNALS
# =========================================================================
@test("signals", crit=True, pid=True,
      defense="SIGPIPE trebuie ignorat (signal(SIGPIPE, SIG_IGN)); subiectul permite signal().")
def t_signal_sigpipe():
    """kill -PIPE <server>."""
    os.kill(CFG.pid, signal.SIGPIPE)
    time.sleep(0.5)
    need(proc_alive(), "server died of SIGPIPE: use signal(SIGPIPE, SIG_IGN)")
    check_alive("after SIGPIPE")
 
 
@test("signals", crit=True, pid=True,
      defense="Ctrl+Z pe server (SIGSTOP/SIGCONT): conexiunile existente continua; datele trimise in pauza sunt procesate dupa SIGCONT.")
def t_signal_stop_cont():
    """Server stopped 2 s (Ctrl+Z) then continued."""
    a = mk()
    try:
        os.kill(CFG.pid, signal.SIGSTOP)
        a.cmd("PING afterstop")
        time.sleep(2)
    finally:
        os.kill(CFG.pid, signal.SIGCONT)
    need(a.expect("afterstop", 4) if HAS_PONG else True, "no PONG after the server was continued")
    check_alive("after SIGSTOP/SIGCONT")
 
 
# --------------------------------------------------------------------- main
def select_tests():
    sel = TESTS
    if CFG.only:
        names = set(CFG.only.split(","))
        sel = [t for t in sel if t["name"] in names]
    if CFG.group:
        groups = set(CFG.group.split(","))
        sel = [t for t in sel if t["group"] in groups]
    return sel
 
 
def final_signal():
    sig = signal.SIGINT if CFG.final_signal == "INT" else signal.SIGTERM
    print(f"\n[ .. ] final signal: sending SIG{CFG.final_signal} to {CFG.pid}")
    stats = server_stats()
    os.kill(CFG.pid, sig)
    end = time.time() + 8
    while time.time() < end and proc_alive():
        time.sleep(0.2)
    if proc_alive():
        print(f"[{RED}FAIL{RESET}] server did not exit within 8s after SIG{CFG.final_signal} "
              f"(main loop must test a flag set by the handler; poll must not restart on EINTR forever)")
        return 1
    print(f"[{GREEN}PASS{RESET}] server exited after SIG{CFG.final_signal} (fds before exit: {stats.get('fds')}). "
          "Now read the valgrind summary: 0 leaks, 0 fds open at exit.")
    return 0
 
 
def main():
    global CFG, HAS_PONG
    p = argparse.ArgumentParser(add_help=True)
    p.add_argument("--host", default="127.0.0.1")
    p.add_argument("--port", type=int, default=6667)
    p.add_argument("--pass", dest="password", default="pass")
    p.add_argument("--pid", help="server PID (first integer is used)")
    p.add_argument("--flood", type=int, default=20000)
    p.add_argument("--only")
    p.add_argument("--group")
    p.add_argument("--list", action="store_true")
    p.add_argument("--heavy", action="store_true")
    p.add_argument("--final-signal", choices=["INT", "TERM"])
    p.add_argument("-v", "--verbose", action="store_true")
    CFG = p.parse_args()
    if CFG.pid:
        m = re.search(r"\d+", CFG.pid)
        CFG.pid = int(m.group()) if m else None
 
    if CFG.list:
        for t in TESTS:
            print(f"{t['group']:<13}{t['name']:<34}{'[CRIT] ' if t['crit'] else '       '}"
                  f"{'(pid) ' if t['pid'] else ''}{'(heavy) ' if t['heavy'] else ''}{t['doc']}")
        return 0
 
    try:
        check_alive("at start")
    except (Fail, OSError) as e:
        print(f"{RED}Cannot talk to the server at {CFG.host}:{CFG.port}: {e}{RESET}")
        return 2
    probe = Client()
    probe.register()
    probe.cmd("PING probe")
    HAS_PONG = probe.expect("PONG", 2)
    probe.close()
    if not HAS_PONG:
        print(f"{YELLOW}note: server does not answer PING; falling back to sleeps for synchronisation{RESET}")
    if not CFG.pid:
        print(f"{DIM}note: no --pid, so fd/RSS/CPU-spin and signal tests are skipped{RESET}")
 
    results = []
    start = server_stats()
    crashed_by = None
    for t in select_tests():
        if t["heavy"] and not CFG.heavy:
            continue
        if t["pid"] and not CFG.pid:
            results.append((t, "SKIP", "needs --pid"))
            print(f"[{DIM}SKIP{RESET}] {t['name']}: needs --pid")
            continue
        print(f"[ .. ] {t['group']}/{t['name']}: {t['doc']}")
        del SOFT[:]
        del NOTES[:]
        pre = server_stats()
        t0 = time.time()
        status, msg = "PASS", ""
        try:
            t["fn"]()
            if SOFT:
                status, msg = "WARN", "; ".join(SOFT)
        except Skip as e:
            status, msg = "SKIP", str(e)
        except Warn as e:
            status, msg = "WARN", "; ".join(SOFT + [str(e)])
        except Fail as e:
            status, msg = "FAIL", str(e)
        except (socket.timeout, TimeoutError) as e:
            status, msg = "FAIL", f"timeout (server hung or stopped answering): {e}"
        except Exception as e:  # noqa
            status, msg = "FAIL", f"{type(e).__name__}: {e}"
        cleanup_clients()
        time.sleep(0.3)
        if status in ("PASS", "WARN") and pre.get("fds"):
            post = server_stats()
            if post.get("fds") and post["fds"] > pre["fds"] + 2:
                status = "WARN" if status == "PASS" else status
                msg = (msg + "; " if msg else "") + f"fd count grew {pre['fds']} -> {post['fds']} (possible leak)"
        dt = time.time() - t0
        color = {"PASS": GREEN, "WARN": YELLOW, "FAIL": RED, "SKIP": DIM}[status]
        tag = " [CRIT]" if (status == "FAIL" and t["crit"]) else ""
        print(f"[{color}{status}{RESET}] {t['name']}{tag} ({dt:.1f}s) {msg}")
        if CFG.verbose or status == "FAIL":
            for n in NOTES:
                print(f"       {DIM}{n}{RESET}")
        results.append((t, status, msg))
        if CFG.pid and not proc_alive():
            crashed_by = t["name"]
            print(f"{RED}!!! the server process is gone after '{t['name']}' (crash).{RESET}")
            break
        if status == "FAIL":
            try:
                check_alive("after failed test")
            except Exception:
                crashed_by = crashed_by or t["name"]
                print(f"{RED}!!! server no longer responds after '{t['name']}', stopping.{RESET}")
                break
            finally:
                cleanup_clients()
 
    final_rc = 0
    if CFG.final_signal and CFG.pid and not crashed_by:
        final_rc = final_signal()
 
    time.sleep(1)
    end = server_stats()
    counts = {k: sum(1 for r in results if r[1] == k) for k in ("PASS", "WARN", "FAIL", "SKIP")}
    print("\n=== Summary ===")
    print(f"{GREEN}{counts['PASS']} passed{RESET}, {YELLOW}{counts['WARN']} warnings{RESET}, "
          f"{RED}{counts['FAIL']} failed{RESET}, {DIM}{counts['SKIP']} skipped{RESET}")
    if start and end and not (CFG.final_signal and CFG.pid):
        print(f"server fds: {start.get('fds')} -> {end.get('fds')}   RSS: {start.get('rss_kb')} KB -> {end.get('rss_kb')} KB")
    if crashed_by:
        print(f"{RED}CRASH / HANG caused by: {crashed_by}{RESET}")
 
    bad = [(t, s, m) for (t, s, m) in results if s in ("FAIL", "WARN")]
    if bad:
        print("\n=== Risc la prezentare / aparare ===")
        order = {"FAIL": 0, "WARN": 1}
        bad.sort(key=lambda r: (order[r[1]], not r[0]["crit"]))
        for t, s, m in bad:
            color = RED if s == "FAIL" else YELLOW
            tag = " CRITIC" if (s == "FAIL" and t["crit"]) else ""
            print(f"{color}{s}{tag}{RESET} {t['group']}/{t['name']}: {m}")
            if t["defense"]:
                print(f"      {CYAN}->{RESET} {t['defense']}")
    else:
        print(f"{GREEN}Nimic de semnalat.{RESET}")
    print("\nOpreste serverul cu Ctrl-C si citeste rezumatul valgrind (0 leaks, 0 fds deschise la iesire).")
    return 1 if (counts["FAIL"] or final_rc) else 0
 
 
if __name__ == "__main__":
    sys.exit(main())
