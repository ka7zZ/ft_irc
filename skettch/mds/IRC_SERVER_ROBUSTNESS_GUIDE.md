# The IRC server in C++98: robustness, edge cases, design and teamwork

> **Unique Guide**, resulting from the merger of six documents written by two colleagues
> (robustness audits, limit case catalog, design consultations,
> comparison of the two demos). Does not apply changes to sources. Contains
> the analysis, the proposed solutions (C++98 fragments), the required tests and the rules
> teamwork.
>
> **Who it's for:** junior developers entering a project of
> network communications (TCP/IP, sockets, processes). Every problem is
> explained in the order: *what happens → why it cracks → how to fix → how to test*.
>
> **Conventions:** compatible code `-std=c++98 -Wall -Wextra -Werror` (no
> `nullptr` , `unique_ptr` , lambda, move). Cases have stable IDs
> ( `T1…` , `L1…` , `P1…` ) and are mapped to the original documents in Appendix B.

---

## Contents

1. Executive summary
2. Current status: the two demos
3. Basics
4. How to find borderline cases
5. Target architecture
6. Catalog of borderline cases
7. The integrated skeleton of the event loop
8. Implementation order
9. Tests and Tools
10. Teamwork (two people, one repository)
11. Catalog of algorithms and patterns (C++98 and modern)
12. Documentation sources
- Appendix A: Errors and ambiguities corrected from source documents
- Annex B: Traceability (original document → section)

---

## 1. Executive summary

The prototype has good conceptual separation (socket, user, lexer, AST,
dispatcher), but **TCP transport is fragile**. Priority issues:

| # | Problem | Consequence |
|---|---|---|
| 1 | A `recv()` treated as a full command | cut/paste commands, misinterpreted |
| 2 |  `send()` assumed full | messages lost silently |
| 3|  `SIGPIPE` unprotected | process dies on a closed peer |
| 4|  `User` does not release socket | memory and fd leak on each client |
| 5|  `pollfd` /lists modified during browsing | use-after-free, crash |
| 6| Signal handler with STL/ `cout` | undefined behavior |
| 7| No input/output/customer limits | unlimited memory, busy-loop |
| 8| Nickname without validation/uniqueness | inconsistent state |
| 9| Parser not reporting invalid input | Apparently valid AST from broken input |

**Realistic goal:** a faulty or hostile client can be disconnected
controlled, without affecting other clients and without crashing. The server does not
promises to accept any input; promises that he rejects it controlled and that a
client cannot corrupt memory, block others, consume memory
unlimited, to silently lose valid output, to terminate the process ( `SIGPIPE` ) or
to leave fds/objects behind.

### The five principles that solve almost the entire catalog

1. **Don't modify a collection as you go through it.** Mark, then apply by ( `T7` , `L2` , `L4` , `L6` ).
2. **An object that lives in multiple collections has a single "death" point**, with fixed step order ( `L3` ).
3. **Don't keep pointers across multiple steps.** Re-lookup by stable identifier (name, fd) and treat "no longer exists" as a normal case ( `L5` , `L6` ).
4. **Any external input is explicitly validated**: network, startup arguments, numeric parameters ( `P1` , `P5` , `P6` ).
5. **One participant's failure does not bring down the rest of the system**: neither another client nor the whole process ( `T2` , `L5` , `P2` ).

---

## 2. Current status: the two demos

There are two implementation directions, each with complementary strengths.
(The audit `ANALIZA_ROBUSTETE…` refers to a third folder, `skettch` , with
same modular structure as `another_demo` : `User` , `Socket` , `Server` ,
 `CommandDispatcher` . The conclusions for `another_demo` apply there as well;
file differences must be confirmed by the team.)

| Criterion |  `demo_sources` (integrated) |  `another_demo` (modular) |
|---|---|---|
| Architecture | Monolithic: everything in `Server.cpp` | Lexer → ASTParser → CommandDispatcher |
| I/O |  `poll()` , list rebuilt at each iteration |  `poll()` , persistent vector of `pollfd` |
| Framing TCP | **Yes**: buffer per socket, `\r\n` , limits 4096/512 | **No**: a `recv()` = a command |
| Writing | Output buffer, `send()` partial, `MSG_NOSIGNAL` ; **no limit** | Result `send()` ignored; no protection `SIGPIPE` |
| Protocol |  `NICK` , `USER` , `JOIN` , `PING` , numeric `001–004` , `353/366` |  `NICK` , `USER` , `PING` ( non-standard `PONG:` ), `PRIVMSG` partial |
| Channels | Yes (members, initial operator, `NAMES` ); **empty channels remain in map** | There is no |
| Validation | Nick, channel, no. parameters, registration status | Minimum; commands are case-sensitive ( `nick` is not recognized) |
| Ownership | Raw pointers, but coherent cycle |  `User::~User()` does not clear the socket → leak;  `Socket` copyable → double-close risk |
| Strict Build | Compile cleanly | **No**: unused parameters `server` + `-Werror` |
| Testability | High network coupling |  `ISocket` allows mocks |
| Extensibility | Heavy (chain of `if` ) | Hi (table of handlers) |

**Specific problems `demo_sources`:** changing the nickname can
desynchronize indexes; unlimited output; errors `accept()` different from
 `EAGAIN` are ignored;  `atoi` for port;  `JOIN` for one channel only;
public members in `User` / `Channel` ; case-folding approx.

**Specific problems `another_demo` :** missing framing;  `send()` ignored;
 `SIGPIPE` ; socket leak; unsafe signal handler;  `POLLERR/HUP/NVAL` 
untreated;  `NICK` accepts anything;  `USER` incomplete;  `PRIVMSG` to channel
not implemented; unknown commands with no numeric response.

### Verdict

- **For an immediate demonstration:** `demo_sources` .
- **For long-term development:** modular structure from `another_demo` , but only after repairing the foundation.
- **Recommended recipe:** *robust transport and channel model from `demo_sources` + Lexer/Parser/Dispatcher from `another_demo` + explicit ownership + protocol tests.*

---

## 3. Basics

- **File descriptor (fd):** integer by which the kernel identifies a socket/file/pipe. Must be closed **exactly once**: zero times = leak, twice = crash or closing someone else's fd.
- **Listening socket vs. client socket:** the listening one does not carry IRC messages, it just announces via `accept()` that a connection has occurred. Each customer receives their own fd.
   `` `text
  socket → setsockopt(SO_REUSEADDR) → bind → listen → accept
  recv/send … → close
  ` `` 
- ** `recv()` / `send()` ** work with bytes, not "commands". Both are **partial**: `recv()` may give less than there is, `send()` may write less than you asked for.
- **TCP is a stream, not a sequence of packets.** The client can send `PI` + `NG :abc\r\nPING :xyz\r\n` and the server can receive the same data in any chunk. Only `\r\n` marks the end of an IRC command.
- ** `poll()` ** does not read or write. Answers: "which fd is worth trying a non-blocking operation on now?".  `POLLIN` → you can try `recv()` ;  `POLLOUT` → you can try `send()`.
- **IPC:** the mechanism by which separate processes exchange data; a TCP socket is an IPC channel.
- **Iterator invalidation:** you modify a `vector` / `map` while iterating and the iterators/indices become invalid → UB (sometimes crash immediately, sometimes much later).
- **Use-after-free / dangling pointer:** you are using a pointer to an object already `delete` -uit.
- **Async-signal-safe:** function callable safely from a signal handler.  `new` , `malloc` , `std::cout` , any STL method **not** are.
- **Backpressure:** when the recipient does not consume, the producer must stop or be disconnected, not accumulate endlessly.
- **Level-triggered:** `poll()` reports an fd as "ready" **while** the condition persists, not just on change. Consequence: if you don't consume the event, `poll()` returns immediately, ad infinitum (busy-loop). Remember, it is the key of `T6` .

---

## 4. How to find borderline cases

For each function that receives data from the outside (client, `argv` , other server), ask the same six questions:

1. What happens if **data is missing** (absent parameter, empty buffer)?
2. What if there are **too many** (huge line, thousands of connections, message repeated ad infinitum)?
3. What if they are **malformed** (invalid bytes, NULLs, out-of-range numbers, text instead of digits)?
4. What if the operation **only partially succeeds** ( `send` / `recv` )?
5. What if **two events overlap** (client leaves in the middle of processing another command of his; signal in the middle of a container change)?
6. Who **owns** the resource and who is allowed to delete it?

---

## 5. Target architecture

### 5.1 The ownership graph

Golden rule: **one owner for each resource; do not mix "Server delete" pattern with "User delete".**

```text
Server
 ├── Listener (fd de listening, non-blocking)
 ├── std::map<int, User*>   ← Server deține User
 │      └── User            ← User deține Connection
 │            └── Connection ← deține Fd (RAII), input buffer, output queue
 ├── UserRegistry (nick normalizat → User*, NON-owning)
 ├── ChannelRegistry (nume normalizat → Channel*, Server deține Channel)
 └── CommandDispatcher (nume comandă → handler)
```

**Target (clean) variant:** Separate IRC transport and identity.

```text
ClientConnection: socket, input buffer, output queue, stare transport, deadline
UserSession:      nickname, username, realname, hostname, stare autentificare, canale
```

**Pragmatic variant (phase 1):** you keep the class `User` , but explicitly declare
that it owns `Connection` , buffers, recording status, channels and state
closing. The code in this guide uses the pragmatic variant
( `user.connection()` ); refactoring to the target variant is later.

### 5.2 Responsibilities

| Component | Make | **Don't** do |
|---|---|---|
|  `Server` (Facade + Mediator) | config listener, event loop, registry coordination, orderly shutdown | does not parse lines, does not call `send()` , does not validate nicknames |
|  `Listener` |  `socket/setsockopt/bind/listen` , non-blocking, `accept()` to `EAGAIN` | no IRC logic |
|  `Fd` / `Socket` | sole owner of fd ( `fd>=0` ⇒ close) | not copied |
|  `Connection` | fd, `appendInput/nextLine` (framing), `queue/flush` (output) | does not tokenize, does not execute commands |
|  `IrcParser` | full line → `ParseResult{ok,error,…}` | does not touch socket |
|  `CommandDispatcher` | normalize command check registration status delegate respond **via `queue()` ** | do not call `send()` directly |
|  `UserRegistry` | uniqueness, atomic reservation, rename, delete | – |
|  `ChannelRegistry` / `Channel` | members, broadcast, cleaning empty channels | – |

### 5.3 Contracts between components

```cpp
// Transportul produce numai linii IRC complete, fără "\r\n".
Connection::FrameStatus Connection::nextLine(std::string& line);
// Protocolul nu apelează send(): pune răspunsul în coadă.
bool Connection::queue(const std::string& message);
```

Contracts prevent the two classic errors: the parser assuming that a
 `recv()` is a complete command and the dispatcher assumes that `send()` 
send the whole message.

### 5.4 Life cycle (FSM)

```text
Accepted → Connected → Unregistered (PASS/NICK/USER) → Registered → Closing → Closed
```

There are **two types of closing**:

- `markForRemoval(fd)` : error/EOF/ `POLLERR` . Delete at the end of the iteration, without writing any more.
- `markForClosing(fd)` : `QUIT` , input too long. Stops reading, tries to empty output queue (eg goodbye message), then deletes; with **deadline** as a non-reading client not to stay in `Closing` indefinitely.

Both are **idempotent** (two close events for the same fd do not produce a double-delete). The cleanup order is in `L3` .

### 5.5 Invariants to check in code and in review

```text
fiecare fd activ apare o singură dată în poll set
users_by_nick[normalize(user.nick)] == &user
user.channels conține channel  <=>  channel.members conține user
_input.size() <= MAX_PENDING_INPUT ; _outputBytes <= MAX_OUTPUT_BYTES
nicio ștergere de User/Channel în timpul parcurgerii unei colecții
```

### 5.6 Patterns: mandatory vs. second phase

Consultant's advice: *don't choose a pattern because it "sounds good"; choose it
for the exact problem in the audit.* In a small project, over-engineering is a risk.

**Mandatory, in this order:**

| Pattern | Where | Solve |
|---|---|---|
| RAII + non-copyable + Rule of Three |  `Fd` , `Connection` , `Socket` | ownership fd, double-close, leak ( `L1` ) |
| Registry (index) |  `UserRegistry` , `ChannelRegistry` | nick/channel uniqueness, atomic rename ( `P4` ) |
| States (WSF) | registration: `CONNECTED → PASS_OK → NICK_SET → USER_SET → REGISTERED` | orders in wrong order ( `JOIN` before `NICK` ) |
| Command | each IRC command a handler, dispatcher = `map<nume, handler>` |  `if/else` gigantic |

**Phase two (especially useful for unit tests without real TCP):**
Facade ( `Server` ), Mediator ( `Server` between `User` and `Channel` ), Strategy
(validate per command), Factory Method (numeric IRC: `makeNumericError` ,
 `makeWelcome` ), Observer (broadcast), Adapter + Dependency Injection
( `ISocket` mock-able), Non-Virtual Interface, Named Constructor
( `Connection::fromAcceptedSocket` ), Null Object ( `findChannel()==0` ).

---

## 6. Catalog of borderline cases

### 6.0 Taxonomy

| Category | Typical symptom | Cases |
|---|---|---|
| Transport / I/O | lost messages, crash, busy-loop, crash on closed peer |  `T1` – `T8` |
| Lifecycle and memory |  `SIGSEGV` , `SIGABRT` , fd leak |  `L1` – `L6` |
| Protocol and input | UB, `terminate()` , inconsistent state |  `P1` – `P6` |

### 6.1 Common constants (one place, `Limits.hpp` )

```cpp
static const std::size_t MAX_IRC_LINE      = 512;    // inclusiv "\r\n" (RFC 2812)
static const std::size_t MAX_PENDING_INPUT = 4096;   // buffer de intrare per conexiune
static const std::size_t MAX_OUTPUT_BYTES  = 65536;  // buffer de ieșire per conexiune
static const std::size_t MAX_CLIENTS       = 512;    // < (ulimit -n) - rezervate, vezi T6
static const std::size_t MAX_CHANNEL_MEMBERS = 200;
static const int         MAX_LINES_PER_TURN  = 32;   // echitate între clienți
```

---

## Transport and I/O

### T1. TCP Framing: A `recv()` is not a command

**Problem.** `handleClientData()` in `another_demo` gives the received block directly to the lexer.
**Consequences.** Fragmented commands rejected or misinterpreted; two commands in a block become one; input without `\r\n` has no clear behavior. Additionally, `std::string(buffer)` after `recv()` truncates to the first NUL, because the valid length is the value returned by `recv()` .
**Solution.** Persistent buffer per connection.  `recv` adds bytes, the parser gets **only full lines**.

```cpp
// Connection.hpp (extras)
class Connection
{
public:
    enum FrameStatus { FRAME_NEED_MORE, FRAME_LINE, FRAME_TOO_LONG };
    enum IoStatus    { IO_DONE, IO_WOULD_BLOCK, IO_ERROR };

    explicit Connection(int fd) : _fd(fd), _outputBytes(0) {}

    int  fd() const { return _fd.get(); }
    std::size_t inputSpace() const { return MAX_PENDING_INPUT - _input.size(); }
    bool appendInput(const char* data, std::size_t size);
    FrameStatus nextLine(std::string& line);
    bool queue(const std::string& message);   // false => limită depășită
    IoStatus flush();
    bool wantsWrite() const { return _outputBytes > 0; }

private:
    Fd _fd;                                   // RAII, vezi L1
    std::string _input;
    std::deque<std::string> _output;
    std::size_t _outputBytes;

    Connection(const Connection&);            // non-copyable (C++98)
    Connection& operator=(const Connection&);
};
```

```cpp
bool Connection::appendInput(const char* data, std::size_t size)
{
    if (size > MAX_PENDING_INPUT - _input.size())   // fără overflow aritmetic
        return false;
    _input.append(data, size);
    return true;
}

Connection::FrameStatus Connection::nextLine(std::string& line)
{
    std::string::size_type end = _input.find("\r\n");
    if (end == std::string::npos)
        // Fără terminator și deja >= 512 octeți: linia nu mai poate fi validă.
        return (_input.size() >= MAX_IRC_LINE) ? FRAME_TOO_LONG : FRAME_NEED_MORE;
    if (end + 2 > MAX_IRC_LINE)
        return FRAME_TOO_LONG;
    line.assign(_input, 0, end);
    _input.erase(0, end + 2);
    return FRAME_LINE;
}
```

I used a **three-state result**, not `bool` : "no line yet"
(normal) must be distinguished from "line too long" (error) (see Appendix A, #1).

> **Decision to document: `\r\n` strict or tolerant?** RFC requires `\r\n` , but
> `nc` / `telnet` test often send only `\n` . Many servers support `\n` and
> remove a possible `\r` before it. Choose explicitly and write in
> `Server.hpp` ; the tests in §9 assume the chosen behavior.

### T2. Line without terminator: Memory exhaustion through a single socket

**Scenario.** A client sends bytes to infinity, without `\r\n` .
The **real risk** is not just the buffer, but **what happens when the limit is exceeded**: if the response is a `throw` that escapes uncaught, a single client stops the whole server. Worse: If the exception is only caught in the main loop and **client state remains unchanged**, the same buffer produces the same exception on every iteration (*poison pill*: infinite log + 100% CPU).

**Solution.** The exception/error is handled **at the client boundary** and the client is marked for shutdown at the same place.

```cpp
class ProtocolError : public std::runtime_error
{
public:
    explicit ProtocolError(const std::string& m) : std::runtime_error(m) {}
};

void Server::readClient(User& user)
{
    Connection& conn = user.connection();
    std::size_t room = conn.inputSpace();
    if (room == 0)                       // recv(len=0) întoarce 0 = fals EOF!
        return;

    char buffer[4096];
    ssize_t count = ::recv(conn.fd(), buffer,
                           room < sizeof(buffer) ? room : sizeof(buffer), 0);
    if (count == 0)  { markForRemoval(conn.fd()); return; }          // EOF ordonat
    if (count < 0)
    {
        if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
            return;                                                   // T5
        markForRemoval(conn.fd());
        return;
    }
    try
    {
        if (!conn.appendInput(buffer, static_cast<std::size_t>(count)))
            throw ProtocolError("input buffer overflow");
        processLines(user);               // poate arunca ProtocolError (T1: FRAME_TOO_LONG)
    }
    catch (const ProtocolError&)
    {
        conn.queue(makeNumericError(417, "Input line was too long")); // 417 = extensie modernă
        markForClosing(conn.fd());       // din acest moment nu mai citim de la el
    }
    catch (const std::bad_alloc&)
    {
        markForRemoval(conn.fd());
    }
}
```

**General rule:** an error caused by a customer is handled at the border
to that client ( `readClient` ), never allowed to go up the loop
main. This remains only the final net for unforeseen bugs ( `P2` ).

### Q3.  `send()` partial, slow client and backpressure

**Problem.** On non-blocking socket, `send()` can only write part or return `EAGAIN` . Ignored result ⇒ lost messages. Unlimited queue ⇒ a client not reading consumes server memory.
**Solution.** Output queue per client, with limit;  `POLLOUT` active **only** as long as the queue is not empty.

```cpp
bool Connection::queue(const std::string& message)
{
    if (message.size() > MAX_OUTPUT_BYTES - _outputBytes)
        return false;                     // clientul lent va fi deconectat de apelant
    _output.push_back(message);
    _outputBytes += message.size();
    return true;
}

Connection::IoStatus Connection::flush()
{
    while (!_output.empty())
    {
        std::string& cur = _output.front();
        ssize_t sent = ::send(_fd.get(), cur.data(), cur.size(), MSG_NOSIGNAL);
        if (sent > 0)
        {
            _outputBytes -= static_cast<std::size_t>(sent);
            if (static_cast<std::size_t>(sent) == cur.size())
                _output.pop_front();
            else
                cur.erase(0, static_cast<std::size_t>(sent));   // păstrăm restul
            continue;
        }
        if (sent < 0 && errno == EINTR)
            continue;
        if (sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
            return IO_WOULD_BLOCK;        // restul rămâne, așteptăm POLLOUT
        return IO_ERROR;                  // EPIPE, ECONNRESET, altele
    }
    return IO_DONE;
}
```

I chose **status codes**, not exceptions, for `queue` / `flush` (see Appendix A,
#9): the caller decides (marks for close) without a client's error
to cross the stack. The dispatcher only calls `queue()` , never `send()` .
For large output pipelines, replace `std::deque<std::string>` with
single `std::string` + offset to avoid duplicates.

### Q4.  `SIGPIPE` / `EPIPE`

If the peer closed the connection exactly before a `send()` , the kernel may send `SIGPIPE` , and the default action **terminates the process**. Sources recommend `MSG_NOSIGNAL` ; here we recommend **both**, for portability:

```cpp
#ifndef MSG_NOSIGNAL          // macOS/BSD nu îl definesc
# define MSG_NOSIGNAL 0
#endif
// … și la pornire, ignorarea globală (vezi installSignalHandlers în T8):
//    sigaction(SIGPIPE, {SIG_IGN}, 0)
```

Write errors ( `EPIPE` , `ECONNRESET` ) cause the client to close
respectively, not when stopping a broadcast ( `L5` ).

### T5. POSIX results, treated differently

| Result | Meaning | Action |
|---|---|---|
|  `recv > 0` | data available | append in buffer |
|  `recv == 0` | peer closed orderly (EOF) |  `markForRemoval` |
|  `recv < 0` , `EINTR` | interrupted by signal | try again / return to `poll` |
|  `recv < 0` , `EAGAIN` / `EWOULDBLOCK` | are no longer given now | come back to `poll` (**no** it's disconnected) |
|  `recv < 0` , else errno | permanent error |  `markForRemoval` |

Same rules for `send()` . For `accept()` (loop through `EAGAIN` ):

```cpp
void Server::acceptPendingConnections()
{
    for (;;)
    {
        if (_users.size() >= MAX_CLIENTS)      // vezi T6: aici NU doar `break`
        {
            pauseListener();
            return;
        }
        int clientFd = ::accept(_listenFd, 0, 0);
        if (clientFd >= 0)  { registerClient(clientFd); continue; } // setează O_NONBLOCK aici
        if (errno == EINTR) continue;
        if (errno == EAGAIN || errno == EWOULDBLOCK) return;
        handleAcceptFailure(errno);            // EMFILE/ENFILE/ECONNABORTED/… vezi T6
        return;
    }
}
```

### T6. Exhaustion of fds ( `EMFILE` / `ENFILE` ) and customer limit

**Scenario.** The process has a limit of fds ( `ulimit -n` , often 1024). Without its own limit, `accept()` starts returning `EMFILE` .
**The pitfall in the source docs.** The idea "we stop from `accept()` and leave the connections in the backlog" is only half correct: `poll()` is **level-triggered**, so the listener remains `POLLIN` as long as there are connections in the queue and `poll()` returns immediately, indefinitely ⇒ **busy-loop with 100% CPU**, exactly what was wanted to be avoided (Appendix A, #3). Also `MAX_CLIENTS = 1024` equal to `ulimit -n` does not reach: stdin/stdout/stderr, the listener and any log files also consume fds.

**Solution (three parts).**

1. `MAX_CLIENTS` strictly below the process limit (checks with `getrlimit(RLIMIT_NOFILE)` on startup and drops a reserve of ~16).
2. When the limit is reached, **remove `POLLIN` from the listener** ( `pauseListener()` ), and put it back when a client leaves ( `resumeListener()` ). Simple alternative: `accept()` + send `ERROR :Server full` + `close()` .
3. For `EMFILE` , the classic *"spare fd"* technique: keep a `open("/dev/null")` open; at `EMFILE` you close it, do `accept()` + `close()` (refuse clean, empty the queue), then reopen the reserve.  `ECONNABORTED` is ignored and continues.

### T7. Event loop: invalidating lists and interpreting `revents`

**Problem.** `push_back` can reallocate `_pollFds` ; deleting the current item moves pointers; a handler can delete the client whose events you are still processing.
**Solutions, in order of preference:**

- **You rebuild `pollfd` at each iteration** (as in `demo_sources` ): the iterated vector is its own copy, and new customers appear from the next iteration. The simplest; enough for an educational project.
- **Snapshot** of a persistent vector + deferred removals/additions.
- **General rule:** handlers do not delete; **mark** ( `markForRemoval` ). Physical deletion happens **only once, at the end of the iteration** ( `reapDisconnectedUsers` ). Thus no *fd reuse* occur in the same iteration either (a new client gets the fd of a closed one and any "old" events from the snapshot are mistakenly applied to it).

**Correct order of handling `revents` :**

1. `POLLNVAL` → remove (bug: fd already closed).
2. `POLLERR` → diagnosis and elimination.
3. `POLLIN` → **read first**, even if `POLLHUP` is set: a client that sends `QUIT` / `PRIVMSG` and closes immediately produces `POLLIN|POLLHUP` ; if you remove `POLLHUP` before reading, you lose the last data (Appendix A, #8). The EOF comes anyway as `recv()==0` .
4. `POLLHUP` without `POLLIN` → elimination.
5. If the user is not marked, `POLLOUT` → `flush()` .

**Requirement:** the listener is emptied by repeating `accept()` until `EAGAIN` (see `T5` ).

** `select()` is forbidden** (ex case `C1` ): `FD_SET` does not check the `FD_SETSIZE` limit (usually 1024); an fd ≥ 1024 writes outside the bitmap ⇒ stack corruption, which passes the tests with 500 clients and crashes under real load. We use `poll()` exclusively and document the ban in `Server.hpp` (plus a `grep -n "select(\|FD_SET" srcs/` in review).

### T8. Signals and shutdown

**Problem.** Handler in `another_demo` writes to `std::cout` and calls `stop()` : not async-signal-safe.

```cpp
#include <csignal>
volatile std::sig_atomic_t g_stop = 0;

extern "C" void handleSignal(int) { g_stop = 1; }   // singura operație permisă

void installSignalHandlers()
{
    struct sigaction sa;
    std::memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handleSignal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;                       // fără SA_RESTART: poll() iese cu EINTR
    sigaction(SIGINT,  &sa, 0);
    sigaction(SIGTERM, &sa, 0);

    struct sigaction ign;
    std::memset(&ign, 0, sizeof(ign));
    ign.sa_handler = SIG_IGN;
    sigemptyset(&ign.sa_mask);
    sigaction(SIGPIPE, &ign, 0);           // vezi T4
}
```

**Rules:**

- `g_stop` is checked **only once per iteration**, at the beginning of the loop; never in the middle of a function that must logically be atomic (eg `removeClient` with all its steps).
- `poll()` interrupted by signal return `-1/EINTR` : **not fatal error**.
- **Race:** if the signal arrives between checking `g_stop` and entering `poll(…, -1)` , the server hangs until the next event. Use **periodic timeout** (eg 1000ms) or self-pipe/ `ppoll` / `eventfd` .
- Shutdown in normal context: stop accepting → (optional) shutdown message → close clients → destroy registries → close listener. All objects must remain in a destructible and idempotent state.

---

## Lifecycle and memory

### L1. Ownership for sockets and fds

`User` gets a `ISocket*` but doesn't delete it ⇒ memory leak **and** of fd on every client. Conversely, if both `Server` and `User` close the same fd ⇒ double-close (may close another client's fd, already reallocated by the kernel).

```cpp
class Fd
{
public:
    explicit Fd(int fd) : _fd(fd) {}
    ~Fd() { if (_fd >= 0) ::close(_fd); }
    int  get() const   { return _fd; }
    bool valid() const { return _fd >= 0; }
    int  release()     { int r = _fd; _fd = -1; return r; }   // transfer explicit
private:
    int _fd;
    Fd(const Fd&);                // non-copyable: un singur proprietar
    Fd& operator=(const Fd&);
};
```

- If `bind()` / `listen()` fails after `socket()` , the destructor closes the fd automatically.
- `Socket` contains a `Fd` , not a copyable `int`.
- The decision ( `Server` → `User` → `Connection` → `Fd` ) is written in the header; do not mix models.
- In C++11, a move-only type / `unique_ptr` with deleter has the same role.

### L2. Deleting from a container while browsing

```cpp
// GREȘIT
for (std::map<int, User*>::iterator it = _users.begin(); it != _users.end(); ++it)
    if (shouldDisconnect(it->second))
        _users.erase(it);          // `it` e mort; ++it din for = UB
```

With `std::vector` , `erase` also **moves** the elements after, so the separately kept pointers end up pointing to another client. **Solution: collect, then delete.**

```cpp
void Server::reapDisconnectedUsers()
{
    std::vector<int> toRemove(_pendingRemoval.begin(), _pendingRemoval.end());
    _pendingRemoval.clear();              // _pendingRemoval: std::set<int> => idempotent
    for (std::vector<int>::const_iterator id = toRemove.begin();
         id != toRemove.end(); ++id)
        removeClient(*id);                // un singur punct de ștergere (L3)
}
```

The first phase does not modify the container; the second works on a separate list and
calls **only** cleanup function, so logic is not duplicated (classic source of leaks).

### L3. A `User` lives in three structures: a single "death" point

A `User*` appears in `_users` (fd → User), in `UserRegistry` (nick → User), and in the member list of each `Channel` . If `removeClient()` only deletes it from `_users` , the other two are left with dead pointers; the next `PRIVMSG` to the old nick or the next broadcast ⇒ use-after-free.

```cpp
void Server::removeClient(int fd)
{
    std::map<int, User*>::iterator it = _users.find(fd);
    if (it == _users.end())
        return;                                   // idempotent: apel dublu = nimic

    User* user = it->second;

    // 1) Din toate canalele. Copiem lista (removeMember o modifică pe parcurs).
    //    Aici se trimite și QUIT/PART către ceilalți membri: userul încă există.
    std::vector<Channel*> channels = user->channels();
    for (std::size_t i = 0; i < channels.size(); ++i)
        removeMember(*channels[i], *user);

    // 2) Din registry-ul de nickname-uri.
    _userRegistry.forget(*user);

    // 3) Din harta de fd-uri.
    _users.erase(it);

    // 4) Abia acum distrugem obiectul: Connection -> Fd -> ::close(fd).
    delete user;
}
```

**Order matters:** step 1 before `delete` , because `removeMember` more
read the status of the user (nickname for the message `PART` ). The opposite would be a
use-after-free right in the cleanup function.  `delete` does not appear anywhere outside
this function.

### L4.  `QUIT` followed by other commands in the same TCP packet

The most subtle case: the client sends in a single packet
 `QUIT :bye\r\nPRIVMSG #c :mai vorbesc?\r\n` . If the `QUIT` handler immediately deletes the `User`, the second iteration of the line loop uses a dead object.
It never appears in manual tests (you type the commands one at a time), but it is guaranteed to appear with a script.

**Solution:** disconnection does not happen "on the spot"; is **marked**, and the loop checks the mark **before each line**.

```cpp
void Server::processLines(User& user)
{
    Connection& conn = user.connection();
    std::string line;
    for (int budget = MAX_LINES_PER_TURN; budget > 0; --budget)
    {
        if (isMarkedForRemoval(conn.fd()) || isMarkedForClosing(conn.fd()))
            return;                       // ce a mai rămas în pachet se ignoră
        Connection::FrameStatus st = conn.nextLine(line);
        if (st == Connection::FRAME_NEED_MORE) return;
        if (st == Connection::FRAME_TOO_LONG)  throw ProtocolError("line too long");
        if (line.empty()) continue;                       // linii goale: ignorate (RFC)
        ParseResult pr = parseLine(line);                 // P3
        if (!pr.ok) { continue; }                         // sau 421, după caz
        _dispatcher.dispatch(pr, user);
    }
    // Budget epuizat: mai pot fi linii în buffer. Ține un flag `hasBacklog`
    // și procesează-le și în iterația următoare, chiar dacă poll() nu mai
    // raportează POLLIN (altfel rămân blocate).
}

void Server::handleQuit(User& user, const std::string& reason)
{
    user.connection().queue(makeQuitNotice(reason));
    markForClosing(user.connection().fd());   // doar marchează; nu șterge nimic acum
}
```

The `MAX_LINES_PER_TURN` budget prevents *starvation*: without it, a client who
send thousands of commands in a packet monopolizes the iteration.

### L5. Broadcast: use-after-free, error isolation and memory amplification

**(a) Use-after-free.** Direct iteration over `members` is risky if any step modifies the list. We iterate over a **copy** of the pointers. With strictly deferred deletion ( `L2` / `L4` ), no deletion occurs during the broadcast, so the check is reduced to "not checked"; the snapshot remains the second line of defense.

> Don't check the validity of a pointer by comparing it to the registry *by address*: after a `delete` , a new `User` can get the same address (ABA problem) and the check passes on the wrong object. Check by **fd/id** or by deletion mark (Appendix A, #7).

**(b) Error Isolation.** A failed `send()` to one member need not stop delivery to the others 49. Therefore the broadcast just puts the message in everyone's queue ( `queue()` ), and if one's queue is full, only that one is marked.

```cpp
void Channel::broadcast(const std::string& msg, const User* except)
{
    std::vector<User*> snapshot = _members;              // copie de pointeri
    for (std::size_t i = 0; i < snapshot.size(); ++i)
    {
        User* member = snapshot[i];
        if (member == except || _server.isMarked(member->connection().fd()))
            continue;
        if (!member->connection().queue(msg))            // T3: limită depășită
            _server.markForRemoval(member->connection().fd());
    }
}
```

**(c) Fan-out.** A channel with 500 members and a message of 400 B
means 200,000 B allocated from one order; limit per customer
( `MAX_OUTPUT_BYTES` ) is respected and the total memory keeps growing. Two solutions:

- **Simple (recommended for school project):** `MAX_CHANNEL_MEMBERS` + `MAX_CLIENTS` ; it's easy to explain and test.
- **Advanced:** shared message, with reference counting (in C++98 there is no `shared_ptr` ):

```cpp
class SharedMessage
{
public:
    explicit SharedMessage(const std::string& text)
        : _text(new std::string(text)), _refs(new long(1)) {}
    SharedMessage(const SharedMessage& o) : _text(o._text), _refs(o._refs) { ++*_refs; }
    ~SharedMessage() { if (--*_refs == 0) { delete _text; delete _refs; } }
    const std::string& text() const { return *_text; }
private:
    std::string* _text;
    long*        _refs;
    SharedMessage& operator=(const SharedMessage&);   // nedefinit
};
```

(The queue would store `SharedMessage` and an offset per recipient; it's not required from the start.)

### L6. Channels: `KICK` in mass and empty channels

** `KICK #c n1,n2,n3` .** If `removeMember()` deletes the channel when it runs out of members and the loop still has nicks to process, the next iteration uses a deleted `Channel`.
**Solutions** (either, or both): (1) **defer empty channel deletion** in the reap phase ( `pruneEmptyChannels()` ); (2) **re-search the channel by name** at each step:

```cpp
void Server::handleKick(const std::string& channelName,
                        const std::vector<std::string>& nicks)
{
    for (std::size_t i = 0; i < nicks.size(); ++i)
    {
        Channel* channel = findChannel(channelName);   // re-căutare, nu pointer păstrat
        if (!channel)
            break;                                     // "nu mai există" = caz normal
        User* target = findUser(nicks[i]);
        if (!target || !channel->hasMember(*target))
            continue;
        removeMember(*channel, *target);
    }
}
```

**Empty channels.** On logout, `demo_sources` removes the member but keeps it
channel ⇒ memory increases with temporary channels. Cleaning must be
in one place:

```cpp
void Server::removeMember(Channel& channel, User& user)
{
    channel.remove(user);
    user.leave(channel);          // invariantul bidirecțional, doar prin metode controlate
    if (channel.memberCount() == 0)
        _channelsToPrune.insert(normalizeIrc(channel.name()));   // ștergere fizică în reap
}
```

`User` / `Channel` must not expose public members ( `members` , `channels` ):
otherwise the invariant `user.channels ∋ channel ⇔ channel.members ∋ user` cannot be guaranteed.

---

## Protocol and input

### P1. Access to parameters without verification

```cpp
// GREȘIT: JOIN\r\n fără canal => params e gol, operator[] nu verifică => UB
std::string channel = params[0];
```

Solution: a helper used **everywhere** in handlers, never `params[i]` directly, and a proper IRC response instead of crashing ( `461 ERR_NEEDMOREPARAMS` ).

```cpp
inline bool paramAt(const std::vector<std::string>& params,
                    std::size_t index, std::string& out)
{
    if (index >= params.size()) return false;
    out = params[index];
    return true;
}

void handleJoin(const std::vector<std::string>& params, User& user)
{
    std::string channel;
    if (!paramAt(params, 0, channel))
    {
        user.connection().queue(makeNumericError(461, "JOIN :Not enough parameters"));
        return;
    }
    joinChannel(user, channel);
}
```

The customer finds out exactly what he did wrong, and a single careless customer can no longer
crash the server for everyone.

### P2. Uncaught STL exceptions: `terminate()` kills all clients

`std::string::at()` and `substr(pos)` with `pos > size()` cast `std::out_of_range` ; huge concatenations can throw `length_error` / `bad_alloc` . Uncaught, triggers `std::terminate()` : **the whole process dies**.
(Note: `substr(0, 500)` on a shorter string **doesn't** throw; only `pos > size()` does. The original example in the source was wrong, see Appendix A, #5.)

**Two layers, both required:**

```cpp
// Stratul 1: verifici înainte, în loc să te bazezi pe excepție.
inline std::string safeSubstr(const std::string& s, std::string::size_type pos,
                              std::string::size_type len = std::string::npos)
{
    if (pos > s.size()) return std::string();
    return s.substr(pos, len);
}

// Stratul 2: plasa finală în bucla principală.
void Server::run()
{
    installSignalHandlers();
    while (!g_stop)                        // singurul loc unde se citește g_stop (T8)
    {
        try
        {
            pollOnce();                    // accept + recv + dispatch + reap
        }
        catch (const std::exception& e)
        {
            logSystemError(e.what());      // fără să oprești procesul
        }
    }
    shutdown();
}
```

Layer 1 sends a friendly IRC message to the location of the problem; layer 2 guarantees that
a failed client never stops the whole server. **Warning:** layer 2
does not override client boundary handling ( `T2` ): if same state
bad remains, the same exception is repeated at each iteration.

### P3. Parser and AST with explicit result

`ASTParser::peekToken()` should only be called when `_index < _tokens.size()` . Blank line, missing command, or invalid parameters should not produce an AST that *looks* valid. Commands are normalized to uppercase ( `nick` = `NICK` ).

```cpp
struct ParseResult
{
    bool ok;
    std::string prefix, command, error;
    std::vector<std::string> params;
    ParseResult() : ok(false) {}
};                                         // fără pointeri raw: nimic de eliberat

ParseResult parseLine(const std::string& line)
{
    ParseResult r;
    if (line.empty() || line.find_first_of(std::string("\r\n\0", 3)) != std::string::npos)
    { r.error = "empty or invalid characters"; return r; }

    std::string::size_type i = 0, n = line.size(), start;
    if (line[0] == ':')
    {
        std::string::size_type sp = line.find(' ');
        if (sp == std::string::npos || sp == 1) { r.error = "bad prefix"; return r; }
        r.prefix.assign(line, 1, sp - 1);
        i = sp;
    }
    while (i < n && line[i] == ' ') ++i;
    start = i;
    while (i < n && line[i] != ' ') ++i;
    if (start == i) { r.error = "missing command"; return r; }
    r.command.assign(line, start, i - start);
    for (std::size_t k = 0; k < r.command.size(); ++k)
        if (r.command[k] >= 'a' && r.command[k] <= 'z')
            r.command[k] = static_cast<char>(r.command[k] - 'a' + 'A');

    for (;;)
    {
        while (i < n && line[i] == ' ') ++i;
        if (i >= n) break;
        if (line[i] == ':') { r.params.push_back(line.substr(i + 1)); break; }  // trailing
        start = i;
        while (i < n && line[i] != ' ') ++i;
        r.params.push_back(line.substr(start, i - start));
        if (r.params.size() > 15) { r.error = "too many parameters"; return r; } // RFC 2812
    }
    r.ok = true;
    return r;
}
```

The dispatcher decides the response: unknown command ⇒ `421 ERR_UNKNOWNCOMMAND` ;
empty lines ⇒ ignored.  `PING :tok` responds to `PONG :tok` (not `PONG: tok` ).

### P4. Nickname: validation, uniqueness, atomic renaming

`NICK` checks for: nil, maximum length (configurable: 9 in RFC 2812, 30 often in practice), first allowed character (letter or special character `[]\` _^{|} `), restul (litere, cifre, special, ` -`), uniqueness **case-insensitive**. The change is atomic at the registry level; on failure, the old nick remains intact. The original form is kept for display, the normalized one for lookup.

**Case-folding IRC** ( generic `tolower` doesn't make it; locale dependency doesn't either): according to RFC 1459/2812, `[ ] \ ~` is uppercase of `{ } | ^` .

```cpp
std::string normalizeIrc(const std::string& s)
{
    std::string r(s);
    for (std::size_t i = 0; i < r.size(); ++i)
    {
        char c = r[i];
        if (c >= 'A' && c <= 'Z') r[i] = static_cast<char>(c - 'A' + 'a');
        else if (c == '[')  r[i] = '{';
        else if (c == ']')  r[i] = '}';
        else if (c == '\\') r[i] = '|';
        else if (c == '~')  r[i] = '^';
    }
    return r;
}

bool UserRegistry::rename(User& user, const std::string& requested)
{
    if (!isValidNick(requested))
        return false;
    const std::string key = normalizeIrc(requested);

    std::map<std::string, User*>::iterator it = _byNick.find(key);
    if (it != _byNick.end() && it->second != &user)
        return false;                        // ocupat de ALTCINEVA; "Ana" -> "ana" e permis

    const std::string oldKey = user.nick().empty() ? "" : normalizeIrc(user.nick());
    _byNick[key] = &user;                    // 1) rezervă cheia nouă (poate arunca; nimic schimbat încă)
    if (!oldKey.empty() && oldKey != key)
        _byNick.erase(oldKey);               // 2) eliberează cheia veche (nu aruncă)
    user.setNick(requested);                 // 3) actualizează obiectul
    return true;
}
```

The `NICK` handler does not directly modify fields from `Socket` / `User` in places
different: calls a single service that validates, checks for uniqueness, and
update all indexes. Invariant: `_byNick[normalize(user.nick)] == &user` .
 `ERR_NICKNAMEINUSE` is `433` , `ERR_ERRONEUSNICKNAME` is `432` .

### P5. Numerical parameters from commands ( `MODE #c +l 100` )

`atoi("abc")` gives `0` in silence;  `atoi("999999999999999")` gives overflow (UB). An accepted `0` can mean "zero limit channel", i.e. a blocked channel or misinterpreted as "no limit". A single strict, reusable wrapper for port, channel limits, etc.:

```cpp
inline bool parseStrictLong(const std::string& text, long minValue, long maxValue, long& out)
{
    if (text.empty()) return false;
    char* end = 0;
    errno = 0;
    long value = std::strtol(text.c_str(), &end, 10);
    if (*end != '\0')        return false;   // "100abc"
    if (errno == ERANGE)     return false;   // overflow în strtol însuși
    if (value < minValue || value > maxValue) return false;   // regula domeniului
    out = value;
    return true;
}
// MODE +l:  parseStrictLong(arg, 1, 100000, limit)  -> altfel numeric de eroare
//           (696 ERR_INVALIDMODEPARAM e extensie modernă, nu apare în RFC 2812)
```

### P6. Starting arguments

A "defending" server must **fail early and clearly**, not start in an ambiguous state ( `atoi` gives `0` ⇒ `bind()` can pick a random port).

```cpp
int main(int argc, char** argv)
{
    if (argc != 3)
    { std::cerr << "Utilizare: " << argv[0] << " <port> <parola>\n"; return 1; }

    long port = 0;
    if (!parseStrictLong(argv[1], 1, 65535, port))
    { std::cerr << "Port invalid: " << argv[1] << "\n"; return 1; }
    // (porturile < 1024 cer privilegii; bind() va raporta clar eroarea)

    const std::string password(argv[2]);
    if (password.empty())
    { std::cerr << "Parola nu poate fi goala\n"; return 1; }

    try
    {
        Server server(static_cast<int>(port), password);   // ctor aruncă la eșec socket/bind/listen
        server.run();
    }
    catch (const std::exception& e)
    { std::cerr << "Eroare fatala la pornire: " << e.what() << "\n"; return 1; }
    return 0;
}
```

---

## 7. The integrated skeleton of the event loop

All of the above solutions put together (reference snippet, not final code):

```cpp
void Server::pollOnce()
{
    std::vector<struct pollfd> fds = buildPollSet();   // copia locală = snapshot natural
    int rc = ::poll(&fds[0], fds.size(), 1000);        // timeout: T8
    if (rc < 0)
    {
        if (errno == EINTR) return;                    // semnal: nu e eroare
        throw std::runtime_error("poll failed");
    }
    for (std::size_t i = 0; rc > 0 && i < fds.size(); ++i)
    {
        const struct pollfd& ev = fds[i];
        if (ev.revents == 0) continue;

        if (ev.fd == _listenFd) { acceptPendingConnections(); continue; }

        User* user = findUserByFd(ev.fd);
        if (!user || isMarked(ev.fd)) continue;        // deja în ieșire

        if (ev.revents & (POLLNVAL | POLLERR)) { markForRemoval(ev.fd); continue; }
        if (ev.revents & POLLIN)                readClient(*user);       // drenează și la HUP
        else if (ev.revents & POLLHUP)        { markForRemoval(ev.fd); continue; }
        if ((ev.revents & POLLOUT) && !isMarked(ev.fd))
            writeClient(*user);                        // flush(); IO_ERROR => markForRemoval
    }
    reapDisconnectedUsers();      // L2/L3: singura ștergere fizică, la final
    pruneEmptyChannels();         // L6
    resumeListenerIfRoom();       // T6
}

// buildPollSet(): listener (POLLIN, dacă nu e în pauză) + pentru fiecare User:
//   POLLIN (dacă nu e Closing), POLLOUT doar dacă connection().wantsWrite().
```

---

## 8. Implementation order

The order reconciles the two audits (foundation first; functionality second):

1. **Ownership** explicit: `Fd` , `User` , `Socket` ; copy disabled ( `L1` ).
2. **Persistent input buffer** + limits ( `T1` , `T2` ).
3. **Output queue** + backpressure ( `T3` ).
4. **POSIX** explicit errors: `EINTR` , `EAGAIN` , EOF, `POLL*` , `accept` ( `T5` , `T6` ).
5. **Event loop stable**: marking + reap, order `revents` ( `T7` , `L2` , `L4` ).
6. **Lifecycle explicit** and unique, idempotent `removeClient` ( `L3` , FSM from §5.4).
7. **Signals** ( `T8` ) and `SIGPIPE` ( `T4` ).
8. **Parser** with `ParseResult` , case-insensitive commands, standard numeric responses ( `P3` , `P1` ).
9. **Nickname registry** and atomic renaming ( `P4` ).
10. **Channel registry**, safe broadcast, empty channels cleaning ( `L5` , `L6` ).
11. **Full IRC numbers** ( `001–004` , `353/366` , `421` , `432` , `433` , `461` , …).
12. **Unit, integration and stress tests** (§9), run in the same PR as the change it protects.

---

## 9. Tests and Tools

### 9.1 Matrix of mandatory tests (before integration)

**Framing and input**
- [ ] `PI` + `NG :abc\r\n` produces a single `PING` ( `T1` ).
- [ ] `PING :a\r\nPING :b\r\n` produces two commands ( `T1` ).
- [ ] An incomplete line remains in the buffer, it is not dispatch-forgotten ( `T1` ).
- [ ] 512 B line **with** CRLF supported; 513 B rejected;  `\r` at the end of one `recv` and `\n` in the next works ( `T1` ).
- [ ] Infinite bytes without terminator: **only** that client is disconnected, the rest works normally ( `T2` ).
- [ ] Input with NUL in the middle does not silently truncate / is rejected controlled ( `T1` , `P3` ).

**Output and backpressure**
- [ ] `send()` partial keep rest in queue ( `T3` ).
- [ ] `EAGAIN` does not close the client ( `T3` , `T5` ).
- [ ] Output queue exceeded ⇒ disconnected controlled, others are not affected ( `T3` ).
- [ ] Broadcast in a channel with a dead/slow member does not stop delivery to others ( `L5` ).
- [ ] `SIGPIPE` does not terminate process ( `T4` ).

**Lifecycle**
- [ ] EOF removes the client from all indexes and closes the fd ( `L1` , `L3` ).
- [ ] `POLLHUP` with data pending: data is processed, then client removed ( `T7` ).
- [ ] `QUIT` + another command in the same package: the second one does not execute, no crash ( `L4` ).
- [ ] Two clients do `QUIT` in the same iteration `poll()` ( `L2` ).
- [ ] Two close events for the same fd: no double-delete ( `L3` ).
- [ ] Over `MAX_CLIENTS` : no busy-loop ( `top` shows normal CPU), clean refuse ( `T6` ).
- [ ] `ulimit -n` small (ex. 20) + many connections: server hangs ( `T6` ).
- [ ] `KICK` with list emptying channel: no access to deleted channel ( `L6` ).
- [ ] An empty channel is removed ( `L6` ).
- [ ] `PART` from last member while other code is broadcasting ( `L5` ).

**Protocol**
- [ ] `JOIN` , `PART` , `KICK` , `MODE` no parameters ⇒ `461` , no crash ( `P1` ).
- [ ] Lowercase command ( `nick` ) recognized; unknown command ⇒ `421` ( `P3` ).
- [ ] Empty line, prefix only, >15 parameters: rejected controlled ( `P3` ).
- [ ] Duplicate Nickname rejected **without** changing the existing state;  `Ana` → `ana` by the same authorized user ( `P4` ).
- [ ] Renaming simultaneously updates `User` and registry ( `P4` ).
- [ ] `MODE #c +l abc` , `+l -5` , `+l 99999999999999999999` explicitly rejected ( `P5` ).
- [ ] Port `abc` , `0` , `70000` , `123abc` , no password ⇒ clear message, no open socket ( `P6` ).

**Signals**
- [ ] `SIGINT` on inactive server (stuck in `poll` ) ⇒ shutdown in ≤ timeout ( `T8` ).
- [ ] `SIGINT` under load ⇒ orderly shutdown, without leaks ( `T8` ).

### 9.2 Practical Test Commands

```bash
# Fragmentare: "PI" .. pauză .. "NG :abc"
( printf 'PI'; sleep 1; printf 'NG :abc\r\n'; sleep 1 ) | nc 127.0.0.1 6667
# Două comenzi într-un singur pachet
printf 'PING :a\r\nPING :b\r\n' | nc 127.0.0.1 6667
# QUIT + comandă în același pachet (L4)
printf 'NICK a\r\nUSER a 0 * :A\r\nQUIT :bye\r\nPRIVMSG #c :x\r\n' | nc 127.0.0.1 6667
# Flux fără terminator (T2)
head -c 1000000 /dev/zero | tr '\0' 'A' | nc 127.0.0.1 6667
# Client care nu citește (T3): tine conexiunea deschisă fără să consume
( printf 'NICK slow\r\nUSER s 0 * :S\r\nJOIN #c\r\n'; sleep 600 ) | nc 127.0.0.1 6667
# Leak-uri de memorie ȘI de fd
valgrind --leak-check=full --track-fds=yes ./ircserv 6667 pass
# Ce fd-uri au rămas deschise
lsof -p $(pgrep ircserv)
# Ce syscall-uri se fac efectiv
strace -f -e trace=poll,accept,recv,send,close -p $(pgrep ircserv)
```

(`nc` options differ between variants: `-q` , `-N` , `-w` . Check `man nc` .)

### 9.3 Build and Analysis

- `-std=c++98 -Wall -Wextra -Werror` on **every** build (for `another_demo` , unused `server` parameters must be removed or marked `(void)server;` ).
- AddressSanitizer + UBSan (get exactly use-after-free from `L3` / `L4` / `L6` ), Valgrind Memcheck, `cppcheck` , `clang-tidy` .
- Testing with `GoogleTest` / `Catch2` only if dependencies are allowed; otherwise, a self-test `main` using a fake `ISocket`.

---

## 10. Teamwork: two people, one GitHub repository

Purpose: the two people not to simultaneously edit the same blocks from
 `Server.cpp` , `Server.hpp` or `Makefile` without coordination.

### 10.1 Division of Responsibilities

| | Person A: transport and lifecycle | Person B: IRC protocol and status |
|---|---|---|
| Files |  `Server.cpp/hpp` , `Socket.cpp/hpp` , `Socket.cpp` |  `User` , `Channel` , `Lexer` , `ASTParser` , `CommandDispatcher` |
| Subjects | framing, `poll` , `accept` , `recv` , `send` , shutdown, ownership fd ( `T*` , `L1` – `L4` ) | IRC validation, nicknames, channels, registry, numbers ( `P*` , `L5` , `L6` ) |

The division is not absolute. If both need to change `Server.cpp` , se
establish the **first contract** (§5.3): A exposes `queue()` and `nextLine()` , B
uses them without rewriting the event loop.

### 10.2 Branch rules

```bash
git fetch origin
git switch main
git pull --ff-only origin main
git switch -c feature/transport-framing      # un branch = un scop clar
```

- A branch is not shared between two people. Small, compilable commits:
   `fix: preserve fragmented IRC lines` , `fix: bound per-client output queue` ,
   `test: cover partial non-blocking writes` . No `misc` , `changes` , `final` .
- Common files ( `Makefile` , `main.cpp` , documentation): it is announced who modifies them; do not combine reformatting with functional modification; never `git checkout --` / `git reset --hard` over the other's work.

### 10.3 Order of integration

1. To publish the transport contract ( `Connection` , input buffer, output queue, error signaling) **and its tests**.
2. B writes the parser/dispatcher against the contract, without writing directly to the socket.
3. Separate pull requests; the transport is integrated first, then the protocol.
4. After each integration: `make re` .

If two changes are logically dependent, the second person rebases the PR
first or starts the branch from her commit (does not manually copy the code).
Each person syncs their branch with `main` **before** the PR;
long branches increase the probability of conflicts in `Server.cpp` .

### 10.4 What happens at `git push` and how the other takes over

```bash
git push -u origin alex          # publică `alex`, setează tracking; NU modifică main
```

```bash
git fetch origin                                   # descarcă, nu schimbă branch-ul curent
git branch -a ; git log --oneline origin/alex ; git diff origin/main..origin/alex
git switch -c alex --track origin/alex             # branch local din cel remote
git switch alex && git pull --ff-only              # dacă branch-ul local există deja
git status ; git branch --show-current             # verifică mereu pe ce branch ești
```

They both **don't** commit to the same branch. To continue working
to the other: `git switch -c feature/protocol origin/alex` (derivative branch) or
 `git cherry-pick <hash>` for a single commit; review through PR
( `alex → main` ).  `git fetch` only brings information;  `git pull` and integrate.

### 10.5 Post-merge synchronization and conflicts

```bash
git fetch origin && git switch main && git pull --ff-only origin main
git switch alex && git rebase origin/main          # rebase DOAR pe branch-ul propriu
# la conflict: rezolvi manual doar fișierele indicate, păstrând intenția ambelor părți
git add <fisiere-rezolvate> && git rebase --continue
git diff --check && make re
git push --force-with-lease origin alex            # nu --force: refuză dacă altcineva a publicat între timp
```

Branches guarantee **isolation** until merge (a `push` does not change
the branch of the other, `main` only changes by going approved), but **no
remove conflicts** if both modify the same lines. Risk reduction:
pre-split files and responsibilities, small commits, synchronization
frequent, no reformatting of common files, common contracts first.
When committing from examples, prefer `git add <fișiere>` instead of `git add .` .

### 10.6 Mandatory review and PR checklist

No PR integrates without the other person's verification:

```text
[ ] Nu există raw pointer owning nou fără justificare scrisă.
[ ] Nu există send() direct din dispatcher.
[ ] Nu se modifică un vector/map în timpul parcurgerii lui.
[ ] Nu există delete de User/Channel în afara punctelor unice (removeClient/prune).
[ ] Erorile POSIX (EINTR, EAGAIN, EOF, POLLHUP, accept) sunt tratate explicit.
[ ] Orice limită nouă are un test.
[ ] Nu apare select()/FD_SET.
[ ] make re (-Wall -Wextra -Werror -std=c++98) și git diff --check trec.
```

---

## 11. Catalog of algorithms and patterns

It's a **recognition index**, not a "to do now" list: when
encounter a new problem, look for the name that describes its shape.

### 11.1 From problem to technique

| Form of the problem | C++98 | Modern equivalent |
|---|---|---|
| Resource Ownership (fd) | RAII, non-copyable, Rule of Three, `auto_ptr` (deprecated) |  `unique_ptr` with deleter, move-only |
| "Check if object still exists" | Registry + stable identifier |  `weak_ptr` |
| Safe copy for classes with resources | Copy-and-swap | move semantics |
| Too many error paths when parsing |  `ParseResult` |  `std::expected` (C++23), `optional` / `variant` (C++17) |
| End of command in a stream | Delimiter-based framing, sliding window |  `string_view` / `span` for childless processing |
| Move/delete during iteration | Two-phase removal, snapshot, erase-remove | ranges (C++20) |
| Quick lookup nick/channel |  `std::map` (O(log n)) |  `unordered_map` , `flat_map` (C++23) |
| Output Queues | producer–consumer, `std::deque` | – |
| Limiting the rate of orders (flood) | Token bucket / leaky bucket | – |
| Cooperative shutdown |  `volatile sig_atomic_t` |  `std::atomic` , `jthread` + `stop_token` |
| I/O Multiplexing |  `poll()` (project), `epoll` / `kqueue` (scaling) | Reactor/Proactor, coroutines, `io_uring` |

### 11.2 C++98 idioms and patterns (name and context only)

- **Resources and Objects:** RAII (fd, files, mutexes) · Rule of Three · Non-copyable idiom · Pimpl (recompile isolation) · `std::auto_ptr` · Copy-and-swap · Named Constructor · Construct-on-first-use · Meyers' Singleton (with care).
- **OOP/polymorphism:** Non-Virtual Interface · Template Method · Strategy · Command (dispatcher) · State (FSM recording) · Observer (broadcast) · Visitor/double dispatch (traversal AST) · Adapter ( `ISocket` ) · Facade ( `Server` ) · Mediator · CRTP · Dependency Inversion/Injection · Null Object · Object Pool.
- **STL:** erase-remove · `find/find_if` · `sort` /introsort (ex. `NAMES` ) · `binary_search` / `lower_bound` · `transform` (mass normalization) · `for_each` · functors ( `unary_function` , `mem_fun` , `bind1st/2nd` ) · `std::map/set` (tree balanced) · `std::deque` ( `OutputQueue` ).
- **Network/system:** `select()` (forbidden: `FD_SETSIZE` ) · `poll()` · `epoll` / `kqueue` · non-blocking loop with retry on `EAGAIN/EWOULDBLOCK/EINTR` · partial read/write loop · edge-safe drain loop ( `accept` to `EAGAIN` ) · producer–consumer queue · watermark/backpressure · token/leaky bucket · exponential backoff (if server initiates connections) · backup fd for `EMFILE` · self-pipe · handler async-signal-safe.
- **Parsing/validation:** FSM explicit (enum + switch) · two-pointer/index scanning · `strtol` strict · fail-fast validation · idempotent cleanup · ownership graph (documented) · snapshot iteration · Single Responsibility.

### 11.3 Modern C++ (C++11 → C++23), directions of evolution

The project remains **C++98**; these facilities are not introduced as dependencies.

- **C++11/14:** move semantics, `unique_ptr` / `make_unique` , `shared_ptr` / `weak_ptr` , lambdas (generic in 14), `std::function` / `std::bind` , variadic templates, `auto` / `decltype` , range-for, `thread` / `mutex` / `condition_variable` , `std::atomic` , `nullptr` , `enum class` , `constexpr` , `static_assert` , `std::array` , `std::chrono` , `unordered_map` , delegating constructors, `override` / `final` , binary literals.
- **C++17:** `optional` , `variant` + `visit` , `string_view` , `any` , structured bindings, `if constexpr` , `filesystem` , fold expressions, CTAD, `scoped_lock` , `shared_mutex` , `pmr` , `invoke` .
- **C++20:** Concepts, Ranges, Coroutines, `span` , `jthread` / `stop_token` , `latch` / `barrier` , Modules, `operator<=>` , calendar/time-zone.
- **C++23:** `std::expected` , `print` / `format` , `mdspan` , `generator` , `stacktrace` , `flat_map` , `byteswap` , deducing `this` , monadic operations.
- **High-level design:** Reactor (≈ `poll()` loop of this project), Proactor ( `io_uring` , IOCP), policy-based design, type erasure.

---

## 12. Documentation sources

**Standards and Specifications**
- RFC 1459, RFC 2810/2811/2812 (IRC protocol, channels, messages, 512 limit, numeric), RFC 7194 (IRC over TLS). For modern numeric and extension behavior (eg `417` , `696` ), "Modern IRC Client Protocol" (modern.ircdocs.horse).
- POSIX.1-2008 (Open Group Base Specifications); ISO/IEC 14882:1998 (C++98) and later editions.
- *C++ Core Guidelines* (Stroustrup & Sutter), for ownership, RAII, exception safety.

**Manual pages (read exact fault codes, never from memory)**
 , `errno(3)` , `getaddrinfo(3)` , `getrlimit(2)` , `tcp(7)` , `ip(7)` .

**Books**
- *Effective C++*, *More Effective C++*, *Effective STL* (Meyers); *Exceptional C++*, *Exceptional C++ Style* (Sutter); *C++ Coding Standards* (Sutter & Alexandrescu); *Design Patterns* (GoF); *Modern C++ Design* (Alexandrescu); *The C++ Programming Language* (Stroustrup).
- **Network/Systems:** *Unix Network Programming vol 1* (Stevens), primary source for `socket/poll/select/SIGPIPE` /framing; *Advanced Programming in the UNIX Environment* (Stevens & Rago), for signals and fds; *TCP/IP Illustrated* (Stevens), for the actual guarantees of TCP; *Beej's Guide to Network Programming* (online, free; first place for a beginner).

**Online:** cppreference.com, isocpp.org (FAQ), *Guru of the Week* (Sutter).

**Tools:** GCC/Clang ( `-Wall -Wextra -Werror -std=c++98` ), GDB, Valgrind, AddressSanitizer, UBSan, `strace` , `lsof` , `nc` , `socat` , `telnet` , `clang-tidy` , `cppcheck` , Wireshark/ `tcpdump` , GoogleTest/Catch2.

**Source codes for architecture study (not for copying):** ngIRCd, InspIRCd, UnrealIRCd. It studies *how to separate the transport from the protocol*; licenses differ.

---

## Appendix A. Errors and ambiguities corrected from source documents

At the merge I checked the code snippets and corrected the following:

| # | Where | Problem | Correction in this guide |
|---|---|---|---|
| 1 |  `LineFramer::popLine` (analysis `skettch` ) |  `bool` returns `false` for both "no line yet" and "line too long"; the caller cannot distinguish them |  Three-state `FrameStatus` ( `T1` ) |
| 2 |  `readClient` (limit-case audit, §4.3) |  `popLine()` (which can throw `ProtocolError` ) was out of block `try` ; the exception was reaching `run()` , the client remained, the same line reproduced the exception at each iteration | all line processing in `try` , with `markForClosing` ( `T2` ) |
| 3|  `MAX_CLIENTS` + `break` ( `C7` ) | The listener remains `POLLIN` (level-triggered) ⇒ busy-loop;  `MAX_CLIENTS=1024` = `ulimit -n` , so `EMFILE` comes first | pause on listener, limit below `RLIMIT_NOFILE` , backup fd ( `T6` ) |
| 4|  `assertNoSelectAssumptions` ( `C1` ) | Function with no effect, although it claims a guard | eliminated; the documented rule remains + `grep` in review ( `T7` ) |
| 5| Example `substr(0, 500)` ( `C5` ) |  `substr(0, n)` does not throw; only `pos > size()` throw; the comment was confusing | corrected example and explanation ( `P2` ) |
| 6|  `SharedMessage` ( `C9` ) | Counter started from 0 with test `< 0` : working but unusual | counter started from 1, `== 0` ( `L5` ) |
| 7|  `isStillMember(User*)` ( `C3` / `C8` ) | Address pointer validation suffers from ABA after `delete` | check by fd/mark + deferred delete ( `L5` ) |
| 8| Order `POLLHUP` (audit + consultation) | Removing on `POLLHUP` before `POLLIN` loses last data ( `QUIT` , `PRIVMSG` ) | read first then delete ( `T7` ) |
| 9|  `flush()` with `throw SocketError` vs `queue()` with `throw SlowClient` | Hot-path exceptions, treated differently in three places | status codes + `markFor…` to caller ( `T3` ) |
| 10 |  `UserRegistry::rename` | Rejects only capitalization change of own nick ( `Ana` → `ana` ); delete the old key before the new | check `owner != &user` , insert-then-erase ( `P4` ) |
| 11 | Case-folding | The list in the analysis omitted the pair `~` ↔ `^` | complete RFC 1459 table ( `P4` ) |
| 12 | Numeric `417` , `696` | Presented as "standard", but do not appear in RFC 2812 | marked as modern extensions |
| 13 |  `MSG_NOSIGNAL` | Not portable (macOS/BSD) |  `#ifndef` + `SIGPIPE` global ignored ( `T4` ) |
| 14 |  `recv(fd, buf, room, 0)` | If `room == 0` return `0` = false EOF | explicit guard ( `T2` ) |
| 15 | Line terminator | The documents impose `\r\n` strictly;  `nc` send `\n` | decision to be documented ( `T1` ) |
| 16 |  `PONG: token` ( `another_demo` ) | Non-standard format |  `PONG :token` ( `P3` ) |
| 17 | Links | References to local absolute paths ( `/home/alexkatz/...` ) | eliminated; files are named relative |

## Appendix B. Traceability

The two colleagues each produced a set of overlay files. How they were merged:

| Original document | Content | Where to find |
|---|---|---|
|  `AUDIT_ROBUSTETE_SI_AJUSTARI.md` | comparative verdict, tweaks 1–10 (RAII, framing, write queue, POSIX, event loop, nickname, channels, signals, parser), Git workflow | §2, §5, `T1` – `T8` , `L1` , `L6` , `P3` , `P4` , §10 |
|  `ANALIZA_ROBUSTETE_SI_IMPLEMENTARE.md` (audit `skettch` ) | mental model, same problems, order of implementation, tests | §3, §6, §8, §9 (most detailed variant kept, redundant one removed) |
|  `AUDIT_CAZURI_LIMITA_SI_IMPLEMENTARE.md` | cases `C1` – `C15` , consolidated implementation, additional tests |  `T2` , `T6` – `T8` , `L2` – `L6` , `P1` , `P2` , `P5` , `P6` , §6.1, §9 |
|  `CONSULTANTA_DESIGN_SI_ALGORITMI.md` | pattern table → problem, sources, catalog C++98 / modern | §5.6, §11, §12 |
|  `CONSULTARE_DESIGN_ALGORITMI_CPP.md` | class design, ownership graph, lifecycle, algorithms, traps | §5, `T7` , `T8` , §11 |
|  `COMPARATIE_DEMO_IRC.md` | `demo_sources` vs `another_demo` comparison, final recommendation | §2 |

ID correspondence: `C1` → `T7` , `C2` → `L2` , `C3` → `L5` , `C4` → `P1` , `C5` → `P2` , `C6` → `T2` , `C7` → `T6` , `C8` → `L5` , `C9` → `L5` , `C10` → `L3` , `C11` → `L4` , `C12` → `P5` , `C13` → `L6` , `C14` → `P6` , `C15` → `T8` .
