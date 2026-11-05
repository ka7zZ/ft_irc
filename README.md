# ft_irc

`ft_irc` is a C++98 IRC server. It accepts TCP clients, registers users, stores
channels, and handles a subset of IRC commands. The repository also contains
a separate dice bot.

This guide follows the implementation from program startup to an IRC reply.
For command-by-command behavior and numeric errors, see
[`helpfulMDs/irc_cmds.md`](./helpfulMDs/irc_cmds.md). For IRC terms, limits,
and protocol sources, see
[`helpfulMDs/server_info.md`](./helpfulMDs/server_info.md). The bot is
described in [`helpfulMDs/ft_irc_bonus.md`](./helpfulMDs/ft_irc_bonus.md).

## 1. IRC terms used by this project

IRC commands are text sent over TCP. The server receives a line, interprets
the command, checks the relevant user and channel state, then queues a reply
or forwards a message.

| Term | Meaning here |
|---|---|
| TCP connection | The network link between one client program and the server. |
| Socket / file descriptor | The operating-system endpoint for that link, identified in the program by an integer. |
| `Client` | The C++ object that stores one accepted client's socket, identity, registration flags, and buffers. |
| User | A client that has completed registration. |
| Nickname | The name used to address a user. |
| Channel | A group conversation such as `#42`. |
| Member | A `Client*` stored in a channel's member map, together with operator/voice flags. |
| Prefix | A sender identity such as `nick!user@host`, generated from client data. |
| Numeric reply | A three-digit server reply code, for example `001` welcome or `401` no such target. |

This repository implements one server. It does not implement links between
IRC servers.

Here, **connection** means the TCP link, not a C++ class. The project has no
`Connection` class; the accepted socket and its state are stored in `Client`.

## 2. Source map and classes

```text
main.cpp
  validates arguments and starts Server
      |
Server
  owns clients, channels, listener, poll descriptors, Dispatcher
      |
  recv -> Client input buffer -> IRCParser -> Message
                                           |
                                      Dispatcher
                                           |
                                     ICommand object
                                           |
                              state change / queued output
                                           |
                                         send
```

| Code | Responsibility |
|---|---|
| [`main.cpp`](./main.cpp) | Validates the port, installs signal handlers, constructs `Server`, and calls `run()`. |
| [`headers/Server.hpp`](./headers/Server.hpp), [`sources/Server/`](./sources/Server/) | Server state, listener, `poll()` loop, client I/O, and cleanup. |
| [`headers/Client.hpp`](./headers/Client.hpp), [`sources/Client.cpp`](./sources/Client.cpp) | Per-client identity, registration flags, and input/output buffers. |
| [`headers/Channel.hpp`](./headers/Channel.hpp), [`sources/Channel.cpp`](./sources/Channel.cpp) | Channel state, members, roles, modes, invitations, bans, key, limit, and topic. |
| [`headers/IRCParser.hpp`](./headers/IRCParser.hpp), [`sources/IRCParser.cpp`](./sources/IRCParser.cpp) | Parsed `Message`/`Parameter` types and parsing of a complete IRC line. |
| [`headers/Dispatcher.hpp`](./headers/Dispatcher.hpp), [`sources/Dispatcher.cpp`](./sources/Dispatcher.cpp) | Command lookup and shared registration/parameter checks. |
| [`headers/Commands/ICommand.hpp`](./headers/Commands/ICommand.hpp) | Common command interface. |
| [`sources/Commands/`](./sources/Commands/) | Implementations of individual command handlers. |
| [`headers/IRCError.hpp`](./headers/IRCError.hpp), [`sources/IRCError.cpp`](./sources/IRCError.cpp) | Numeric error code storage and error-line formatting. |
| [`bonus/`](./bonus/) | Separate bot executable and dice logic. |
| [`Makefile`](./Makefile) | C++98 flags, source lists, and build targets. |

### Main data types

- **`Server`** owns `std::map<int, Client> _clients` and
  `std::map<std::string, Channel> _channels`. It also owns the listener
  descriptor, poll list, and `Dispatcher`.
- **`Client`** stores the socket descriptor, nickname, username, hostname,
  real name, registration flags, and input/output strings.
- **`Channel`** stores its name, `Member` entries, invitation descriptors,
  ban records, key, limit, topic, and mode string. A `Member` contains a
  non-owning `Client*` and operator/voice flags.
- **`Message`** stores the parsed prefix, command, and ordered `Parameter`
  values. `Parameter::isTrailling` records whether the value followed `:`.
- **`Dispatcher`** stores command objects in a map keyed by lowercase command
  names.

When `Server::removeClient()` removes a client, it first removes that socket
from channel memberships and invitation lists, then erases the client and
closes the descriptor.

### Why commands implement `ICommand`

Each command provides:

```text
needsRegistration()  whether the caller must be registered
minParams()          minimum parameter count
execute(...)         command-specific checks and behavior
```

`Dispatcher::dispatch()` lowercases the parsed command, finds the command
object, applies the registration and minimum-parameter checks, then calls
`execute()`. Each handler owns its command-specific behavior. `Dispatcher`
deletes its command objects through `ICommand*`; the interface has a virtual
destructor for that purpose.

## 3. From socket bytes to command

The server creates a TCP listening socket, binds it to `INADDR_ANY` and the
requested port, listens, and sets it non-blocking. Accepted client sockets are
also made non-blocking. `poll()` reports which descriptors are ready.

TCP is a byte stream, not a command stream: one `recv()` can return part of a
line or several lines. For each client, the server:

1. appends bytes from `recv()` to `Client`'s input buffer;
2. extracts complete lines at `\n` and removes an optional `\r`;
3. creates an `IRCParser` for each non-empty line;
4. parses it into a `Message` and passes it to `Dispatcher`.

The message shape is:

```text
[:prefix ]COMMAND [middle parameters] [:trailing parameter]\r\n
```

For example:

```text
:alice!alice@127.0.0.1 PRIVMSG #42 :Hello there
```

The parser lowercases the command (`PRIVMSG` becomes `privmsg`), keeps
parameter values as supplied, and treats everything after a leading `:` as
one trailing parameter.

The server accepts complete lines up to 512 bytes including CRLF. An
unfinished input buffer exceeding 512 bytes is disconnected. For output,
`sendData()` erases the bytes successfully sent and leaves any unsent suffix
in the client's output buffer. `poll()` requests `POLLOUT` while output is
pending; `MSG_NOSIGNAL` prevents a broken peer from raising `SIGPIPE`.

## 4. Registration and dispatch

The implemented registration commands are:

```text
PASS <server-password>
NICK <nickname>
USER <username> <mode> <unused> :<real-name>
```

`Client` tracks password, nickname, user-information, and authentication
state with separate flags. `NICK` and `USER` can arrive in either order after
the password has been accepted. When the required fields are present, the
server marks the client registered and sends welcome replies `001` through
`004`.

The dispatcher currently registers:

```text
PASS NICK USER JOIN PART PING PONG PRIVMSG INVITE KICK LIST MODE TOPIC
```

`QUIT`, `NAMES`, `NOTICE`, and `CAP` are not registered.

## 5. Channels, modes, and messages

Creating a channel through `JOIN` adds the creator as an operator and voiced
member. The channel stores its members and channel state. A private `PRIVMSG`
looks up a nickname; a channel `PRIVMSG` is broadcast to channel members other
than the sender.

Channel and user mode behavior implemented in `ModeCommand` includes:

| Mode | Stored effect |
|---|---|
| `i` | Invite-only channel. |
| `m` | Moderated channel. |
| `n` | No-external-message flag. |
| `p`, `s` | Private and secret channel flags. |
| `t` | Operator-only topic changes. |
| `b` | Ban-list operations. |
| `k` | Channel key. |
| `l` | Channel member limit. |
| `o`, `v` | Member operator and voice status. |
| User `i` | Invisible user flag. |

The state is stored in `Channel` or `Client`; command handlers enforce
relevant rules. In the current implementation:

- `JOIN` checks invite-only, ban, key, and member-limit state.
- `PRIVMSG` blocks non-members, banned users, and users disallowed by
  moderated mode. The `+n` flag is stored but is not checked by this handler.
- `LIST` hides both `+p` and `+s` channels from non-members.
- Ban matching compares the full prefix exactly; it does not apply wildcard
  masks.
- `TOPIC`, `INVITE`, and `KICK` apply channel-membership/operator checks.

## 6. Replies and error handling

Success replies are built by handlers. Examples include:

```text
:ircserv 001 alice :Welcome ...
:ircserv 331 alice #42 :No topic is set
:ircserv 332 alice #42 :<topic>
:ircserv 353 alice = #42 :@operator alice
:ircserv 366 alice #42 :End of /NAMES list
```

Handlers throw `IRCError` for protocol errors. The dispatcher catches
uncaught errors of that type and queues the formatted line for the client.
Handlers that process multiple targets or modes may catch errors locally and
continue with the remaining items. The current formatter's wire layout and
text mismatches are documented in
[`helpfulMDs/irc_cmds.md`](./helpfulMDs/irc_cmds.md).

## 7. Build and libraries

The Makefile builds with:

```make
CXX = c++
CXXFLAGS = -g -Wall -Wextra -Werror -std=c++98
CPPFLAGS = -Iheaders
```

```sh
make
./ircserv 6667 password
make bonus
make clean
make fclean
make re
```

The server's `main.cpp` accepts ports from 1024 through 65535.

| Library/API | Use in this project |
|---|---|
| `std::string` | IRC text, names, prefixes, and buffers. |
| `std::vector` | Parsed parameters, poll descriptors, and ordered lists. |
| `std::map` | Client, channel, and command lookup. |
| `<algorithm>`, `<sstream>` | Container searches and text/number conversion. |
| `<iostream>`, `<exception>`, `<stdexcept>` | Console output and exception handling. |
| `<cstdlib>`, `<cctype>`, `<cstring>` | Conversion, character checks, and socket-structure initialization. |
| `<csignal>`, `<cerrno>` | Shutdown signals and system-call error checks. |
| POSIX sockets (`sys/socket.h`, `netinet/in.h`, `arpa/inet.h`) | TCP and IPv4 operations. |
| `poll.h`, `fcntl.h`, `unistd.h` | Readiness polling, non-blocking flags, and closing descriptors. |

## 8. Limits and implementation boundary

Explicit limits in the current code include:

- IRC input line: 512 bytes including CRLF.
- Pending unfinished input: more than 512 bytes causes disconnection.
- Server port argument: 1024–65535.
- Channel name accepted by `JOIN`: starts with `#`, 2–50 characters.
- Channel key accepted by `MODE +k`: at most 23 characters.
- Channel membership: optional maximum set by `MODE +l`.

This implementation has no server-to-server protocol or routing. The bonus
contains a dice bot; it does not contain DCC. The registered command list
above describes which client commands reach handlers.

## 9. References

- [RFC 2810 — IRC Architecture](https://www.rfc-editor.org/rfc/rfc2810)
- [RFC 2811 — IRC Channel Management](https://www.rfc-editor.org/rfc/rfc2811)
- [RFC 2812 — IRC Client Protocol](https://www.rfc-editor.org/rfc/rfc2812)
- [RFC 1459 — Original IRC Protocol](https://www.rfc-editor.org/rfc/rfc1459)
- [Modern IRC Documentation](https://modern.ircdocs.horse/)
