# C++ recovery roadmap for ft_irc

This is a restart plan for a student who completed only CPP00-CPP04 and then
stopped practicing for two months. The goal is not to learn all of C++ before
starting ft_irc. The goal is to recover the C++ habits and system concepts that
are directly needed to build a small, correct IRC server.

The plan assumes the 42 C++ rules, usually C++98, and a two-month break. It is
organized into four intensive weeks. Study six days per week, approximately
three focused hours per day. Keep the seventh day for recovery, catch-up, and
review. Start the server during Week 2; do not wait until the C++ review feels
complete.

## 1. What you already need from CPP00-CPP04

You do not need to restart from zero. Before beginning the server, you should
be able to use these ideas again:

- classes, constructors, destructors, and member functions;
- `private`, `public`, and basic encapsulation;
- references, pointers, and `const`;
- initialization lists;
- copying and assignment;
- inheritance and virtual functions;
- exceptions;
- basic file and stream operations;
- Makefiles and compiler warnings.

The most important recovery topics are not advanced C++. They are ownership,
container usage, string parsing, error handling, and the Unix socket API.

## 2. The final target

By the end of the four weeks, the target is a single-process IRC server that
can:

- listen on a TCP port;
- accept several clients without blocking on one client;
- reconstruct IRC messages from fragmented TCP input;
- register clients with PASS, NICK, and USER;
- maintain users and channels;
- implement JOIN, PART, PRIVMSG, NOTICE, QUIT, PING/PONG, TOPIC, MODE, KICK,
  INVITE, and NAMES as required by the project;
- queue partial writes correctly;
- clean up users and channel membership after disconnects;
- reject malformed input and enforce resource limits.

Do not begin with server-to-server IRC links. A complete local server is already
a substantial project.

## 3. How to study each day

Use the same rhythm every study day:

1. Spend 20 minutes recalling yesterday's material without looking at notes.
2. Read or watch one small concept for 30 minutes.
3. Write code for 50-70 minutes.
4. Test it with compiler warnings and a few deliberate failures.
5. Write three lines: what worked, what was confusing, and what to repeat.

Use a compiler command similar to:

```sh
c++ -Wall -Wextra -Werror -std=c++98 -g *.cpp
```

For larger exercises, use a Makefile. Keep warnings enabled from the first
exercise. Do not use `using namespace std;` in new code, and avoid features
that are unavailable in the C++ standard required by your project.

## 4. Week 1 - recover C++ and build the parser

### Goal

Recover only the C++ needed by the server and finish a tested IRC line parser.

### Review

- primitive types, conversions, and scope;
- functions, references, pointers, and `const`;
- classes and object construction;
- initialization lists;
- copy constructor and assignment operator;
- destructors and ownership;
- inheritance and virtual destructors;
- exceptions and `try`/`catch`;
- `std::string`, `std::vector`, `std::map`, and `std::set` at a basic level.

### Days 1-2: C++ recovery

Write a small `User` class with a nickname, username, constructors, getters,
and a display-prefix method. Review references, pointers, `const`, initialization
lists, copying, assignment, destructors, exceptions, and virtual destructors.

Use `std::string`, `std::vector`, `std::map`, and `std::set` in a small program.
Implement the rule of three once for a class that owns dynamic memory, then
explain who owns each object. Do not spend time on advanced inheritance or
templates that the server does not need.

### Days 3-6: parser and serializer

1. Write `split_spaces` for ordinary words.
2. Parse an optional prefix, command, middle parameters, and the IRC trailing
   parameter from:

```text
NICK alice\r\n
USER alice 0 * :Alice Example\r\n
PRIVMSG #chat :hello with spaces\r\n
```

3. Return a structure containing the optional prefix, command, and ordered
   parameters.
4. Reject NUL, CR, LF inside parameters and lines over 512 bytes.
5. Write a serializer that always emits `\r\n` and respects the 512-byte limit.
6. Test empty lines, empty trailing parameters, missing parameters, fragmented
   input, and multiple commands in one input string.

### Completion check

You can explain who owns every object in your exercise, and you know whether a
function should receive a value, pointer, or reference. Your parser can handle
fragmented input, multiple messages, trailing parameters, and line limits.

## 5. Week 2 - Unix sockets and the multi-client core

### Goal

Build a small multi-client TCP server and connect it to the Week 1 parser.
This week is the point where the IRC project begins.

### Days 1-2: system programming recovery

- file descriptors and ownership;
- `open`, `read`, `write`, `close`, `errno`, `perror`, and `strerror`;
- blocking versus non-blocking operations;
- partial reads and partial writes;
- Makefiles, compiler warnings, `gdb`, and project exit codes.

Write a file-copy program using `open`, `read`, `write`, and `close`. Make the
write loop handle partial writes. Then write a line reader that handles a
partial final line. Add a simple signal-driven shutdown request without doing
complex work inside the signal handler.

### Days 3-6: TCP and multiplexing

Learn the purpose and normal order of:

```text
socket -> setsockopt -> bind -> listen -> accept
read/recv -> write/send -> close
```

Review IPv4 address structures, port conversion, non-blocking mode, `poll`,
readable and writable events, `EAGAIN`, and `EWOULDBLOCK`.

1. Write a one-client blocking echo server.
2. Convert it to non-blocking sockets.
3. Add `poll` and accept multiple clients.
4. Give each client a private input buffer and output queue.
5. Handle partial writes and remove clients on EOF or socket errors.
6. Connect with several `nc 127.0.0.1 PORT` processes.
7. Replace echoing with IRC line extraction and parser dispatch.

### Completion check

A slow or idle client cannot freeze another client. The server accepts several
connections, extracts complete IRC lines, and queues partial writes correctly.

## 6. Week 3 - registration, users, channels, and messages

### Goal

Turn the multi-client core into a usable local IRC server.

### Days 1-2: connection state and registration

Each connection needs at least:

```text
fd
input_buffer
output_queue
registration_state
optional nick
optional username
optional realname
last_activity_time
closing flag
```

Use these registration states:

```text
UNREGISTERED -> NICK_SEEN -> REGISTERED
```

Implement:

1. PASS if required by the project.
2. NICK with validation and uniqueness checks.
3. USER with its trailing real-name parameter.
4. Registration only after all required fields are valid.
5. Welcome numerics `001` through `004`.
6. `451 ERR_NOTREGISTERED` for ordinary commands too early.

### Days 3-6: users, channels, and delivery

Use these indexes:

```text
connections: fd -> Connection
users_by_nick: normalized nickname -> User
channels_by_name: normalized channel -> Channel
```

Implement in this order:

1. Private `PRIVMSG nick :text`.
2. `NOTICE`, with no automatic error replies.
3. Channel creation, JOIN, and creator operator status when required.
4. PART and deletion of empty ordinary channels.
5. Channel `PRIVMSG` to eligible members.
6. TOPIC and NAMES.
7. Required modes, especially `i`, `t`, `k`, `o`, `l`, `n`, and `m` if in scope.
8. INVITE and KICK.

For every handler, check registration, parameters, target existence, membership,
permissions, and channel modes before mutating state.

### Completion check

You can connect with `nc`, register manually, and see correct IRC replies. The
server remains responsive while several clients are connected.

## 7. Week 4 - hardening, integration, and submission readiness

### Goal

Make the server survive malformed input and disconnects, then test the required
workflow end to end. This week is deliberately focused on correctness instead
of adding optional features.

### Days 1-2: cleanup and liveness

Every termination reason must use the same conceptual operation:

```text
mark connection as closing
broadcast QUIT if the user was registered
remove the user from every channel
remove the nickname index entry
remove the descriptor from poll
close the descriptor
release connection memory
```

Cover QUIT, EOF, read errors, write errors, malformed input, timeout, and
server shutdown.

### Add liveness

- track the last activity time;
- send PING after an idle period;
- expect PONG before a deadline;
- close unresponsive clients through the normal cleanup path.

### Days 3-4: limits and protocol correctness

At minimum protect:

- maximum input line and incomplete input buffer;
- maximum output queue;
- maximum clients;
- maximum channels per user;
- maximum topic and nickname lengths;
- maximum mode masks and channel members;
- command rate per client.

Add or verify:

- input line and incomplete-buffer limits;
- output queue limits;
- maximum clients, channels per user, topic length, and mode masks;
- correct `CR LF` termination and 512-byte maximum;
- numeric replies for malformed, unauthorized, and missing targets;
- no blocking calls inside command handling.

### Days 5-6: integration and submission tests

1. Build a clean command table and keep handlers small.
2. Centralize numeric reply construction, validation, and output queueing.
3. Test fragmented reads, combined reads, partial writes, EOF, and slow clients.
4. Test all permission failures, not only successful commands.
5. Compare behavior against the required project specification and RFC 2812.
6. Use `gdb`, compiler warnings, and a memory checker available in your
   environment to investigate failures.
7. Read the existing IRC README again and explain the entire event flow aloud.

The Week 4 deliverable is a local IRC server that two or more raw clients can
use for registration, private messages, channel messages, JOIN, PART, TOPIC,
MODE, KICK, INVITE, NAMES, PING/PONG, and QUIT.

### Do not add yet

Do not spend these four weeks on:

- server-to-server synchronization;
- services;
- DNS or Ident lookups;
- offline message storage;
- channel delay and safe channels;
- compression;
- TLS;
- a large abstraction framework.

Understand these topics from the RFCs, but implement them only after the local
server is stable and the project requirements demand them.

## 12. The minimum C++ knowledge needed during ft_irc

You should be comfortable with these tools while coding:

### Classes and ownership

Know which object owns a resource and when it is destroyed. File descriptors,
heap allocations, and pointers to users are especially important. Avoid storing
pointers or references to objects whose lifetime you cannot guarantee.

### `const`

Use `const` to make read-only operations visible:

```cpp
const User *findUser(const std::string &nick) const;
```

The exact design may differ, but the habit matters: command handlers should not
silently mutate state while performing a lookup.

### Containers

Use a container because of the operation it supports:

- `std::map` for lookup by nickname or channel name;
- `std::set` for uniqueness and membership;
- `std::vector` for ordered lists and poll descriptors;
- `std::string` for protocol text and buffers.

Know iterator invalidation before erasing while iterating.

### Exceptions and return values

Use exceptions for unrecoverable construction or configuration failures if that
matches the project style. Use ordinary return values for expected protocol
errors such as an invalid nickname or missing channel. A bad client command is
not a server crash.

### Polymorphism

You probably do not need an inheritance hierarchy for every command. A simple
command table or member-function dispatch is often easier to debug. Use virtual
functions when the behavior is genuinely substitutable, not just because
inheritance was introduced in CPP03.

## 13. Core tests to keep permanently

Create a small test checklist and run it after every meaningful change:

### Parser

- empty line;
- command with no parameters;
- prefix;
- trailing parameter with spaces;
- empty trailing parameter;
- fragmented line;
- multiple lines in one read;
- CR without LF;
- overlong line;
- invalid NUL.

### Registration

- PASS/NICK/USER in normal order;
- NICK before PASS when a password is required;
- duplicate nickname;
- invalid nickname;
- duplicate USER;
- command before registration;
- disconnect before registration completes.

### Channels

- first JOIN creates a channel;
- second JOIN receives topic and NAMES;
- wrong key;
- invite-only channel;
- channel limit;
- banned user;
- non-operator MODE/KICK/INVITE;
- last user PART removes an ordinary channel.

### Networking

- several simultaneous clients;
- fragmented input;
- several commands in one read;
- partial output writes;
- slow reader;
- EOF;
- connection reset;
- PING timeout;
- graceful shutdown.

## 14. Signs that you are ready to code the server

You are ready to begin the first IRC slice when you can answer these questions
without guessing:

1. What happens if one `read()` returns half of `PRIVMSG`?
2. What happens if one `read()` returns three commands?
3. Where are incomplete bytes stored?
4. What happens if `send()` writes only 20 bytes of a 100-byte reply?
5. When exactly does a nickname become reserved?
6. Which object removes a user from every channel?
7. What happens when a client closes without sending QUIT?
8. How does a command handler know whether the sender is a channel operator?
9. Which errors are sent as numeric replies?
10. How do you prevent an unregistered client from using JOIN?

If you can explain those answers, you do not need to wait until every C++ topic
feels familiar. Start implementing, keep the slices small, and let compiler
errors and tests identify the next concept to review.

## 15. Suggested first seven coding sessions

These are deliberately concrete:

1. **Session 1:** compile a small C++98 program with warnings; recreate a
   `User` class and a `std::map` lookup.
2. **Session 2:** implement and test IRC line parsing from a `std::string`.
3. **Session 3:** write a one-client blocking TCP echo server.
4. **Session 4:** convert it to non-blocking sockets and `poll`.
5. **Session 5:** add per-client input buffers and output queues.
6. **Session 6:** implement PASS, NICK, USER, and welcome replies.
7. **Session 7:** connect two raw clients and implement private PRIVMSG.

After session 7, the project is no longer abstract. It is a sequence of small
problems: add one state transition, one command, one test, and one cleanup rule
at a time.

## References

- [ft_irc implementation guide](README.md)
- [RFC 2810: Internet Relay Chat: Architecture](https://www.rfc-editor.org/info/rfc2810/)
- [RFC 2811: Internet Relay Chat: Channel Management](https://www.rfc-editor.org/info/rfc2811/)
- [RFC 2812: Internet Relay Chat: Client Protocol](https://www.rfc-editor.org/info/rfc2812/)
- [RFC 2813: Internet Relay Chat: Server Protocol](https://www.rfc-editor.org/info/rfc2813/)
