# ft_irc — The Study Guide
### From "what is a socket?" to "how a senior writes a server that never crashes"

> Built from our whole conversation: your code → the refactored code, every definition, example and mnemonic,
> your questions + my answers, your own insights, the rules of good code, and the order in which the ideas depend on each other.
>
> **Honesty note:** code blocks are reconstructed from our conversation. Names (`_members`, `getSocket()`, …) follow *your* code
> as far as I saw it; adapt where yours differs.

---

## How to read this file (the map)

The file is a staircase. Each level only uses ideas from the levels below it.

| Level | Who it's for | What you learn | Parts |
|---|---|---|---|
| 🟢 **L1** | 1st year | C++98 mechanics: `const`, `static`, references, iterators, containers, exceptions | 1 |
| 🟡 **L2** | 2nd year | Sockets, `poll`, buffers, partial data, non-blocking I/O | 2 |
| 🟠 **L3** | 3rd year | The IRC protocol: messages, registration, numerics, commands | 3 |
| 🔴 **L4** | 4th year | Architecture: pipeline, Dispatcher, ICommand, error philosophy, lifecycle | 4–7 |
| ⚫ **L5** | last year | Robustness, trade-offs, advanced topics (CTCP/DCC, keep-alive, complexity) | 8 |
| 📚 | everyone | Q&A log, your insights, rules of great code, debugging playbook, HexChat cheat sheet, exercises | 9–13 |
| 🧭 | everyone | **Pre-flight questions** before a function, class, design, dispatcher, parameters, inheritance | 14 |

**Mnemonic for the whole project (the pipeline):**
> **R**eally **F**ast **P**arrots **D**eliver **E**ggs, **Q**uietly **P**eeling **S**hells
> **R**ecv → **F**rame → **P**arse → **D**ispatch → **E**xecute → **Q**ueue → **P**oll(POLLOUT) → **S**end

---
---

# 🟢 PART 1 — C++98 foundations (the tools)

## 1.1 `const` member functions

A `const` method promises: "I won't modify `*this`."
Consequence: **inside a const method you may only call other `const` or `static` members.**

```cpp
class ICommand {
public:
    virtual ~ICommand() {}
    virtual bool   needsRegistration() const = 0;
    virtual size_t minParams()         const = 0;
    virtual void   execute(Server&, Client&, const Message&) const = 0;
};
```

### The bug you hit
```
error 245: a nonstatic member reference must be relative to a specific object
error: the object has type qualifiers that are not compatible with the member function
```
You called a **non-const / non-static helper** (`splitParams`, `validName`, `isLegit`…) from a `const execute`.

**Fix:** helpers that don't touch object state become `static`:

```cpp
// JoinCommand.hpp
private:
    static void splitParams(const std::string& s, std::vector<std::string>& out);
    static bool validName(const std::string& n);
```
```cpp
// JoinCommand.cpp  → NO `static` keyword here, only in the header
void JoinCommand::splitParams(const std::string& s, std::vector<std::string>& out) { /*...*/ }
```

**Mnemonic:** *"A const method sees only const and static."* Helpers with no state → `static`.

### Abstract class error
```cpp
new JoinCommand();   // error: cannot instantiate abstract class
```
Cause: the pure virtual in `ICommand` is `const`, but the override in `JoinCommand` **forgot `const`** → it's a *different* function, so the pure virtual stays unimplemented.
**Rule:** the override signature must match **exactly**, including `const`.

## 1.2 `static` — only in the header
`static` goes on the declaration inside the class. Repeating it in the `.cpp` definition is an error.
Calling a static: `JoinCommand::validName(x)` (class name) — but a *non-static* member is always called **on an object**, never via the class name (that was your error 245).

## 1.3 Types you met
| Thing | Truth |
|---|---|
| `std::string::size_type` | same as `size_t` in practice |
| `find_first_of(...)` | returns a **position**; compare with `std::string::npos` |
| iterators | you need `.begin()` / `.end()`; `*it` is the element, `it->` is the member |
| `ssize_t` | signed — `recv`/`send` return it; `-1` means error. Never store in `size_t`. |

## 1.4 `++it` vs `it++`
In the 3rd slot of a `for`, the result is thrown away, so **both behave identically** for the loop.
They differ in *expression value*: `it++` returns the old value (needs a copy), `++it` returns the new one.
Habit: use `++it` (never slower).

## 1.5 Containers & `erase` (the #1 source of crashes)

```cpp
std::vector<Channel>::iterator it = v.begin();
while (it != v.end())
{
    if (shouldDelete(*it))
        it = v.erase(it);   // erase RETURNS the next valid iterator
    else
        ++it;
}
```
**Rule:** *"Erase returns the next — if you erased, don't `++`."*
Why: after `erase`, `it` and everything after it are invalidated; `++` after erase skips an element (or runs `++end()` → undefined behaviour).
Also: never cache `v.end()` across an `erase` — call `v.end()` every time.

For `std::map` (C++98): `m.erase(it++);` (post-increment gives you the next *before* erase kills it).

## 1.6 Exceptions
```cpp
throw IRCError(403, name);       // throw by value
catch (const IRCError& e) { }    // catch by const reference
```
| Syntax | Meaning |
|---|---|
| `throw;` | re-throw **the same** exception object (keeps its real type) |
| `throw e;` | throws a **copy** typed as the static type of `e` → *slicing* |
| `catch (...)` | catches **anything** (can't name it) |
| `catch (const std::exception& e)` | catches only types derived from `std::exception` |

`IRCError : public std::exception` with `virtual ~IRCError() throw() {}` (C++98 spelling of "doesn't throw").

**What `try/catch` can NOT save you from:** segfaults, undefined behaviour, `SIGPIPE`, infinite loops. Those are *bugs*, not exceptions.

## 1.7 Constructors that throw
A constructor that throws **never runs its destructor** (the object was never fully built). So if you `new` things in it, you must clean up yourself:

```cpp
Dispatcher::Dispatcher()
{
    try {
        _commands["JOIN"] = new JoinCommand();
        _commands["PRIVMSG"] = new PrivmsgCommand();
        /* ... */
    }
    catch (...) {        // "anything went wrong" (e.g. bad_alloc)
        clear();         // free what was already allocated
        throw;           // re-throw the SAME exception to the caller
    }
}
```
You asked *"what does `catch (...) { clear(); throw; }` mean?"* → exactly the above: **clean up, then pass the error on.**
And *"is it the same as `catch (std::exception& e)`?"* → No: `(...)` also catches non-`std::exception` types; and with `throw;` the type is preserved.

## 1.8 Ownership: "who deletes this?"
- `Channel` stores **non-owning** `Client*` (the `Server` owns clients in `std::map<int, Client>`).
- Non-owning pointers **dangle** the moment the owner erases the object. → Whenever a client is removed, you must remove it from every place that points to it (see Part 7).
- `Dispatcher` **owns** the command objects: `new` in ctor, `delete` in `clear()`, copy ctor & `operator=` **private** (Rule of Three: if you own raw resources, forbid copying).

```cpp
void Dispatcher::clear()
{
    for (std::map<std::string, ICommand*>::iterator it = _commands.begin(); it != _commands.end(); ++it)
        delete it->second;
    _commands.clear();
}
```
*(Your bug here: you wrote `==` where you meant `=` → the statement did nothing. Mnemonic: **"`==` asks, `=` does."**)*

## 1.9 Header hygiene
- Include guards (`#ifndef X_HPP / #define X_HPP / #endif`).
- `#include <string>` in any header that mentions `std::string`. Otherwise: `<error type>`.
- Declaration in `.hpp` and definition in `.cpp` must match **exactly** (name, params, `const`). Otherwise: *"class has no member sendReplies"* / *"identifier is undefined"*.

### 🟢 L1 mini-exercises
1. Write a `const` method that calls a `static` helper. Then try calling a non-static from it and read the error.
2. Erase all even numbers from a `std::vector<int>` with the `while/erase/else ++` pattern.
3. Make `throw;` vs `throw e;` print different dynamic types (hint: derive `B : A`, catch `A&`).

---
---

# 🟡 PART 2 — Sockets & `poll()` (the plumbing)

## 2.1 The subject's rules (what makes ft_irc *ft_irc*)
- One `poll()` for **all** I/O (clients + listening socket).
- All fds non-blocking: `fcntl(fd, F_SETFL, O_NONBLOCK)`.
- `send()` **only** when `poll` says `POLLOUT`; `recv()` only on `POLLIN`.
- **No `errno` decisions after `recv`/`send`** (so: `<= 0` ⇒ treat as disconnect).
- The server **must never crash**. Partial commands must be buffered per client.
- Allowed `signal` (use `SIGPIPE → SIG_IGN`).

## 2.2 The loop

```cpp
while (g_serverRunning)
{
    rebuildPollFds();                                   // derive pollfd[] from _clients + listen fd
    if (poll(&_pollSockets[0], _pollSockets.size(), -1) < 0)
    {
        if (errno == EINTR) continue;                   // a signal interrupted poll → recheck the flag
        throw std::runtime_error("poll failed.");
    }
    handlePollEvents();
}
```
- `rebuildPollFds()` each turn is a *good* design: the poll array is always derived from the truth (`_clients`), so a stale fd can't linger.
- `POLLOUT` is requested **only if** `client.hasOutput()`. (Asking for `POLLOUT` always = 100 % CPU busy loop.)

### Signals
```cpp
volatile sig_atomic_t g_serverRunning = 1;
static void handleSignal(int) { g_serverRunning = 0; }   // ONLY set a flag in a handler
std::signal(SIGINT,  handleSignal);
std::signal(SIGTERM, handleSignal);
std::signal(SIGPIPE, SIG_IGN);                           // writing to a closed socket must not kill us
```
In `Server.cpp`: `extern volatile sig_atomic_t g_serverRunning;`

## 2.3 What each `poll` event means

| `revents` | Meaning | Action |
|---|---|---|
| `POLLIN` on listen fd | someone is connecting | `accept`, make non-blocking, add `Client` |
| `POLLIN` on client fd | data (or EOF) to read | `recv` → `n <= 0` ⇒ disconnect |
| `POLLOUT` | we can write without blocking | `sendData` |
| `POLLHUP` | peer hung up | disconnect |
| `POLLERR` / `POLLNVAL` | broken / invalid fd | disconnect |

**Mnemonic:** *"IN reads, OUT writes, HUP/ERR/NVAL bury."*

> On Linux a peer closing usually arrives as **`POLLIN | POLLHUP` together** → the real detection is `recv() == 0`.
> That is why `handleClientData` must handle `bytesRead == 0`.

## 2.4 Your final event handler, and what I changed

```cpp
void Server::handlePollEvents()
{
    for (size_t i = 0; i < _pollSockets.size(); ++i)
    {
        const int   fd = _pollSockets[i].fd;
        const short ev = _pollSockets[i].revents;

        if (ev == 0) continue;
        if (fd != _serverFd && getClientBySocket(fd) == NULL) continue;   // already removed this turn

        if (ev & (POLLERR | POLLNVAL))
        {
            if (fd != _serverFd) disconnectClient(fd, "socket error");
            continue;
        }
        if (ev & POLLOUT)
        {
            sendData(fd);
            // sendData may have REMOVED the client (send error / close-after-flush)
            if (fd != _serverFd && getClientBySocket(fd) == NULL) continue;   // ← added by review
        }
        if (ev & POLLIN)
        {
            if (fd == _serverFd) registerClient();
            else                 handleClientData(fd);
            continue;
        }
        if ((ev & POLLHUP) && fd != _serverFd)
            disconnectClient(fd, "connection closed by peer.");
    }
}
```
**Lesson:** after *any* call that can remove a client, **re-check it still exists** before touching it again.
(Subtle extra: `accept` in the same turn can reuse a just-closed fd number, so a stale `revents` may hit a *new* client. The `getClientBySocket` guard + `<= 0` ⇒ disconnect policy keeps this harmless.)

## 2.5 Receiving: buffers and frames

TCP is a **stream**, not messages. One `recv` may contain half a command, or three commands.

```cpp
void Server::handleClientData(int fd)
{
    char buf[1024];
    Client* c = getClientBySocket(fd);
    if (!c) return;

    ssize_t n = recv(fd, buf, sizeof(buf), 0);
    if (n <= 0) { disconnectClient(fd, "peer closed / recv error"); return; }

    c->appendBuffer(std::string(buf, n));      // APPEND (never assign!)
    if (!extractValidData(c)) return;          // returns false if the client was removed
    if (exceedsPendingLimit(c->getBuffer().size(), 512)) disconnectClient(fd, "spam");
}
```

**Line framing (the correct shape):**
```cpp
std::string& in = cl->getInputBuffer();        // REFERENCE (a copy would erase nothing real)
size_t pos;
while ((pos = in.find('\n')) != std::string::npos)
{
    std::string line = in.substr(0, pos);
    in.erase(0, pos + 1);                      // erase only what you consumed
    if (!line.empty() && line[line.size() - 1] == '\r') line.erase(line.size() - 1);
    // parse(line) + dispatch
}
```
Common bugs (we listed them): overwrite instead of append; searching only for `\r\n` (nc sends `\n`); buffer declared local to the function; `getInputBuffer()` returning by value; `if` instead of `while` (only first line processed).

**Mnemonic:** *"Append, find `\n`, cut, repeat — with a reference."*

## 2.6 Sending: queue first, flush on POLLOUT

```
command → sv.sendTo(client, text)   // ONLY appends to client's out-buffer
next poll turn → hasOutput() ⇒ request POLLOUT
POLLOUT → sendData(): send(), erase the bytes actually sent
```
**Never call `send()` outside POLLOUT.** *(Your question "on a private message should I check send or recv?" → the command only **queues**; the real `send` happens on `POLLOUT`; `recv` is for what the *sender* typed.)*

```cpp
void Server::sendData(int fd)
{
    Client* cl = getClientBySocket(fd);
    if (!cl || !cl->hasOutput()) return;
    ssize_t bytes = send(fd, cl->getOutput().c_str(), cl->getOutput().size(), 0);
    if (bytes < 0) { removeClient(fd); return; }           // no errno inspection
    cl->eraseOutput(bytes);                                 // partial sends are normal
    if (cl->getMarkForClose() && !cl->hasOutput()) removeClient(fd);   // close-after-flush
}
```

## 2.7 🧪 Lab: testing partial frames with `nc` (and the Ctrl+D trap)

Your confusion: *"I press Ctrl+D several times and nothing works coherently."* Here is the truth:

| Keystroke | What the terminal does |
|---|---|
| `text` + **Ctrl+D** (non-empty line) | **flushes** `text` to nc **without `\n`** → server buffers it, waits (correct!) |
| **Ctrl+D** on an *empty* line | **EOF** → nc half-closes → server gets `recv()==0` → **disconnects you** |
| 3rd Ctrl+D | nothing, the connection is already dead |

> **One D flushes, two D's kill.**

Subject test:
```
com   [Ctrl+D]  (once)
man   [Ctrl+D]  (once)
d     [Enter]   → now the server executes "command"
```
Handy: `printf 'PASS pa' | nc 127.0.0.1 8888` (incomplete → no reply expected).

### 🟡 L2 mini-exercises
1. Write a tiny echo server with one `poll` and non-blocking sockets.
2. Make it echo only *complete* lines, buffering partial ones per client.
3. Add a 512-byte max line; disconnect offenders.

---
---

# 🟠 PART 3 — The IRC protocol (the language)

## 3.1 Message shape
```
[:prefix] COMMAND param1 param2 ... [:trailing text with spaces]\r\n
```
- Max 512 bytes **including** `\r\n`.
- The **trailing** parameter starts with `:` — the parser **strips the `:`** and keeps the rest (spaces included).
- Prefix of a user: `nick!username@host` — host comes from the **socket IP** (e.g. `127.0.0.1`), not from the USER command.
- Commands are case-insensitive → the parser **uppercases** `msg.command` (your bug: `pass` gave `421`).

Examples:
```
PRIVMSG #newch :hello all           params = ["#newch", "hello all"]
:alex!alex@127.0.0.1 JOIN #newch    (server → clients)
```

## 3.2 Registration (the order **you** chose, strict)

> **PNU** — *"Please Nobody Uninvited"*: **P**ASS → **N**ICK → **U**SER

| Step | Rule | Errors |
|---|---|---|
| PASS | first; wrong password ⇒ `464` and **close after flush** | 461 (empty), 462 (already registered), 464 |
| NICK | requires PASS done, else `451` | 431 empty, 432 illegal, 433 in use |
| USER | requires NICK done, else `451`; **completes registration** → sends `001` | 461, 462 |

Your insight (correct for your design): *"NICK needs the password first"* and *"I don't need `_hasUser`/`tryRegister`, because it's impossible to reach `setAuthState` without pass and nick."*
(I had called that a "security hole"; with a strict order it isn't — withdrawn.)

**USER format:** `USER <username> <mode> <unused> :<realname>` — always 4 params.
- `mode` is a bitmask (4 = w, 8 = i); you may **ignore** it.
- `username` and `realname` need **not** be unique. Only the **nick** is unique (433).
- Forbidden-char checks on username/realname don't matter much.
- `USER ana ana ana :Ana P` → username `ana`, mode `ana` (ignored), unused `ana`, realname `Ana P`.

Welcome line after registration:
```
:ircserv 001 alex :Welcome to the Internet Relay Network alex!alex@127.0.0.1
```

## 3.3 Commands at a glance

| Command | Needs registration | Min params | Notes |
|---|---|---|---|
| PASS | ✗ | 1 | no reply on success |
| NICK | ✗ | 0–1 | 431 handled inside |
| USER | ✗ | 4 | |
| PING | ✗ | 1 | → `PONG` |
| PONG | ✗ | 0 | accept & ignore |
| QUIT | ✗ | 0 | |
| JOIN | ✓ | 1 | comma lists: `JOIN #a,#b key1,key2` |
| PART | ✓ | 1 | |
| PRIVMSG / NOTICE | ✓ | 0 (check inside: 411/412) | comma-list targets, nick or `#chan` |
| TOPIC | ✓ | 1 | |
| KICK | ✓ | 2 | |
| INVITE | ✓ | 2 | exactly `<nick> <channel>`; extras ignored |
| MODE | ✓ | 1 | channel modes **i t k o l** |

> **Lists use commas, not spaces.** `JOIN #ch1 #ch2` → `params[0]="#ch1"`, `params[1]="#ch2"` (as *keys*!). The correct form: `JOIN #ch1,#ch2`.
> (You first thought `params[0]` would only get `#ch1` — true for the space form, and that exact observation led us to `splitParams`.)

## 3.4 Numeric replies (cheat sheet)

| Code | Name | Mnemonic |
|---|---|---|
| 001 | WELCOME | first hello |
| 331 / 332 | NOTOPIC / TOPIC | |
| 341 | INVITING | |
| 353 / 366 | NAMREPLY / ENDOFNAMES | names list, then end |
| 401 | NOSUCHNICK | no such **n**ick |
| 403 | NOSUCHCHANNEL | no such **c**hannel |
| 404 | CANNOTSENDTOCHAN | can't send |
| 411 / 412 | NORECIPIENT / NOTEXTTOSEND | **1**1 no one, **1**2 no text |
| 421 | UNKNOWNCOMMAND | |
| 431 / 432 / 433 | NONICKNAMEGIVEN / ERRONEUSNICKNAME / NICKNAMEINUSE | |
| 441 / 442 / 443 | USERNOTINCHANNEL / NOTONCHANNEL / USERONCHANNEL | |
| 451 | NOTREGISTERED | |
| 461 / 462 | NEEDMOREPARAMS / ALREADYREGISTRED | |
| 464 | PASSWDMISMATCH | |
| 471 | CHANNELISFULL (+l) | |
| 473 | INVITEONLYCHAN (+i) | |
| 475 | BADCHANNELKEY (+k) | |
| 482 | CHANOPRIVSNEEDED | |

**Mnemonic for the channel-join trio:** **L**imit 471, **I**nvite 473, **K**ey 475 → "**4-7-1, 4-7-3, 4-7-5 = L-I-K**" (the order you check them).

## 3.5 Channel modes (the five required)
| Mode | Meaning | Data |
|---|---|---|
| `i` | invite-only | — |
| `t` | only operators set the topic | — |
| `k` | key (password) | empty `_key` = no key |
| `o` | give/take operator | nick |
| `l` | user limit | `0` = no limit |

New channels start with **no** modes. The creator is an operator (`@` in `353`).

## 3.6 NAMES line
```
:ircserv 353 alex = #newch :@alexkatz bia
```
`@` = operator. Separator is **one space between** names.
(We briefly suspected a missing space; it was actually two distinct nicks: `alexkatz` from HexChat and `bia`.)

---
---

# 🔴 PART 4 — Architecture (the blueprint)

## 4.1 Who does what

```
 client ──► Server ──► IRCParser ──► Dispatcher ──► ICommand::execute ──► Server::sendTo ──► out-buffer ──► (POLLOUT) ──► send()
            (poll,      (1 line →      (map name→     (one class per        (queue only)
            recv,        Message)       command;       command, stateless)
            frame)                      checks 421/451/461)
```

| Class | Owns | Responsibility |
|---|---|---|
| `Server` | `_clients`, `_channels`, `Dispatcher` | sockets, poll, framing, lifecycle, `sendTo`, `broadcast`, `removeClient` |
| `Client` | in/out buffers, nick/user, flags | per-connection state; **no** protocol logic |
| `Channel` | members (non-owning `Client*`), invited, key, limit, modes | membership & mode data |
| `IRCParser` | nothing (local object per line) | text → `Message{command, params[]}` |
| `Dispatcher` | command objects | **generic validation** + routing |
| `ICommand` + subclasses | nothing (stateless) | **one command's** rules |
| `IRCError` | code, arg | formatted error reply |

## 4.2 The interface

```cpp
struct Param   { std::string value; bool isTrailling; };
struct Message { std::string command; std::vector<Param> params; };

class ICommand {
public:
    virtual ~ICommand() {}
    virtual bool   needsRegistration() const = 0;
    virtual size_t minParams()         const = 0;
    virtual void   execute(Server&, Client&, const Message&) const = 0;
};
```
Your question: *"why ICommand?"* → because the Dispatcher wants **one** type to store in a `std::map<std::string, ICommand*>` and call polymorphically. Without it, you'd need a giant `if/else` chain.

*"What commands need registration?"* → see table in 3.3. *"The Dispatcher class only has ctor, dtor and `dispatch`, right?"* → Yes (plus private `clear()`, private copy).

## 4.3 `IRCError`

```cpp
class IRCError : public std::exception {
    int         _code;
    std::string _arg;
public:
    IRCError(int code, const std::string& arg = "") : _code(code), _arg(arg) {}
    virtual ~IRCError() throw() {}
    std::string format(const std::string& nick) const;   // ":ircserv <code> <nick|*> <arg> :text\r\n"
};
```
`nick` is `*` if the client has none yet.

## 4.4 The Dispatcher — the single place for generic errors

```cpp
void Dispatcher::dispatch(Server& sv, Client& cl, const Message& msg)
{
    try
    {
        std::map<std::string, ICommand*>::const_iterator it = _commands.find(msg.command);
        if (it == _commands.end())                                  throw IRCError(421, msg.command);
        const ICommand* cmd = it->second;
        if (cmd->needsRegistration() && !cl.getAuthState())         throw IRCError(451);
        if (cmd->minParams() > msg.params.size())                   throw IRCError(461, msg.command);
        cmd->execute(sv, cl, msg);
    }
    catch (const IRCError& e)
    {
        sv.sendTo(cl, e.format(cl.getNickname()));
    }
}
```
**Mnemonic for the check order — U·R·P·E:** *Unknown (421) → Registered? (451) → Params (461) → Execute.*
("**U**nicorns **R**arely **P**ark **E**lephants.")

---
---

# 🔴 PART 5 — Error philosophy (your most important question, answered in full)

You asked it three different ways:
1. *"Why should I try/catch inside commands if the Dispatcher already does?"*
2. *"If I need registration, is every error thrown inside `execute` with its own try/catch?"*
3. *"Where does the 451 come from?"*

### The rule

> **Commands THROW. The Dispatcher CATCHES. Loops CATCH-and-CONTINUE.**

| Where | What it does |
|---|---|
| `Dispatcher::dispatch` | throws 421 / 451 / 461 itself, and has **one** `try/catch (IRCError)` that sends the reply |
| `execute` of a simple command (INVITE, TOPIC, NICK…) | only `throw IRCError(...)` for its **own** errors — **no try/catch** |
| `execute` of a **list** command (JOIN, PRIVMSG; PART/KICK if lists) | `throw` inside a helper + **`try/catch` inside the loop** |

**Why the loop is special:** `JOIN #a,#b,#c` — if `#a` fails (403), without an inner catch the exception leaves the loop and `#b`, `#c` are **never tried**. One bad target must not cancel the others.

**Where exceptions that are *not* `IRCError` go:** to the outer `catch (const std::exception&)` in `extractValidData`, which **disconnects** that client (the server survives).

**451 is thrown by the Dispatcher, not by `execute`.** For a registration-needing command, `execute` never even runs for an unregistered client.

---
---

# 🔴 PART 6 — Command implementations: *your code → refactored*

Each section: **what you sent** (summary) → **what was wrong** → **final version** → **idea to remember**.

## 6.1 JOIN (the richest example — lists, keys, modes, helpers)

**Your first attempt:** only the first channel; `splitParams` non-static (error 245); no bounds check on keys.
Your sharp question: *"if I call `keys[i]` but keys are empty?"* → exactly the bug; guard with `(i < keys.size()) ? keys[i] : ""`.

**Final:**
```cpp
// helper: tries ONE channel; throws IRCError on failure
void JoinCommand::joinChannel(Server& sv, Client& cl, const std::string& name, const std::string& key)
{
    if (!validName(name)) throw IRCError(403, name);

    Channel* ch = sv.getChannel(name);
    if (ch == NULL)                                   // new channel → creator becomes operator, no modes
    {
        sv.createChannel(&cl, name);
        ch = sv.getChannel(name);
    }
    else
    {
        if (ch->isMember(cl.getSocket())) return;     // already in → silently ignore
        if (ch->getLimit() != 0 && !freeSpace(ch->getLimit(), ch->getSize())) throw IRCError(471, name);
        if (ch->isInviteOnly() && !ch->isInvited(cl.getSocket()))             throw IRCError(473, name);
        if (!ch->getKey().empty() && ch->getKey() != key)                     throw IRCError(475, name);
        ch->addMember(&cl, false);
        ch->removeInvited(cl.getSocket());            // invitation is consumed
    }
    sv.broadcast(*ch, ":" + cl.prefix() + " JOIN " + ch->getName() + "\r\n", -1);
    sendReplies(sv, cl, *ch);                         // 331/332, 353, 366
}

void JoinCommand::execute(Server& sv, Client& user, const Message& msg) const
{
    std::vector<std::string> channels, keys;
    splitParams(msg.params[0].value, channels);
    if (msg.params.size() > 1) splitParams(msg.params[1].value, keys);

    for (size_t i = 0; i < channels.size(); ++i)
    {
        try { joinChannel(sv, user, channels[i], (i < keys.size()) ? keys[i] : ""); }
        catch (const IRCError& e) { sv.sendTo(user, e.format(user.getNickname())); }   // ← loop catch
    }
}
```
`validName`: size 2..50, starts with `#`, no space / comma / BEL (`\x07`) / CR / LF.

**Ideas to remember**
- *Split work in two layers:* `execute` = loop over targets; `joinChannel` = rules for **one** target.
- *Check order* is the order of the numerics: limit → invite → key.
- An empty channel name `#` alone is invalid (size ≥ 2).
- A trailing parameter starting with `:` (like `:#a,#b`) is legal — the parser already stripped the `:`.
- Debug you reported: `JOIN mynewchannel` → `403` (no `#`): **correct** behaviour. You also noticed `getChannel("mynewchannel")` seemed to find `#mynewchannel` — that smells like `find`/substring vs `==`. Compare **whole names**.

## 6.2 INVITE

```cpp
// needsRegistration: true | minParams: 2
void InviteCommand::execute(Server& sv, Client& cl, const Message& msg) const
{
    const std::string& nick = msg.params[0].value;
    const std::string& chan = msg.params[1].value;

    Channel* ch = sv.getChannel(chan);
    if (!ch)                              throw IRCError(403, chan);   // no such channel
    if (!ch->isMember(cl.getSocket()))    throw IRCError(442, chan);   // you're not on it
    if (ch->isInviteOnly() && !ch->isAdmin(cl.getSocket()))
                                          throw IRCError(482, chan);   // op needed only when the channel is +i (RFC); requiring it always is also acceptable
    Client* target = sv.getClientByNickname(nick);
    if (!target)                          throw IRCError(401, nick);   // no such nick
    if (ch->isMember(target->getSocket())) throw IRCError(443, nick + " " + chan);   // already there

    ch->addInvited(target->getSocket());
    sv.sendTo(cl,      ":ircserv 341 " + cl.getNickname() + " " + nick + " " + chan + "\r\n");
    sv.sendTo(*target, ":" + cl.prefix() + " INVITE " + nick + " :" + chan + "\r\n");
}
```
- `INVITE` is **exactly** `<nick> <channel>`; extra params are ignored. (Your question *"min 2 or strictly 2?"* → min 2, extras ignored.)
- Check order **403 → 442 → 482 → 401 → 443**: *"channel, me, rights, target, duplicate."*
- Your test *"inviting myself — is it ok?"* → you're already a member ⇒ `443`.
- *"USER + channel should give 403"* → yes when the channel doesn't exist.
- `getClientByNickname` must be **case-insensitive** (IRC nicks are).

## 6.3 NICK

```cpp
bool NickCommand::isLegit(const std::string& n)     // static
{
    if (n.empty() || n.size() > 9) return false;
    if (n.find_first_of(",.!*@ ") != std::string::npos) return false;
    if (std::string("1234567890-#&$:").find(n[0]) != std::string::npos) return false;
    return true;
}

void NickCommand::execute(Server& sv, Client& cl, const Message& msg) const
{
    if (!cl.getPassState())  throw IRCError(451);                    // strict order: PASS first
    if (msg.params.empty() || msg.params[0].value.empty()) throw IRCError(431);
    const std::string& nick = msg.params[0].value;
    if (!isLegit(nick))      throw IRCError(432, nick);
    Client* other = sv.getClientByNickname(nick);
    if (other && other != &cl) throw IRCError(433, nick);           // someone else has it
    if (other == &cl)        return;                                  // same nick → no-op

    std::string old = cl.prefix();
    cl.setNickname(nick);
    cl.setNickState(true);
    if (cl.getAuthState())   // already registered → announce the change
        sv.sendTo(cl, ":" + old + " NICK :" + nick + "\r\n");        // (typo you had: " NICK :")
}
```
- Bug you hit: `isLegit` non-static called from `const execute` → error 245.
- `std::string::find_first_of` returns **a position**, compare with `npos`.
- `431` empty, `432` illegal, `433` taken.

## 6.4 USER (registration finisher)

```cpp
void UserCommand::execute(Server& sv, Client& user, const Message& msg) const
{
    if (user.getAuthState())  throw IRCError(462);       // already registered
    if (!user.getNickState()) throw IRCError(451);       // NICK first
    const std::string& username = msg.params[0].value;
    const std::string& realname = msg.params[3].value;   // minParams() == 4 guarantees it exists
    if (username.empty() || realname.empty()) throw IRCError(461, "USER");

    user.setUsername(username);
    user.setRealname(realname);
    user.setAuthState(true);
    sv.sendTo(user, ":ircserv 001 " + user.getNickname()
                  + " :Welcome to the Internet Relay Network " + user.prefix() + "\r\n");
}
```
Only 3 error kinds live here (as you summarized): bad format, `461` (the Dispatcher), `451`; plus `462`.
Header must have include guards and `#include <string>`.

## 6.5 PASS

```cpp
// needsRegistration: false | minParams: 1
void PassCommand::execute(Server& sv, Client& user, const Message& msg) const
{
    if (user.getAuthState())              throw IRCError(462);
    if (msg.params[0].value.empty())      throw IRCError(461, "PASS");
    if (msg.params[0].value != sv.getPassword())
    {
        user.setMarkForClose(true);        // close AFTER the 464 is flushed
        throw IRCError(464);
    }
    user.setPassState(true);               // success: no reply line
}
```
- Delete the stray `sv.sendTo(user, "Password approved.")` — IRC clients don't expect a reply.
- Your *"close the client after the dispatcher does its job"* → exactly the **close-after-flush** pattern (Part 7.3).

## 6.6 PRIVMSG (comma targets, nicks and channels mixed)

```cpp
// needsRegistration: true | minParams: 0  (411/412 are decided inside)
void PrivmsgCommand::sendOne(Server& sv, Client& user, const std::string& target, const std::string& text)
{
    const std::string line = ":" + user.prefix() + " PRIVMSG " + target + " :" + text + "\r\n";
    if (target[0] == '#')                                       // channel branch
    {
        Channel* ch = sv.getChannel(target);
        if (!ch)                               throw IRCError(403, target);
        if (!ch->isMember(user.getSocket()))   throw IRCError(404, target);
        sv.broadcast(*ch, line, user.getSocket());               // everyone EXCEPT the sender
    }
    else                                                         // nick branch
    {
        Client* dst = sv.getClientByNickname(target);
        if (!dst)                              throw IRCError(401, target);
        sv.sendTo(*dst, line);
    }
}

void PrivmsgCommand::execute(Server& sv, Client& user, const Message& msg) const
{
    if (msg.params.empty())              throw IRCError(411);   // no recipient
    if (msg.params.size() < 2)           throw IRCError(msg.params[0].isTrailling ? 411 : 412);
    std::vector<std::string> targets;
    splitParams(msg.params[0].value, targets);
    for (size_t i = 0; i < targets.size(); ++i)
    {
        try { sendOne(sv, user, targets[i], msg.params[1].value); }
        catch (const IRCError& e) { sv.sendTo(user, e.format(user.getNickname())); }
    }
}
```
- `PRIVMSG` always has **2 meaningful params** (targets, text); extras are ignored.
- Mixed recipients are legal: `PRIVMSG bob,#chan :hi`.
- *"What does the prompt look like when Alex messages Bianca?"* → Bianca receives `:alex!alex@127.0.0.1 PRIVMSG bia :text`.

### The bug you debugged live (great case study)
Symptoms on `nc`: `PRIVMSG #newch :re all` → **`421`**; later the receiver saw `re allre` (no prefix, no `PRIVMSG`, no newline) and HexChat showed nothing for `PRIVMSG alexkatz :…`.

| Symptom | Diagnosis |
|---|---|
| `421` on PRIVMSG | `_commands["PRIVMSG"]` **never registered** in the Dispatcher constructor |
| `re allre` | the line built was **only the text** — missing `":" + prefix + " PRIVMSG " + target + " :"` and `\r\n` |
| sender receives own message | `broadcast` called with `exceptFd = -1` instead of `user.getSocket()` |

**Debug habits that found it:** print `[` + `msg.command` + `]` before `find`; open HexChat **Raw Log**; compare against a hand-typed correct line.

## 6.7 PING / PONG

```cpp
// PING: needsRegistration false | minParams 1
void PingCommand::execute(Server& sv, Client& cl, const Message& msg) const
{
    sv.sendTo(cl, ":ircserv PONG ircserv :" + msg.params[0].value + "\r\n");   // NOTE the space before ':'
}
// PONG: accept and ignore (so HexChat doesn't get 421)
```
- Bare `PING` → `461` from the Dispatcher (RFC says `409 ERR_NOORIGIN`; use `minParams()=0` + `if (params.empty()) throw IRCError(409);` if you want strict).
- Your typo: `ircserv:msg` → needs **`ircserv :msg`** (space before the colon, otherwise it isn't the trailing param).

### "Why does PING look different from HexChat and nc?" (your question)
| What you typed | Real wire format | Who answers |
|---|---|---|
| `PING token` on **nc** | IRC command `PING` to the **server** | server → `PONG` |
| `/ping bia` in **HexChat** | `PRIVMSG bia :\x01PING 1791839990938\x01` (**CTCP**, `\x01` invisible) | client `bia` (with a `NOTICE`) |

The server just relays the CTCP exactly like any PRIVMSG. Same mechanism as DCC (Part 8.1).

## 6.8 Server helpers

**`broadcast`**
```cpp
void Server::broadcast(const Channel& ch, const std::string& msg, int exceptFd)
{
    const std::map<int, Member>& members = ch.getAll();      // REFERENCE (not a copy)
    for (std::map<int, Member>::const_iterator it = members.begin(); it != members.end(); ++it)
    {
        if (exceptFd > 0 && it->first == exceptFd) continue;
        sendTo(*it->second.cl, msg);
    }
}
```
Bugs we fixed: infinite loop (`while` that never advanced), copying the whole map every call.
`getAll()` must be declared **`const std::map<int, Member>& getAll() const;`** — returning `const T` by value does nothing useful and copies.

The compiler message you got (*"qualifiers dropped in binding reference"*) means: a **non-const** reference/iterator bound to a **const** thing. Rule: *everything coming out of `getAll()` is read via `const&` / `const_iterator`.* To modify members, add dedicated methods (`setAdmin`, `removeMember`).

**`sendTo`** — only appends to the client's out-buffer. Never `send()` here.

## 6.9 KICK (a review that found 8 bugs — the best "why" lesson of the file)

**What you wrote:** two paths — `kickMultipleUsers` (1 channel × N users) and `kickOnPair` (N channels × N users). You knew the RFC shape correctly: that's real protocol knowledge.

**What was wrong (by severity):**

| # | Bug | Effect |
|---|---|---|
| 1 | `catch` used `target->getNickname()` but `target == NULL` exactly when 401 is thrown | **NULL dereference → crash** |
| 2 | `sv.getChannel(...)` may be `NULL`, then `ch->isAdmin(...)` | **crash** on `KICK #nope bob` |
| 3 | `length = chs.size()` computed **before** `splitParams`; 2nd split fills `chs` instead of `users` | loop never runs / out-of-bounds |
| 4 | `KICK` line sent **only to the op** | victim and channel never learn about it |
| 5 | `target->getNickname() + ":" + reason` | missing space → `:reason` isn't trailing |
| 6 | `e.format(...) + "\r\n"` | `format` already ends with `\r\n` → double |
| 7 | no 403 / 442 before 482 | wrong errors |
| 8 | no check `users.size()` vs `chs.size()` | out-of-bounds |

**Fix = reduce both paths to ONE unit: a (channel, nick) pair.** One function judges one pair; `execute` loops.

```cpp
void KickCommand::kickOne(Server& sv, Client& op, const std::string& chanName,
                          const std::string& nick, const Message& msg)
{
    Channel* ch = sv.getChannel(chanName);
    if (ch == NULL)                          throw IRCError(403, chanName);
    if (!ch->isMember(op.getSocket()))       throw IRCError(442, chanName);
    if (!ch->isAdmin(op.getSocket()))        throw IRCError(482, chanName);

    Client* target = sv.getClientByNickname(nick);
    if (target == NULL)                      throw IRCError(401, nick);
    if (!ch->isMember(target->getSocket()))  throw IRCError(441, nick + " " + chanName);

    const std::string reason = (msg.params.size() > 2) ? msg.params[2].value : op.getNickname();
    sv.broadcast(*ch, ":" + op.prefix() + " KICK " + ch->getName() + " "
                      + target->getNickname() + " :" + reason + "\r\n", -1);   // victim included
    ch->removeMember(target->getSocket());                                      // AFTER the broadcast
}

void KickCommand::execute(Server& sv, Client& user, const Message& msg) const
{
    std::vector<std::string> chs, users;
    splitParams(msg.params[0].value, chs);
    splitParams(msg.params[1].value, users);

    if (chs.empty() || users.empty())                    throw IRCError(461, "KICK");
    if (chs.size() != 1 && chs.size() != users.size())   throw IRCError(461, "KICK");

    for (size_t i = 0; i < users.size(); ++i)
    {
        const std::string& chan = (chs.size() == 1) ? chs[0] : chs[i];
        try { kickOne(sv, user, chan, users[i], msg); }
        catch (const IRCError& e) { sv.sendTo(user, e.format(user.getNickname())); }
    }
}
```

**Rules extracted**
- **Check order (C·M·R·T·M):** **C**hannel exists 403 → **M**e in it 442 → **R**ights 482 → **T**arget exists 401 → target is **M**ember 441.
- **Announce before you remove** (same principle as QUIT in `removeClient`): the victim must still be in the member list to receive the line.
- **In a `catch`, never use a pointer that the `try` may have failed to obtain.** Use data you are sure exists (`user`).
- **Two code paths doing the same checks = one function missing.** Find the smallest unit (the pair) and loop over it.
- If a self-kick empties the channel, delete the channel (same rule as `removeClient`).
- `splitParams` now exists in JOIN, PRIVMSG, INVITE, KICK → move it to one shared utility.

**Testing from HexChat:** see 13.3.


---
---

# 🔴 PART 7 — Lifecycle & robustness (the part that makes servers "never crash")

## 7.1 Registration state machine
```
connected ─PASS ok─► passState ─NICK ok─► nickState ─USER ok─► authState (registered)
   │ PASS wrong ⇒ 464 + markForClose ⇒ closed after flush
```
`_closeAfterFlush` and `_markForClose` in `Client` are **redundant** → keep one.
`_hasUser` is unused in the strict design → delete it.

## 7.2 The disconnect funnel — **one** exit door

Everything that ends a connection goes through one function:
```
recv<=0, send<0, POLLHUP/ERR/NVAL, QUIT, wrong PASS, flood  ──►  disconnectClient(fd, reason) ──► removeClient(fd)
```
**Why:** if there are five ways to remove a client, you'll forget a step in four of them.

### The bug you reported: *"after Ctrl+C the client stays logged in"*
`removeClient` only did `_clients.erase` + `close`. Channels still held a **dangling `Client*`**:
- ghost nick in `353`,
- crash on the next `broadcast` (use-after-free).

**Final `removeClient`:**
```cpp
void Server::removeClient(int clientFd)
{
    std::map<int, Client>::iterator itClient = _clients.find(clientFd);
    if (itClient == _clients.end()) return;

    const std::string quitLine = ":" + itClient->second.prefix() + " QUIT :Client disconnected\r\n";   // needs the word QUIT

    std::vector<Channel>::iterator itChannel = _channels.begin();
    while (itChannel != _channels.end())
    {
        itChannel->removeInvited(clientFd);
        if (itChannel->isMember(clientFd))
        {
            itChannel->removeMember(clientFd);                       // remove FIRST …
            if (itChannel->getSize() > 0)
                broadcast(*itChannel, quitLine, -1);                  // … then tell the REST (no self-send)
        }
        if (itChannel->getSize() == 0) itChannel = _channels.erase(itChannel);   // empty channel dies
        else                           ++itChannel;                              // erase XOR ++
    }
    _clients.erase(itClient);
    close(clientFd);
}
```
Three bugs you had in successive versions — good to memorize:
1. `_channels.erase(*itChannel)` — `erase` takes an **iterator**, not the element.
2. `erase` followed by unconditional `++itChannel` — skips / `++end()` UB.
3. `":" + prefix + " :Client disconnected.\r\n"` — **forgot `QUIT`** → clients ignore the line.

**Mnemonic for the order — A·U·E·C:** **A**nnounce (QUIT) → **U**nlist (channels) → **E**rase (client) → **C**lose (fd).

Extras: dedupe QUIT recipients with a `std::set<int>` if users share several channels; if the last operator leaves, optionally promote another member. If `Channel` has `const`/reference members, `vector::erase` fails to compile → use `std::list<Channel>` or `std::map<std::string, Channel>` (map erase in C++98: `m.erase(it++)`).

## 7.3 Close-after-flush (send the error, *then* hang up)

Goal: wrong password must produce a `464` **and** a disconnect, without calling `send` outside POLLOUT.
```cpp
// 1) PassCommand:  user.setMarkForClose(true); throw IRCError(464);   → the 464 goes to the out-buffer
// 2) extractValidData, right after dispatch:
if (cl->getMarkForClose() && !cl->hasOutput()) removeClient(fd);       // nothing to flush → close now
// 3) otherwise sendData() closes it after the LAST byte has been sent:
//    if (cl->getMarkForClose() && !cl->hasOutput()) removeClient(fd);   (see 2.6)
```
(You saw with `nc` that disconnect appeared only at the next keystroke — that's `nc` noticing late, not the server.)

## 7.4 Dangling pointer rule
> **After any call that may remove the client, do not touch the pointer/reference again.**

`extractValidData` must return `false` whenever it removed the client; `handleClientData` returns immediately on `false`. In `handlePollEvents` re-check with `getClientBySocket(fd)`.

## 7.5 Flood & size
- Per-client incomplete buffer limit (512) ⇒ disconnect.
- Also limit a **single** line to 512 bytes (a 600-char complete line slips past the "leftover" check).

## 7.6 Shutdown
After the loop: close every client fd and the listening socket (destructor). `SIGINT/SIGTERM` only set the flag.

---
---

# ⚫ PART 8 — Advanced topics

## 8.1 Bonus: file transfer (DCC) — "where's the catch?"
You asked: *"I can't transfer files with only the allowed libraries — where's the catch?"* and *"so I don't have to implement DCC, just not alter the messages?"* → **Exactly.**
- DCC is **client-to-client**: they open a TCP connection to each other.
- The server's only job: **relay** `PRIVMSG target :\x01DCC SEND file ip port size\x01` **unchanged** (same for `NOTICE`).
- So: don't strip `\x01`, don't truncate, keep ≤ 512 bytes.

## 8.2 Server-initiated keep-alive (optional)
- `Client`: `time_t _lastActivity`, `bool _pingSent`.
- `poll` with a timeout (e.g. 1000 ms) instead of `-1`.
- After each poll: idle > 60 s ⇒ send `PING :ircserv`; no `PONG` within 120 s ⇒ disconnect.
- `PONG` clears `_pingSent`; any `recv` refreshes `_lastActivity`.
- Beware: removal inside the iteration loop — collect fds in a `std::vector<int> toRemove` and remove **after** the loop.
Verdict: not required by the subject → do it **last**.

## 8.3 Data-structure trade-offs
| Choice | Pros | Cons |
|---|---|---|
| `std::vector<Channel>` | simple | `erase` invalidates pointers; O(n) lookup by name |
| `std::map<std::string, Channel>` | O(log n) lookup; stable references | name must be normalized |
| `std::list<Channel>` | stable iterators, cheap erase | O(n) lookup |
| finding a user's channels by looping `_channels` with `isMember(fd)` | no second index to keep in sync | O(#channels) per disconnect — fine at this scale |

A separate `_channelsUsers` map = **two sources of truth** ⇒ they drift apart. Prefer one.

## 8.4 Complexity quick-think
- `broadcast`: O(members).
- `getClientByNickname`: O(clients) if you scan; add a `std::map<std::string,int>` only if profiling proves it matters (and then keep it in sync inside the same function that changes nicks).
- `rebuildPollFds` each turn: O(clients) — negligible next to `poll` itself.

## 8.5 Case-insensitivity
IRC nicks and channel names compare **case-insensitively**. Normalize (lowercase) for *comparison/keys*, keep the original for *display*.

---
---

# 📚 PART 9 — Your questions and my answers (condensed log, in order)

### Commands & parsing
| Your question | Short answer |
|---|---|
| "Send me again the JOIN example and where do the vectors come from?" | The vectors are **local to `execute`**; `splitParams(str, vec)` fills them. |
| "JOIN is `JOIN #ch1 #ch2 #ch3`, so `params[0]` gets only ch1" | Real syntax is **commas**: `JOIN #a,#b`. Keys also comma-separated. |
| "INVITE: min two params or strictly two?" | Min 2; extras ignored. |
| "Do I make the split variables in `execute`?" | Yes — locals in `execute`, helper `static`. |
| "INVITE bob #a,#b,#c?" | INVITE takes one channel; lists aren't part of it. |
| "Why error when `splitParams`… qualifiers not compatible?" | const method calling non-static helper → make it `static`. |
| "When creating a channel, do I set the limit too?" | No. New channel: no modes, `_limit = 0` (none), `_key` empty. |
| "`:#a` is a valid trailing" | Correct — parser strips `:`; `validName` sees `#a`. |
| "`keys[i]` when keys is empty?" | Guard: `i < keys.size()`. |
| "Is the `throw` in `joinChannel` visible in the dispatcher too?" | Only if no inner catch stops it; inside the loop the inner catch handles it. |
| "Why catch in commands if the dispatcher does?" | Because of **loops** (Part 5). |
| "Min parameter of USER?" | 4. |
| "What does mode 0 mean?" | Bitmask (4 = w, 8 = i). Ignore. |
| "Do I implement modes in USER?" | No. |
| "Does USER need `tryRegister` / `_hasUser`?" | Not in your strict order. |
| "Does username/realname matter for forbidden chars? Unique?" | No; only nick is unique. |
| "What is the username for?" | It's in the prefix `nick!username@host`. |
| "Do I set host in USER?" | No — host is the socket IP. |

### Language mechanics
| Question | Answer |
|---|---|
| "`it++` vs `++it` — one keeps its value, the other passes" | In a `for` slot identical; differ in expression value. |
| "What is `catch (...) { clear(); throw; }`?" | Clean up then re-throw (ctor that throws has no dtor). |
| "Same as `catch (std::exception& e)`?" | No — `(...)` catches everything, `throw;` keeps type. |
| "What errors can `std::exception& e` NOT handle?" | Segfault, UB, SIGPIPE, infinite loops, non-`std::exception` throws. |
| "Why ICommand?" | One polymorphic type for the map. |
| "`new JoinCommand()` abstract" | Missing `const` on the override. |

### Server & robustness
| Question | Answer |
|---|---|
| "I can register or not depending on dispatch result?" | In your design USER itself finishes registration. |
| "Wrong PASS: close after dispatcher is done?" | `markForClose` + flush then close. |
| "Ctrl+D never sends; Ctrl+D ×N breaks?" | One D flushes, two D's = EOF (Part 2.7). |
| "Where do I send PING to reply clients?" | Server-initiated keep-alive in the loop (Part 8.2). |
| "Ctrl+C and the client stays logged" | `removeClient` didn't clean channels (Part 7.2). |
| "Is this done in `run()`?" | `run()` detects; `removeClient()` does. |
| "Why not `for(...)` with channel iterator + `while`?" | Equivalent; the point is *where `++` goes*. |

### KICK (added later)
| Question | Answer |
|---|---|
| "Review my KICK (two paths: one channel × N users, and pairs)" | 8 bugs (6.9); reduce to one `(channel, nick)` unit + one loop. |
| "From HexChat, how do I kick from the command line?" | `/kick nick [reason]` in the channel window; anywhere: `/quote KICK #chan nick :reason` (see 13.3). |

### Testing & tools
- HexChat: *"wrong version number"* = SSL enabled → disable "Use SSL"; use `127.0.0.1` / your port; set the server password in the network settings.
- HexChat **Raw Log** shows exact bytes.
- `nc` disconnect notices appear on the next input.

### Bonus / protocol
| Question | Answer |
|---|---|
| "PRIVMSG always 2 args?" | Yes (targets, text). |
| "Mix of channels and names as recipients?" | Yes, comma list. |
| "Where's the catch with file transfer?" | DCC is client↔client; relay unchanged. |

---
---

# 💡 PART 10 — Insights you had (and why they were valuable)

1. **"`JOIN #ch1 #ch2` has 3 tokens — `params[0]` only gets one."** You noticed the gap between *how text is tokenized* and *how a list is encoded*. It led to comma-lists + `splitParams`.
2. **"I want the dispatcher to be just a map of name → handler."** You were pushing for a *small core*. It led to `ICommand` and generic checks in one place.
3. **"What if `keys[i]` doesn't exist?"** Boundary thinking: every indexed access needs a guard.
4. **"Why try/catch in the command if the dispatcher already catches?"** The question that forced the sharpest rule: *only loops need inner catches.*
5. **"NICK shouldn't work before PASS."** You defended a **strict state machine** — and it removed the need for `tryRegister` and `_hasUser`. (Fewer states = fewer bugs.)
6. **"I want to disconnect *after* the dispatcher does its job."** You independently asked for **close-after-flush**.
7. **"`it++` keeps its value, `++it` passes by."** Right instinct about *expression values*; the lesson was that in a `for` slot it doesn't matter.
8. **"What errors can `exception& e` not handle?"** You asked what a safety net does **not** cover — the mark of someone thinking about failure modes.
9. **"Why does my PING look different from HexChat's?"** You noticed **two different protocols sharing one pipe** (command vs CTCP).
10. **"Same nick Ctrl+C — still logged."** You tested the *unhappy path* and found a real dangling-pointer bug.
11. **"KICK has two shapes: one channel × many users, or pairs."** You knew the RFC detail others skip. The refactor then reduced both to one *pair* — a good example of "different shapes, same unit".
12. **"How do I kick from the command line?"** You went to a real client to test the unhappy paths instead of trusting the code — the habit that found the earlier bugs.

---
---

# 🏛️ PART 11 — Rules, architecture and habits of great programmers' code

### Architecture
1. **One responsibility per class.** Client = state, Channel = membership, Dispatcher = validation + routing, Command = one rule set.
2. **Validate at the boundary, once.** Generic checks (421/451/461) in the Dispatcher; specific ones in the command.
3. **One source of truth.** Never keep two structures that must stay in sync (e.g. `_channels` + `_channelsUsers`).
4. **One exit door per lifecycle event.** All disconnects → `disconnectClient → removeClient`.
5. **Make illegal states hard to reach.** Strict registration order = fewer flags.
6. **Stateless commands.** Objects with no data are trivially safe to share and reuse.

### Safety
7. **Never trust the network.** Partial data, garbage, huge lines, abrupt closes — all expected.
8. **Non-owning pointers need an owner's plan.** When the owner deletes, everyone else must forget.
9. **After a call that may destroy something, stop touching it.**
10. **Errors are values flowing to one handler**, not scattered `if/else` printing.
11. **A safety net (try/catch) is not a substitute for correctness.** It doesn't catch UB.
12. **Fail closed.** Unknown command → 421; bad state → 451; never "best guess."

### Style
13. **Const-correctness**: methods that don't mutate are `const`; helpers with no state are `static`.
14. **Pass by `const&`** for read-only input; return `const&` for big read-only data; never copy a container by accident.
15. **Name things for what they mean** (`needsRegistration`, `isInviteOnly`), not how they work.
16. **Small functions with a single level of abstraction** (`execute` loops, `joinChannel` decides).
17. **Declaration ≡ definition.** Exact signatures in `.hpp` and `.cpp`.
18. **Include what you use** (`<string>` in the header that mentions `std::string`).

### Process
19. **Test the unhappy path first** (wrong PASS, half a line, Ctrl+C, 600-byte line).
20. **Read raw bytes** (HexChat Raw Log, `nc`, `printf | nc`) before guessing.
21. **Change one thing, rerun.** Print the smallest fact that discriminates two hypotheses (e.g. `[command]` before `find`).
22. **Follow the spec first (RFC / subject), your preferences second.**

---
---

# 🧭 PART 12 — The logical path of development (build order)

Each step is testable on its own; don't start the next until the current one is green.

| # | Step | You can test with |
|---|---|---|
| 1 | Listening socket + `poll` + accept (non-blocking) | `nc` connects, server logs |
| 2 | Per-client in/out buffers + line framing | `com`^D `man`^D `d`⏎ |
| 3 | `IRCParser` → `Message` | print command/params |
| 4 | `IRCError` + `Dispatcher` + `ICommand` skeleton | unknown command → `421` |
| 5 | PASS → NICK → USER (+ `001`) | `nc` handshake |
| 6 | `sendTo` + POLLOUT flush | replies appear |
| 7 | `removeClient` that cleans **everything** | Ctrl+C, then JOIN |
| 8 | JOIN / PART / NAMES, channels | two `nc` clients |
| 9 | PRIVMSG / NOTICE (+ broadcast) | HexChat ↔ nc |
| 10 | TOPIC, INVITE, KICK | operator rules (482) |
| 11 | MODE i t k o l | `mode_combinations.md` cases |
| 12 | PING/PONG, QUIT | HexChat idle for minutes |
| 13 | Flood limits, SIGPIPE, shutdown | `yes | nc …`, `kill -INT` |
| 14 | Bonus: DCC relay, bot, keep-alive | HexChat `/dcc send` |

---
---

# 🛠️ PART 13 — Debugging playbook & mnemonic sheet

## 13.1 Symptom → first suspect

| Symptom | First suspect |
|---|---|
| `421` for a command that exists | not registered in `Dispatcher` ctor; command not uppercased; `msg.command` has `\r` |
| `461` on a command with params | `minParams()` too high / parser put params in wrong order |
| no reply at all | forgot `sendTo`; client has no `POLLOUT`; missing `\r\n` |
| text glued to next text (`re allre`) | missing `\r\n`, or building the line from text only |
| client "stays logged in" | `removeClient` missed channels; `recv==0` ignored |
| crash after a disconnect | dangling `Client*` in a channel |
| crash in a loop with `erase` | `++` after `erase`; cached `end()` |
| commands never execute | no `\n` received (Ctrl+D trap); buffer overwritten |
| `error 245` / "type qualifiers" | const method calling non-const/non-static |
| "abstract class" | override missing `const` |
| `<error type>` | missing `#include <string>` |
| HexChat "wrong version number" | SSL enabled in the network |
| KICK crashes on a wrong nick/channel | `NULL` used in `catch` or before the check (6.9) |
| victim doesn't see the KICK | removed from members before `broadcast` |
| `:reason` shows as a param | missing space before the `:` |
| every error line appears twice with blank line | `format()` already adds `\r\n` |

## 13.2 Mnemonic sheet (the whole file in 12 lines)

1. **Pipeline:** Really Fast Parrots Deliver Eggs, Quietly Peeling Shells *(Recv→Frame→Parse→Dispatch→Execute→Queue→Poll→Send)*.
2. **Registration:** *PNU* — PASS → NICK → USER.
3. **Dispatch checks:** *U·R·P·E* — Unknown, Registered, Params, Execute.
4. **Errors:** Commands **throw**, Dispatcher **catches**, loops **catch-and-continue**.
5. **const:** a const method sees only const and static.
6. **erase:** erase returns the next — if you erased, don't `++`.
7. **Events:** IN reads, OUT writes, HUP/ERR/NVAL bury.
8. **Ctrl+D:** one D flushes, two D's kill.
9. **Framing:** append, find `\n`, cut, repeat — with a reference.
10. **Disconnect:** A·U·E·C — Announce, Unlist, Erase, Close.
11. **Join checks:** L-I-K → Limit 471, Invite 473, Key 475.
12. **Safety:** after anything that may delete the client, stop touching it.

## 13.3 HexChat commands for testing (type in the bottom box, with `/`)

| Goal | Command | Sent to your server |
|---|---|---|
| join | `/join #newch` · with key `/join #newch key` | `JOIN #newch [key]` |
| kick (current channel window) | `/kick bia` · `/kick bia reason` | `KICK #newch bia :reason` |
| kick from anywhere / many | `/quote KICK #chan nick1,nick2 :reason` | exact line |
| invite | `/invite bia #newch` | `INVITE bia #newch` |
| topic | `/topic new text` | `TOPIC #newch :new text` |
| modes | `/mode #newch +i` · `+k secret` · `+l 5` · `+o bia` | `MODE ...` |
| private msg | `/msg bia hello` | `PRIVMSG bia :hello` |
| nick | `/nick newname` | `NICK newname` |
| leave | `/part #newch` | `PART #newch` |
| raw line (any command) | `/quote COMMAND args` | unchanged |
| see raw traffic | **Window → Raw Log** (or `View → Raw Log`) | — |

What a successful KICK must look like on **every** member (victim included):
`:alex!alex@127.0.0.1 KICK #newch bia :reason` → HexChat prints "alex has kicked bia from #newch (reason)".
Victim doesn't see it ⇒ you called `removeMember` **before** `broadcast`.
Error tests: `/quote KICK #nope bia` → 403 · from a non-op → 482 · `/quote KICK #newch ghost` → 401.

## 13.4 Capstone exercises (last-year level)
1. Add **keep-alive** (Part 8.2) without invalidating iterators — use the `toRemove` pattern.
2. Switch `_channels` to `std::map<std::string, Channel>` with **case-insensitive keys**; fix `removeClient` accordingly.
3. Add a **recipient-deduplicating QUIT** (`std::set<int>`) so a user in 5 shared channels sends one line.
4. Write a **stress test script** (bash + `nc`/Python) that opens 200 clients, joins them to one channel, sends partial lines, and kills random sockets; the server must never crash (run under `valgrind --track-fds=yes`).
5. Implement `MODE +o/-o/+k/-k/+l/-l/+i/-i/+t/-t` with **combined strings** (`+kl key 10`) and the correct numerics — see `mode_combinations.md`.

---

# 🧭 PART 14 — Pre-flight questions (ask BEFORE you type)

> **Rule zero:** if you can't answer a question below in **one sentence**, you are not ready to write code. Writing is the slow way to think.

---

## 14.1 Before writing a FUNCTION

| # | Ask | Verdict |
|---|---|---|
| 1 | What is its **one** job, as a verb phrase? | Needs "and" → **split it**. |
| 2 | What goes in, what comes out, what must the caller guarantee? | Unknown → **don't write it yet**. |
| 3 | What can fail, and how is failure reported (throw / return / impossible)? | Decide **now**, not mid-body. |
| 4 | Does it touch object state? | No → `static` or free function. Reads only → `const`. |
| 5 | Can it invalidate something (`erase`, `delete`, `close`)? | Yes → write it in the **comment** and re-check callers. |
| 6 | Does this already exist somewhere? | Yes → **reuse**. (`splitParams` ×4 = bug.) |
| 7 | Can a one-line test prove it? | No → the function does too much. |
| 8 | Does the name tell the truth? | `is/has` for bool, verb for action. Names with `And`, `Manager`, `Handle`, `Process` → **rename**. |

Hard limits: more than ~30 lines, or two levels of abstraction (loop **and** rules) → split (`execute` loops, `joinChannel` decides).

---

## 14.2 Before a PROJECT DESIGN

| # | Ask | Verdict |
|---|---|---|
| 1 | What enters and what leaves the system? | Draw the **pipeline** first. |
| 2 | What are the **nouns** and the **verbs**? | Nouns → classes/data. Verbs → methods/commands. |
| 3 | **Who owns** each piece of data? | **Exactly one** owner each. Two owners = bug. |
| 4 | Where do failures enter and where are they handled? | **One** place. |
| 5 | What changes often, what never? | Put the often-changing (commands) behind an **interface**. |
| 6 | What are the hard external rules? | Write them down (subject, RFC) before designing. |
| 7 | What is the build order, each step testable alone? | No testable step → step too big. |
| 8 | What am I **not** building? | Unlisted = out of scope. |

No design is done without: a pipeline drawing + an ownership table + a failure-handling point.

---

## 14.3 Before a CLASS

| # | Ask | Verdict |
|---|---|---|
| 1 | What does it represent/own, in one sentence **without "and"**? | Can't → wrong class. |
| 2 | What must **always** be true about it (invariants)? | None → it's a `struct`. |
| 3 | What is private? | **Everything**, until a caller proves it needs access. |
| 4 | Does it own a resource (`new`, fd)? | Yes → destructor + private/implemented copy (Rule of Three). No → write none. |
| 5 | Does it need state at all? | No → free/static functions, not a class. |
| 6 | Who creates it, who destroys it, who may point to it? | Any non-owning pointer → plan what happens on delete. |
| 7 | Is the public interface the **minimum**? | Remove what no caller uses. |
| 8 | How many other classes' internals does it know? | 3 or more → wrong boundary. |

Smell: a class that is only getters/setters = a `struct` in disguise. A class that does I/O **and** rules = two classes.

---

## 14.4 Before choosing the KIND of class

| Kind | Use when | Not when |
|---|---|---|
| **struct** | just groups data (`Message`, `Param`) | it has invariants |
| **concrete class** | one implementation, owns data + rules (`Client`, `Channel`) | you'll need to swap it at runtime |
| **interface** (pure abstract) | many different implementers behind one call (`ICommand`) | only one implementer exists |
| **abstract base with shared code** | ≥ 2 implementers **already** duplicate code | you "might" share later |
| **static helpers / free functions** | no state (`validName`, `split`) | they need object data |
| **singleton / global** | **never** (except the signal flag, which has no alternative) | — |

Ask in this order: *Need to swap behavior at runtime? → interface. Same logic, different data? → one class with data. Just data? → struct. No state? → function.*

---

## 14.5 Before choosing a function's PARAMETERS

| Need | Use |
|---|---|
| small read-only value (`int`, `bool`, `char`) | by value |
| large read-only (`string`, `vector`) | `const T&` |
| function must change the caller's object | `T&` |
| may be absent | pointer (check `NULL`) — otherwise a reference |
| function **keeps** it after returning | decide the owner **in writing**: copy, or pointer with an owner |
| function returns a single result | **return it**, no out-parameter |
| several results / fill a container | `T&` out-parameter, placed **last** |

Hard rules:
1. Everything that doesn't change is `const`.
2. **More than 4 parameters → group them** in a struct (or you're passing two jobs).
3. **A `bool` that switches behavior → two functions.**
4. Order: context (`Server&`) first, inputs next, outputs last.
5. Default values: only for truly optional, only in the header.
6. Never return a reference to a local, and never keep a reference across an `erase`/`insert`.

---

## 14.6 Before building a CONTROLLER / DISPATCHER

| # | Ask | Verdict |
|---|---|---|
| 1 | What is the key → handler mapping? | A `map<name, handler>`. |
| 2 | Which checks apply to **every** handler? | Only those (unknown, auth, arity) go in the dispatcher. |
| 3 | Who owns the handlers and for how long? | Dispatcher: `new` in ctor, `delete` in `clear()`, copy private. |
| 4 | Where do errors become replies? | **One** `try/catch`. |
| 5 | How do I add a handler without editing existing code? | One line in the ctor + one new class. Otherwise the design is wrong. |
| 6 | Are handlers stateless, getting context by parameter? | They must be. |

**Categorical:** a dispatcher that contains a command-specific `if` is broken. It routes and validates generically — **business rules never live there**.

---

## 14.7 Interface vs inheritance vs abstract class

| Tool | Meaning | Use only if |
|---|---|---|
| **Interface** (only pure virtuals + `virtual ~X() {}`) | a **contract** | unrelated implementers must be called through one type |
| **Abstract base with code/data** | a **partial implementation** | ≥ 2 subclasses share real, already-duplicated code |
| **Concrete inheritance** (extend a working class) | "is-a" **and** substitutable | almost never |
| **Composition** ("has-a") | reuse by holding an object | **default** |

Questions, in order:
1. Is it truly **"is-a"** *and* does the subclass keep **all** the base's promises (can I use it anywhere the base is used)? If not → **no inheritance**.
2. Do I actually call it through a base pointer/reference? If not → **no inheritance**.
3. Am I inheriting just to reuse code? → **composition or a free function**.
4. Will callers need `delete` through the base? → `virtual` destructor, always.

C++98 traps: override must match the signature **exactly** (including `const`; there is no `override` keyword), pure virtual is `= 0`, never call virtuals from a constructor.

**Verdict:** default to composition; interface when you need polymorphism; abstract base only after duplication is **proven**; **never** inherit only for code reuse.

---

## 14.8 In general, whenever you program

**Before**
1. State the problem in **one sentence**: input → output.
2. What is the **simplest** thing that works?
3. How can it break? Check this list every time: *empty, NULL, too big, too small, partial, duplicated, disconnected mid-way, reused fd, case differences.*
4. Who owns what; who frees what?
5. How will I test it in under a minute?

**During**
6. **One change, then run.** Never two.
7. Read the **first** error only. Everything after is usually its echo.
8. When stuck, print the **one fact** that separates two hypotheses.
9. After any call that can destroy an object, **stop touching it.**

**After**
10. Delete dead code and commented-out code.
11. Any copy-paste of 5+ lines → extract it.
12. Would a stranger understand it without you? If not, rename or split.

---

## 14.9 The 30-second card

| Moment | The one question |
|---|---|
| function | "What is its **one** job?" |
| class | "What does it **own**, and what must always be true?" |
| parameter | "Does it change, can it be absent, is it big?" |
| design | "Who owns each piece of data, and where do errors end up?" |
| dispatcher | "Is anything command-specific in here?" (must be **no**) |
| inheritance | "Do I call it through the base?" (if **no** → don't inherit) |
| any code | "How does this break?" |

---

## Still open on your side (from our last checks)
- Register **every** command in the Dispatcher constructor (PRIVMSG was missing).
- Build PRIVMSG lines with prefix + `PRIVMSG` + target + `:` + text + `\r\n`; pass `user.getSocket()` as `exceptFd`.
- Remove the stray `"Password approved."`; uppercase commands in the parser; fix `" NICK :"`.
- Rewrite KICK as one `kickOne(channel, nick)` + one loop (6.9): fix the `NULL` use in `catch`, the missing broadcast, `splitParams` into `users`, and the 403/442/482/401/441 order.
- Move `splitParams` to one shared utility.
- Implement PART, TOPIC, MODE, QUIT, NOTICE; then test with HexChat **and** `nc` (13.3).

*End of guide — read it once top-to-bottom, then keep Part 13 (debugging) and Part 14 (pre-flight questions) next to your keyboard.*
