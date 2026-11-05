# IRC Server Information

This is a practical introduction to IRC, written around this repository. It
covers the main ideas, the commands this server knows, important limits,
common errors, and the sources used to check protocol details.

> **Scope note:** this project is a small, single-server IRC implementation. It
> accepts clients over TCP. It does not link multiple IRC servers together.
> Some protocol features described below are general IRC ideas, not features
> implemented here. See [README](../README.md) for the project architecture and
> [irc_cmds.md](./irc_cmds.md) for command behavior and errors.

## What is an IRC server?

An IRC server is a program that accepts IRC clients, reads their commands,
keeps track of registered users and channels, and sends replies or forwards
messages. A traditional IRC network can contain several connected servers;
this project runs one server and does not implement server-to-server links.

In this project, a client opens a TCP connection and sends text lines such as:

```text
PASS secret
NICK alice
USER alice 0 * :Alice Example
JOIN #42
PRIVMSG #42 :Hello everyone
```

The server reads those lines, parses them, selects a command handler, checks
the current state and permissions, then queues replies for the client.

## Concepts, one at a time

### Client and connection

A **client** is one IRC user connection. The server stores its nickname,
username, real name, socket descriptor, registration state, and input/output
buffers in a `Client` object. The socket descriptor is the operating system's
integer handle for that connection.

### TCP stream and line framing

TCP delivers an ordered stream of bytes. It does not promise that one
`recv()` call equals one IRC command. A command may arrive in pieces, or
several commands may arrive together. The server buffers received bytes,
extracts complete lines at `\n`, removes an optional `\r`, and only then
parses a command.

The IRC line limit is 512 bytes including the line ending. This server checks
that a complete line plus its `\r\n` is not longer than 512 bytes and also
limits an unfinished input buffer.

### IRC message shape

A common IRC message has this shape:

```text
[:prefix ]COMMAND [parameter ...] [:trailing text]\r\n
```

- The **prefix** identifies the sender. A user prefix often looks like
  `nickname!username@host`.
- The **command** is a name such as `JOIN`, `PRIVMSG`, or a numeric reply.
- Ordinary **parameters** end at a space.
- A **trailing parameter** starts with `:` and can contain spaces until the
  end of the line.

For example:

```text
:alice!alice@127.0.0.1 PRIVMSG #42 :Hello there
```

The parser stores the result in a `Message`: a prefix, a command, and an
ordered vector of parameters. In this repository, command names are
normalized to lowercase before dispatch. Parameter values keep their
original spelling.

### Registration

Registration is the process that makes a connection an IRC user. This server
uses:

- `PASS` to check the server password.
- `NICK` to set the nickname.
- `USER` to set the username and real name.

After the password, nickname, and user data are accepted, the server marks the
client registered and sends welcome replies. The order and exact checks are
described in [irc_cmds.md](./irc_cmds.md).

### Nickname, username, and real name

- A **nickname** is the short name other users use to address a client.
- A **username** is part of the client's full IRC prefix.
- A **real name** is descriptive text supplied during registration.

For example, `alice!alice@127.0.0.1` contains the nickname, username, and host.
The host in this project comes from the connecting client's IPv4 address.

### Channel, membership, and roles

A **channel** is a named group conversation. In this server, channel names
must start with `#`; for example, `#42`. A client joins a channel before
being treated as one of its members.

Each channel has member records. Some members can have operator or voice
status:

- A **channel operator** can manage channel settings and members.
- A **voiced member** can speak in a moderated channel.

The `Channel` object stores members, the topic, invitations, bans, a key, a
member limit, and channel modes.

### Channel modes

A **mode** is a flag or setting attached to a channel. This implementation
handles these channel flags:

| Mode | Meaning |
|---|---|
| `i` | Invite-only; joining requires an invitation. |
| `m` | Moderated; only operators and voiced members may speak. |
| `n` | Members-only messaging; outsiders cannot send to the channel. |
| `p` | Private channel; hidden from some channel listings. |
| `s` | Secret channel; hidden from some channel listings and lookups. |
| `t` | Only operators may change the topic. |
| `b` | Ban mask management. |
| `k` | Channel key (password). |
| `l` | Maximum member count. |
| `o` | Grant or remove channel operator status. |
| `v` | Grant or remove voice status. |

Supported flags and their checks can be traced in
[`ModeCommand.cpp`](../sources/Commands/ModeCommand.cpp) and
[`Channel.hpp`](../headers/Channel.hpp).

### Dispatcher and command interface

The **dispatcher** finds the handler for a command name. Each command class
implements the same `ICommand` interface: whether registration is needed,
the minimum parameter count, and how to execute the command.

This keeps routing separate from command rules. The dispatcher can apply
common registration and parameter checks; the command class handles the
specific channel or user behavior.

### Numeric replies

IRC servers often use three-digit numeric replies. For example, `001` is the
welcome reply, `331` means a channel has no topic, and `401` reports a missing
nickname or channel. A normal numeric reply has this general form:

```text
:server 001 nickname :Welcome text\r\n
```

The current project's success replies often follow that form. Its shared
`IRCError::format()` error output does **not** fully follow the standard
layout; see the error-format note in [irc_cmds.md](./irc_cmds.md).

### Event loop and `poll`

The server handles many sockets in one thread. `poll()` reports which sockets
are ready for reading or writing. The loop can then accept a new connection,
read available bytes, or send queued output without waiting on one idle client
to finish.

## Commands available in this repository

These are the handlers currently registered in
[`Dispatcher.cpp`](../sources/Dispatcher.cpp):

| Command | Short description |
|---|---|
| `PASS` | Check the connection password. |
| `NICK` | Set or change the nickname. |
| `USER` | Set username and real-name data; may finish registration. |
| `JOIN` | Create or join one or more channels. |
| `PING` | Reply with `PONG` to keep the connection alive. |
| `PONG` | Accept a keep-alive reply. |
| `PRIVMSG` | Send a message to a user or channel. |
| `INVITE` | Invite a user to a channel. |
| `KICK` | Remove a user from a channel. |
| `LIST` | List channels and their member counts/topics. |
| `MODE` | Read or change supported user/channel modes. |
| `TOPIC` | Read or set a channel topic. |

`PartCommand.cpp` exists and is compiled, but `PART` is not currently added to
the dispatcher's command map. Commands such as `QUIT`, `NAMES`, `NOTICE`, and
`CAP` are also not registered. See the complete table in
[irc_cmds.md](./irc_cmds.md).

## Important limits

Some limits come from IRC; others are choices in this implementation.

| Limit | Value or behavior | Where it comes from |
|---|---|---|
| IRC line size | 512 bytes including `\r\n`. | IRC protocol limit; enforced in `ServerHandleRawInput.cpp`. |
| Pending unfinished input | More than 512 bytes is disconnected. | Project policy in `ServerHandleRawInput.cpp`. |
| Server port | 1024–65535 accepted by `main.cpp`. | Project command-line validation; lower ports are rejected. |
| Channel name | Must start with `#`; accepted length is 2–50 characters. | Current `JoinCommand::validName()`. |
| Channel key | At most 23 characters; spaces, commas, and `:` are rejected. | Current `ModeCommand::keyOpt()`. |
| Channel member limit | Set by `+l`; joining is rejected when full. | Current `JoinCommand` and `ModeCommand`. |
| Nickname length | No explicit maximum length is checked by the current `NickCommand`. | Current implementation. |
| Message size | The parser accepts a trailing text field, but forwarding does not clamp the full output line to 512 bytes. | Current `PrivmsgCommand`; a long prefix/message can exceed the line limit. |
| Channel name character handling | The code rejects a few characters and limits length; it is not a full implementation of every IRC network's name rules. | Current `JoinCommand`. |

The current `main.cpp` accepts a numeric port and a password. It does not
accept a bind address; `ServerUtils.cpp` binds the listener to all local IPv4
interfaces with `INADDR_ANY`.

## Common errors

### IRC command errors

These are common numeric meanings used by the command handlers:

| Numeric | Meaning | Common cause |
|---|---|---|
| `401` | No such nickname/channel. | Private message or operator command targets an unknown user. |
| `403` | No such channel. | A channel name is invalid or does not exist. |
| `404` | Cannot send to channel. | Sender is not allowed to speak there. |
| `409` | No origin specified. | `PING` or `PONG` has no parameter. |
| `411` | No recipient given. | `PRIVMSG` has no usable recipient. |
| `412` | No text to send. | `PRIVMSG` has no message text. |
| `421` | Unknown command. | Command name has no registered handler. |
| `431` | No nickname given. | `NICK` has an explicitly empty nickname parameter. |
| `432` | Erroneous nickname. | Nickname contains a rejected character. |
| `433` | Nickname already in use. | Another client has the nickname. |
| `441` | User is not on channel. | `KICK` or a member MODE targets a non-member. |
| `442` | You're not on that channel. | Sender attempts a member-only operation. |
| `443` | User is already on channel. | An invitation targets a channel member. |
| `451` | You have not registered. | A command requires a registered client. |
| `461` | Not enough parameters. | Required command arguments are missing or invalid. |
| `462` | Already registered / unauthorized. | Repeating registration-only commands. |
| `464` | Password incorrect. | `PASS` does not match the server password. |
| `467` | Channel key already set. | Attempt to set an already-set channel key. |
| `471` | Channel is full. | `+l` member limit reached. |
| `472` | Unknown mode. | Unsupported channel mode letter. |
| `473` | Invite-only channel. | Joining a `+i` channel without an invite. |
| `474` | Banned from channel. | Joining a channel whose ban mask matches the user. |
| `475` | Bad channel key. | Missing or wrong key for a `+k` channel. |
| `476` | Bad channel mask. | Defined in the error formatter; not used by the current `JOIN` validator. |
| `482` | Channel operator privileges needed. | A non-operator tries an operator-only action. |
| `501` | Unknown user MODE flag. | Unsupported user mode. |
| `502` | Users do not match. | Trying to change another user's mode. |
| `525` | Invalid key. | Key is too long or contains a rejected character. |

The exact command cases and the current output formatting are documented in
[irc_cmds.md](./irc_cmds.md).

### Common programming and network errors

| Problem | What it means | What to check |
|---|---|---|
| Bind/listen failure | The server could not claim or listen on its port. | Port already in use, port permission, address setup. |
| Client connect failure | The client cannot reach the server. | Host, port, server process, and firewall. |
| `recv()` returns 0 | The peer closed the connection. | Clean up the client and remove its channel memberships. |
| `send()` is partial | Only part of the output was accepted by the socket. | Keep the unsent suffix in the output buffer. |
| `EINTR` from `poll()` | A signal interrupted the wait. | Retry if the server is still running. |
| Missing vector parameter | Code read `params[i]` when the parameter was absent. | Check `params.size()` before indexing. |
| Stale `Client*` | A channel still points to an erased client. | Remove membership before destroying the client. |

## Sources and what each one is useful for

| Source | Summary |
|---|---|
| [RFC 2812: Internet Relay Chat — Client Protocol](https://datatracker.ietf.org/doc/html/rfc2812) | The main reference for client-to-server messages, registration, commands, replies, and numeric meanings. This project implements only a subset. |
| [RFC 1459: Internet Relay Chat Protocol](https://datatracker.ietf.org/doc/html/rfc1459) | The original IRC protocol description. Useful for the protocol's basic model and historical command behavior; check newer references for updated details. |
| [Modern IRC Documentation](https://modern.ircdocs.horse/) | A readable, maintained guide to modern IRC message formats, commands, numerics, and practical differences between server implementations. |
| [CTCP documentation](https://modern.ircdocs.horse/ctcp.html) | Explains CTCP messages, which are IRC messages with control-character framing. CTCP is often used to negotiate features such as DCC; neither CTCP nor DCC is implemented by this project. |
| [This repository's command reference](./irc_cmds.md) | A traceable guide to commands and errors in this codebase, including implementation gaps and output-format differences. |
| [This repository's README](../README.md) | Explains the actual source tree, main classes, libraries, and build setup. |
