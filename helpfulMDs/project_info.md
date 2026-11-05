# ft_irc

This guide combines the IRC protocol background and server-design notes with
the repository-specific architecture, classes, libraries, and build
instructions. It moves from the basic IRC model to this project's
implementation, then to the event loop, commands, safety, and future work.

For exact current command behavior and error replies, see
[`helpfulMDs/irc_cmds.md`](./helpfulMDs/irc_cmds.md). For an IRC concept
overview and protocol references, see
[`helpfulMDs/server_info.md`](./helpfulMDs/server_info.md). The bonus bot and
the DCC extension idea are described in
[`helpfulMDs/ft_irc_bonus.md`](./helpfulMDs/ft_irc_bonus.md).

## Contents

1. [What IRC is](#1-what-irc-is)
2. [RFC map and vocabulary](#2-rfc-map-and-vocabulary)
3. [Repository structure and classes](#3-repository-structure-and-classes)
4. [Build, C++ libraries, and system APIs](#4-build-c-libraries-and-system-apis)
5. [The server's core state](#5-the-servers-core-state)
6. [Socket setup and the event loop](#6-socket-setup-and-the-event-loop)
7. [Byte stream framing and IRC messages](#7-byte-stream-framing-and-irc-messages)
8. [Registration as a state machine](#8-registration-as-a-state-machine)
9. [Command dispatch](#9-command-dispatch)
10. [Message delivery](#10-message-delivery)
11. [Channels and permissions](#11-channels-and-permissions)
12. [Connection loss, PING, and cleanup](#12-connection-loss-ping-and-cleanup)
13. [Security and resource limits](#13-security-and-resource-limits)
14. [Recommended implementation order](#14-recommended-implementation-order)
15. [Server-to-server extension](#15-server-to-server-extension)
16. [Minimal acceptance checklist](#16-minimal-acceptance-checklist)
17. [References](#17-references)

## 1. What IRC is

> IRC is a text protocol over a reliable byte stream, normally TCP. A client does not talk directly to another client: it sends commands to a server, and the server locates recipients, validates the request, changes state when necessary, and sends messages to the recipients.

The smallest useful topology is:

```text
client A -- TCP -- server -- TCP -- client B
						 |
						 +-- channel membership and user state
```

The full IRC architecture permits many servers connected as a spanning tree.
Every server keeps a view of the network and forwards messages along that tree.
RFC 2810 calls the server's responsibilities:

1. **Client locator:** map nicknames to users and their connections.
2. **Message relaying:** route private and channel messages.
3. **Channel hosting and management:** maintain membership, modes, and topics.

For a first implementation, treat the server as an event-driven state machine:

```text
bytes from socket
	-> input buffer
	-> complete IRC lines
	-> parsed message
	-> registration/permission checks
	-> state mutation
	-> replies and broadcasts
	-> output queues
	-> socket writes
```

## 2. RFC map and vocabulary

Read the documents in this order:

| Document | Use it for |
| --- | --- |
| [RFC 2810](https://www.rfc-editor.org/rfc/rfc2810) | architecture and message delivery |
| [RFC 2812](https://www.rfc-editor.org/rfc/rfc2812) | client-to-server commands and replies |
| [RFC 2811](https://www.rfc-editor.org/rfc/rfc2811) | channel lifetime, modes, and access rules |
| [RFC 2813](https://www.rfc-editor.org/rfc/rfc2813) | server-to-server protocol and implementation issues |
| [RFC 1459](https://www.rfc-editor.org/rfc/rfc1459) | original protocol, implementation notes, and historical context |

RFC 2810, 2811, 2812, and 2813 update the older RFC 1459 material. Use RFC
2812 as the primary client protocol reference and RFC 2813 only when adding
server links.

Important terms:

- **Connection:** one TCP socket and its private input/output buffers.
- **Client:** anything connected to a server that is not another server.
- **User:** a registered human client, identified locally and globally by a nick.
- **Server:** the IRC process and, in a network, a named peer.
- **Channel:** a named group of users with membership and modes.
- **Prefix:** the origin of a server-to-client message, such as
  `:nick!user@host`.
- **Numeric reply:** a three-digit response such as `001` or `ERR_NOSUCHNICK`.

## 3. Repository structure and classes

### Why commands use an interface

Each command implements [`ICommand`](./headers/Commands/ICommand.hpp), which
defines whether registration is required, the minimum parameter count, and
how the command executes.

This gives the server a consistent way to handle different commands:

- The dispatcher can look up a command and perform shared checks without
  containing every command's business rules.
- Each command keeps its own behavior and permission checks in its handler.
- A new command can follow the same interface as existing commands.

The dispatcher owns the command objects and releases them when it is
destroyed. The virtual destructor in `ICommand` makes deletion through an
`ICommand*` safe.

### Repository map

| Path | What is here |
|---|---|
| [`main.cpp`](./main.cpp) | Validates the server port, installs signal handlers, and starts the server. |
| [`headers/`](./headers/) | Declarations for the server, clients, channels, parser, errors, dispatcher, and commands. |
| [`headers/Commands/`](./headers/Commands/) | `ICommand` and one header for each command class. |
| [`sources/Server/`](./sources/Server/) | Server lifecycle, sockets, polling, input/output handling, and client/channel utilities. |
| [`sources/Commands/`](./sources/Commands/) | Command implementations. |
| [`sources/Client.cpp`](./sources/Client.cpp) | Per-client identity, registration state, and buffers. |
| [`sources/Channel.cpp`](./sources/Channel.cpp) | Channel members, roles, modes, invitations, bans, key, limit, and topic. |
| [`sources/IRCParser.cpp`](./sources/IRCParser.cpp) | Parses a complete IRC line into a `Message`. |
| [`sources/Dispatcher.cpp`](./sources/Dispatcher.cpp) | Maps command names to handlers and performs common checks. |
| [`sources/IRCError.cpp`](./sources/IRCError.cpp) | Formats numeric-style error replies. |
| [`bonus/`](./bonus/) | Separate dice-bot executable. DCC is not implemented. |
| [`tester/`](./tester/) | Python test script. |
| [`Makefile`](./Makefile) | C++98 build settings, source lists, and build targets. |

### Main classes

| Class | Responsibility in this codebase |
|---|---|
| `Server` | Owns the listener, connected clients, channels, poll descriptors, and dispatcher. Coordinates network events and cleanup. |
| `Client` | Stores one connection's socket, identity, registration flags, input/output buffers, and user flags. `Server` owns client objects. |
| `Channel` | Stores channel state and member roles. Member records contain `Client*` references, so client removal must also remove channel membership. |
| `IRCParser` | Parses one complete line into a prefix, a normalized command, and ordered parameters. |
| `Message` / `Parameter` | Hold the parsed command data; `Parameter` records whether the value was trailing text. |
| `Dispatcher` | Looks up a command object, checks registration and minimum parameter count, then calls it. |
| `ICommand` | Defines the shared command-handler contract. |
| `IRCError` | Carries an error code and is used by the dispatcher to build an error line. Current formatting caveats are in [irc_cmds.md](./helpfulMDs/irc_cmds.md). |

The Makefile builds these command classes:
`InviteCommand`, `JoinCommand`, `KickCommand`, `ListCommand`, `ModeCommand`,
`NickCommand`, `PartCommand`, `PassCommand`, `PingCommand`, `PongCommand`,
`PrivmsgCommand`, `TopicCommand`, and `UserCommand`.

Being compiled does not always mean a command is available to clients:
`PartCommand.cpp` is compiled but `part` is not registered in the dispatcher.
See the current behavior in
[`helpfulMDs/irc_cmds.md`](./helpfulMDs/irc_cmds.md).

## 4. Build, C++ libraries, and system APIs

The project is compiled as C++98. The headers below are used by the server
and/or the bot.

### C++ standard library

| Header/type | Used for here | Why it is useful in general |
|---|---|---|
| `<string>` / `std::string` | Commands, names, prefixes, topics, and buffers. | Owns and manipulates text. |
| `<vector>` / `std::vector` | Parsed parameters, poll entries, and ordered lists. | Stores a resizable sequence. |
| `<map>` / `std::map` | Client, channel, and command lookup. | Associates keys with values. |
| `<algorithm>` | Searching collections such as invitations. | Provides reusable operations over iterator ranges. |
| `<sstream>` / `std::stringstream` | Parsing numbers and formatting replies. | Reads or writes typed values as text. |
| `<iostream>` | Startup, status, and error output. | Reads/writes standard streams. |
| `<cstddef>` | `size_t` and container sizes. | Provides common size-related types. |
| `<exception>` | Base class for `IRCError`. | Defines the standard exception interface. |
| `<stdexcept>` | Runtime failures. | Provides standard exception types. |
| `<cstdlib>` | `strtol`, `atoi`, and exit status values. | C conversion and process utilities. |
| `<cctype>` | `tolower` and `isdigit`. | Character classification and conversion. |
| `<cstring>` | Zeroes socket address structures. | C memory utilities for C-style data. |
| `<ctime>` | Ban timestamps and the dice bot's random seed. | Time values and clock functions. |
| `<csignal>` | Signal handling and `sig_atomic_t`. | Lets the program react to operating-system signals. |
| `<cerrno>` | `errno` and `EINTR` checks. | Describes why some system calls failed. |

### POSIX networking and polling

| Header/API | Used for here | Why it is useful in general |
|---|---|---|
| `<sys/socket.h>`: `socket`, `setsockopt`, `bind`, `listen`, `accept`, `recv`, `send` | Creating and operating TCP sockets. | The POSIX socket interface. |
| `<netinet/in.h>`: `sockaddr_in`, `htons`, `htonl` | IPv4 address structures and network byte order. | Describes IPv4 socket endpoints. |
| `<arpa/inet.h>`: `inet_ntoa`, `ntohs`, `inet_addr` | Converting IPv4 addresses and ports. | Makes network addresses usable in C programs. |
| `<poll.h>`: `poll`, `pollfd`, `POLLIN`, `POLLOUT` | Waiting for listener/client sockets to become ready. | Monitors several file descriptors in one event loop. |
| `<fcntl.h>`: `fcntl`, `F_SETFL`, `O_NONBLOCK` | Setting sockets to nonblocking mode. | Changes file-descriptor behavior. |
| `<unistd.h>`: `close` | Closing sockets. | Releases operating-system file descriptors. |

These are operating-system APIs rather than C++ classes. They use file
descriptors and status values; many report failures with `-1` and `errno`.

### Build and run

The Makefile uses:

```make
CXX = c++
CXXFLAGS = -g -Wall -Wextra -Werror -std=c++98
CPPFLAGS = -Iheaders
```

- `-std=c++98` selects the required language standard.
- `-Wall -Wextra` enable common compiler warnings.
- `-Werror` makes warnings fail the build.
- `-Iheaders` adds the project header directory to the include search path.
- `-MMD -MP` creates dependency files so header changes trigger rebuilds.

Common commands:

```sh
make
./ircserv 6667 password
make bonus
make clean
make fclean
make re
```

The server takes a port and a password. The current `main.cpp` accepts ports
from 1024 through 65535. `make bonus` builds the separate `bot` executable;
its behavior is described in
[`helpfulMDs/ft_irc_bonus.md`](./helpfulMDs/ft_irc_bonus.md).

## 5. The server's core state

The sketch correctly identifies `server`, `sockets`, `clients`, `channels`,
`nick`, `state`, `permissions`, `admin`, and message parsing as the central
pieces. The important design decision is to make ownership and indexes explicit.

### 5.1 Connection state

Keep a connection object for every accepted socket:

```text
Connection {
	fd
	kind: listener | client
	registration: unregistered | nick_seen | registered
	input_buffer: bytes/string
	output_queue: byte queue
	nick: optional string
	username: optional string
	realname: optional string
	user_modes
	joined_channels: set of Channel references
	last_read_time
	last_write_time
	last_ping_time
	closing: boolean
}
```

`registration` is not the same thing as socket connectivity. A connected
socket is initially an unregistered client and can only use registration
commands. Do not insert the client into the global nick index until both NICK
and USER have succeeded.

### 5.2 Server indexes

Use indexes that match the operations the protocol performs:

```text
connections: fd -> Connection
users_by_nick: normalized_nick -> User
channels_by_name: normalized_channel -> Channel
```

The `User` object can either be the registered part of `Connection` or a
separate object containing the connection pointer. The important invariant is:

```text
every registered user has exactly one connection and one unique nickname
```

### 5.3 Channel state

```text
Channel {
	name
	topic: optional string
	modes: invite_only, moderated, no_external, topic_operator_only,
		   private, secret
	key: optional string
	user_limit: optional integer
	bans: set of masks
	invited_nicks: set of normalized nicknames
	members: map User -> member_modes(operator, voice)
}
```

For the common `ft_irc` scope, implement the channel modes required by the
project specification first. RFC 2811 contains additional historical modes;
do not silently claim to support a mode you do not implement.

## 6. Socket setup and the event loop

The server must never block on one client. One slow or idle client must not
stop all other clients from being served.

### 6.1 Listener setup

1. Create a TCP socket with `socket`.
2. Set `SO_REUSEADDR` if appropriate for restart behavior.
3. Bind to the configured address and port.
4. Mark it non-blocking.
5. Call `listen`.
6. Add the listener to `poll`/`select`/`epoll` and watch for readability.

### 6.2 Accepting clients

When the listener is readable, accept in a loop until the non-blocking socket
reports that no more connections are ready. For every accepted socket:

1. Set it non-blocking.
2. Create a `Connection` with `registration = unregistered`.
3. Add it to the poll set and `connections`.
4. Do not read as though one `read` equals one IRC message.

### 6.3 One event-loop iteration

```text
while not stopping:
	update timers and poll interest
	ready = poll(all sockets, timeout)

	if listener is readable:
		accept all pending clients

	for each ready connection:
		if readable:
			read_available_bytes(connection)
			extract_complete_lines(connection)
			process_each_line(connection)

		if writable:
			flush_output_queue(connection)

		if connection was closed during processing:
			remove it safely after iteration bookkeeping

	run_ping_and_timeout_checks()
```

The exact order can vary, but it must be deliberate. A useful policy is to
process reads first, queue all resulting output, and then flush writes. Never
call a blocking `send` while handling a command.

## 7. Byte stream framing and IRC messages

TCP is a stream. A single `read` can contain half a command, one command, or
many commands. The input buffer in the sketch is therefore essential.

### 7.1 Framing rules

- IRC messages end with `CR LF` (`"\r\n"`).
- A complete message, including its terminator, is at most 512 bytes.
- The command and parameters may therefore use at most 510 bytes.
- Empty lines may be ignored.
- NUL, CR, and LF must not appear inside a parsed parameter.

Algorithm:

```text
append every byte returned by read() to input_buffer

while input_buffer contains CR LF:
	line = bytes through the first CR LF
	remove that line from input_buffer
	if line length > 512:
		reject or close according to policy
	parse line
```

If the buffer grows beyond the maximum possible incomplete message without a
terminator, close the connection or reject it. Otherwise an attacker can keep
memory growing forever by sending bytes without `CR LF`.

### 7.2 Message grammar

Conceptually, RFC 2812 defines:

```text
message  = [":" prefix SPACE] command [params] CR LF
params   = zero or more middle parameters, then an optional trailing parameter
middle   = no spaces, and does not start with ":"
trailing = may contain spaces and is introduced by ":"
```

Examples:

```text
NICK alice\r\n
USER alice 0 * :Alice Example\r\n
PRIVMSG bob :hello with spaces\r\n
:alice!alice@localhost JOIN #chat\r\n
```

Parsing rule: split off an optional prefix only when the line begins with `:`;
then split the command from the rest; split parameters on spaces until the
first parameter beginning with `:`, which consumes the remainder as one
trailing parameter. Preserve empty trailing parameters where the command allows
them.

The parser should return data, not perform business logic:

```text
Message {
	optional prefix
	command: normalized command name
	params: ordered list of strings
}
```

That keeps malformed-input handling separate from command permissions and state
mutation.

## 8. Registration as a state machine

For a user connection, the normal sequence is:

```text
TCP accept
  -> optional PASS
  -> NICK
  -> USER
  -> registered
  -> welcome numerics and MOTD
```

RFC 2812 requires successful registration to produce the welcome sequence,
normally `001 RPL_WELCOME`, `002 RPL_YOURHOST`, `003 RPL_CREATED`, and
`004 RPL_MYINFO`. The server may then send `LUSERS` information and MOTD.

Rules to enforce:

- `NICK` must be syntactically valid and not already in use.
- A nickname is reserved only after it passes the uniqueness check and is added
  atomically to `users_by_nick`.
- `USER` requires the username, mode, unused field, and trailing real name.
- `PASS`, if required, must arrive before registration completes.
- A registered client cannot send NICK/USER/PASS again as a new registration.
- Commands other than registration commands must receive `451 ERR_NOTREGISTERED`
  while the connection is unregistered.

The registration transition should be one function that validates all required
fields, creates the user, inserts indexes, and emits the welcome messages. This
avoids a half-registered user becoming visible to channel or private-message
code.

## 9. Command dispatch

After parsing, normalize the command name for lookup, but retain the original
parameters. Dispatch through a table rather than a long accidental chain:

```text
handlers = {
	"PASS": handle_pass,
	"NICK": handle_nick,
	"USER": handle_user,
	"PING": handle_ping,
	"PONG": handle_pong,
	"JOIN": handle_join,
	"PART": handle_part,
	"PRIVMSG": handle_privmsg,
	"NOTICE": handle_notice,
	"MODE": handle_mode,
	"TOPIC": handle_topic,
	"KICK": handle_kick,
	"INVITE": handle_invite,
	"NAMES": handle_names,
	"QUIT": handle_quit
}
```

Each handler should follow the same shape:

1. Check registration state.
2. Check parameter count and syntax.
3. Resolve nick/channel targets.
4. Check permissions and channel rules.
5. Mutate state.
6. Build the canonical IRC message.
7. Deliver locally and/or queue a reply.

Use numeric replies consistently. Examples include `421` unknown command,
`461` missing parameters, `433` nickname in use, `401` no such nick/channel,
`442` not on channel, `482` not channel operator, and `481` not an IRC operator.

## 10. Message delivery

There are three useful local delivery paths.

### 10.1 Private message

For `PRIVMSG nick :text`:

1. Validate recipient and text.
2. Look up the recipient in `users_by_nick`.
3. Create a message with the sender prefix.
4. Queue it on the recipient connection.
5. If the nickname is absent, send `401` to the sender.

### 10.2 Channel message

For `PRIVMSG #channel :text`:

1. Resolve the channel.
2. Confirm the sender is a member unless `+n` is disabled.
3. If moderated, permit only operators and voiced members.
4. Apply ban and other project-required checks.
5. Send one copy to every member except the sender, unless the protocol/client
   behavior requires an echo.

Use a recipient set when a command accepts multiple targets so one connection
does not receive duplicate copies through overlapping targets.

### 10.3 NOTICE

NOTICE is similar to PRIVMSG, but the server must not generate automatic error
replies to a NOTICE. This prevents automated clients from entering reply loops.

### 10.4 Output queues

`send()` may write only part of a message or return `EAGAIN`. Therefore every
connection needs an output queue containing unsent bytes:

```text
queue(message)
enable POLLOUT

on POLLOUT:
	write as many queued bytes as possible
	remove successfully written bytes
	disable POLLOUT when the queue is empty
```

Enforce a maximum queue size. A client that cannot drain its queue must not
cause unbounded memory growth or block the whole server.

## 11. Channels and permissions

### 11.1 Channel lifecycle

For ordinary `#`, `&`, and `+` channels, creation is implicit on the first
successful JOIN and deletion occurs when the last member leaves. The first user
normally receives channel-operator status, except where the channel type or
project rules say otherwise.

Channel names are case-insensitive and cannot contain spaces, comma, control-G,
or CR/LF. RFC 2812 allows prefixes `#`, `&`, `+`, and `!`, with a maximum length
of 50 characters.

### 11.2 JOIN decision

For each requested channel, evaluate in a stable order:

1. valid channel name;
2. user channel-count limit;
3. existing membership;
4. ban masks and exceptions;
5. invite-only mode and invitation;
6. channel key;
7. user limit;
8. add membership and operator status if creating;
9. broadcast JOIN to members;
10. send topic and NAMES replies to the joining client.

The check and insertion should happen as one logical operation. Do not announce a
JOIN before the member has actually been inserted into the channel.

### 11.3 PART, KICK, and QUIT

All three remove membership, but their authority and notification differ:

- `PART`: the user voluntarily leaves one or more channels.
- `KICK`: a channel operator removes another user.
- `QUIT`: the user connection ends; remove the user from every channel and then
  notify the remaining members.

After removal, delete an ordinary channel if it has no members. Every removal
path must update both sides of the relationship: the channel's member map and
the user's joined-channel set.

### 11.4 MODE and TOPIC

`MODE` has two domains: user modes and channel modes. Parse the complete mode
change before applying it, because modes such as `+k`, `+l`, `+o`, `+v`, and `+b`
consume parameters. Check the sender's authority for each change and send one
canonical MODE notification after the mutation.

`TOPIC #channel` reads the topic. `TOPIC #channel :new text` changes it, subject
to membership and `+t` rules. An empty trailing topic clears it.

## 12. Connection loss, PING, and cleanup

A connection can end because the client sends QUIT, `read` returns zero, a
socket error occurs, a timeout expires, or the server rejects malformed input.
All paths must converge on one cleanup routine:

```text
close_connection(connection, reason):
	if already closing: return
	mark closing
	if registered:
		broadcast QUIT to relevant channel members
		remove user from every channel
		erase users_by_nick entry
	remove fd from poll set and connections
	close(fd)
	release buffers and connection memory
```

Do not erase a connection while iterating a container that still expects to
visit it. Mark it closed, finish the current dispatch safely, then remove it
using an iterator-safe strategy.

For liveness, track activity per connection. Send `PING` after an idle period;
expect `PONG` before the deadline; close and clean up an unresponsive socket.
Timers must not block the event loop.

## 13. Security and resource limits

The sketch's `limits` node should become explicit policy, not a last-minute
feature. At minimum define:

- maximum IRC line length and input-buffer length;
- maximum output queue size;
- maximum clients and channels;
- maximum channels per user;
- maximum channel name, nick, username, and topic lengths;
- maximum members, ban masks, and mode parameters per channel;
- nickname and channel validation rules;
- password handling for the server and OPER command;
- flood control, at least a simple per-client command rate limit.

Never trust a client-provided hostname, server name, prefix, or privileges.
Client prefixes are not authority. A server constructs prefixes from its own
state. A client cannot grant itself operator or channel-operator status by
sending `MODE +o`.

Passwords in the historical RFCs are sent in clear text. For a real deployment,
use TLS or keep the server on a trusted network; do not treat plain PASS as
secure authentication.

## 14. Recommended implementation order

Build vertical slices that can be tested after each step:

1. Listener, non-blocking sockets, poll loop, accept, and clean shutdown.
2. Per-connection input buffering and CRLF framing.
3. Parser tests for prefixes, middle parameters, trailing parameters, empty
   trailing parameters, fragmented input, and multiple commands per read.
4. Registration with PASS/NICK/USER and welcome numerics.
5. PING/PONG and QUIT.
6. User index and private PRIVMSG/NOTICE.
7. Channel creation, JOIN, PART, and channel PRIVMSG.
8. TOPIC, NAMES, MODE, INVITE, and KICK.
9. Output queues, backpressure, limits, and flood control.
10. Queries and optional commands such as WHOIS, WHO, LIST, and AWAY.
11. Only then consider server-to-server links.

At every stage, test with two or more clients and with a raw tool such as
`nc` or `telnet`. Send commands in fragments and send several commands in one
write; these expose framing bugs that an interactive client can hide.

## 15. Server-to-server extension

RFC 2813 adds a second protocol role and a distributed database. A server link
is not just another client socket:

1. Authenticate with PASS, then register with SERVER.
2. Reject duplicate server routes because the topology must remain acyclic.
3. Exchange known servers first.
4. Exchange users and services.
5. Exchange channel membership and modes.
6. Route messages using prefixes and the next hop toward the target.
7. On link loss, propagate SQUIT for servers behind the link and QUIT for
   affected users.

Server messages received from a peer must have their prefix validated. An
unknown or incorrectly routed prefix is a protocol-integrity failure, not an
ordinary client error. This is why server links should be added only after the
single-server state model is coherent.

## 16. Minimal acceptance checklist

- Two clients can connect without either blocking the other.
- A fragmented command is reconstructed correctly.
- Multiple commands in one TCP read are all processed.
- An overlong line cannot grow memory without bound.
- Duplicate nicknames are rejected atomically.
- Commands before registration receive `451`.
- Successful registration emits `001` through `004`.
- Private messages reach exactly one target.
- Channel messages reach the right members without duplicate delivery.
- JOIN enforces key, invite, ban, limit, and membership rules.
- PART, KICK, QUIT, EOF, and socket errors remove all references.
- A slow client cannot block writes to other clients.
- PING/PONG detects dead or unresponsive connections.
- Every emitted line ends in `\r\n` and is at most 512 bytes.

## 17. References

- [RFC 1459: Internet Relay Chat Protocol](https://www.rfc-editor.org/info/rfc1459/)
- [RFC 2810: Internet Relay Chat: Architecture](https://www.rfc-editor.org/info/rfc2810/)
- [RFC 2811: Internet Relay Chat: Channel Management](https://www.rfc-editor.org/info/rfc2811/)
- [RFC 2812: Internet Relay Chat: Client Protocol](https://www.rfc-editor.org/info/rfc2812/)
- [RFC 2813: Internet Relay Chat: Server Protocol](https://www.rfc-editor.org/info/rfc2813/)