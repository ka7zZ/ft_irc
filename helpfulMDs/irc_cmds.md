# IRC Commands and Replies

This guide explains the commands currently registered by this server, their
main inputs, behavior, and the numeric errors raised by the code. It separates
what this repository does from what an IRC reference generally describes.

> **Important:** an error number can have the correct meaning while this
> project still formats its wire reply differently from standard IRC. The
> current `IRCError::format()` output is explained under
> [Error reply format](#error-reply-format-in-this-project).

## Message format and command flow

An IRC command line commonly looks like:

```text
[:prefix ]COMMAND [parameter ...] [:trailing text]\r\n
```

Example:

```text
PRIVMSG #42 :Hello there
```

The trailing parameter is `Hello there`; the leading colon marks it as one
parameter even though it contains a space.

The server receives bytes, rebuilds complete lines, parses each line into a
`Message`, lowercases the command name, and calls the matching `ICommand`
object through `Dispatcher`. The dispatcher checks whether registration is
required and whether the command has its minimum number of parameters. The
command then applies its own checks and changes server state or sends replies.

Registered commands are listed in
[`Dispatcher.cpp`](../sources/Dispatcher.cpp). `PART` has a source file and is
compiled, but is not registered, so clients currently receive `421` for it.

## Registration commands

### `PASS <password>`

Checks the supplied password against the server password.

| Input/case | Current behavior |
|---|---|
| Correct password | Sets the client's pass state and sends the plain text `Password approved` line. |
| Wrong password | Marks the client for closing and raises `464`. |
| No parameter | Dispatcher raises `461`. |
| Password already accepted | Raises `462`. |
| Empty password parameter | Raises `461`. |

The plain `Password approved` line is not a standard IRC numeric reply.
`PASS` is allowed before registration. See
[`PassCommand.cpp`](../sources/Commands/PassCommand.cpp).

### `NICK <nickname>`

Sets the client's nickname. A successful nickname change after registration
is broadcast to users who share a channel with the client. During
registration, `NICK` can complete registration if a valid `PASS` and `USER`
have already been received.

| Error | Trigger |
|---|---|
| `451` | No valid `PASS` has been accepted yet. |
| `431` | Nickname is empty. |
| `432` | Nickname starts with a rejected character or contains a rejected character. |
| `433` | Another client already uses the nickname. |

The current check rejects a leading digit, `-`, `#`, `&`, `$`, or `:`, and
rejects spaces, comma, `.`, `!`, `*`, or `@` anywhere. It does not enforce a
maximum nickname length. Lookup for a duplicate nickname is normalized, but
the same-name no-op check is exact-case.

See [`NickCommand.cpp`](../sources/Commands/NickCommand.cpp).

### `USER <username> <mode> <unused> <realname>`

Stores the username from parameter 1 and the real name from parameter 4.
There must be at least four parameters, and a non-empty username and real name
are required.

| Error | Trigger |
|---|---|
| `461` | Fewer than four parameters, empty username, or empty real name. |
| `451` | No valid `PASS` has been accepted. |
| `462` | Client is already registered. |

If `NICK` was already accepted, `USER` completes registration. If `USER`
arrives first, a later `NICK` completes it. Successful registration sends
`001`, `002`, `003`, and `004`.

See [`UserCommand.cpp`](../sources/Commands/UserCommand.cpp).

## Channel commands

### `JOIN <#channel>[,#channel...] [<key>[,<key>...]]`

Creates or joins one or more channels. The first client creating a channel
becomes its operator. On success, the server broadcasts `JOIN` and sends the
joiner topic and member-list numerics.

Current channel validation requires a name that:

- starts with `#`;
- is 2 to 50 characters long;
- contains none of the rejected characters in `JoinCommand::validName()`.

| Error | Trigger |
|---|---|
| `461` | No channel parameter. |
| `403` | Invalid channel name. |
| `471` | Channel member limit (`+l`) is already reached. |
| `473` | Channel is invite-only (`+i`) and the client is not invited. |
| `474` | The client's prefix matches a channel ban. |
| `475` | Channel has a key (`+k`) and the supplied key is missing or wrong. |

Comma-separated empty entries are skipped by the current helper. A client
already in the channel is ignored. A key is used only when the channel has a
key set.

Success replies include `331` (no topic) or `332` (topic), `353` (names), and
`366` (end of names). See
[`JoinCommand.cpp`](../sources/Commands/JoinCommand.cpp).

### `PART <#channel>[,#channel...] [:reason]` — not currently dispatchable

`PartCommand.cpp` implements leaving a channel and can raise `461`, `403`, or
`442`, but `part` is missing from the command map in `Dispatcher.cpp`. As a
result, an IRC client currently gets `421` Unknown command instead of running
this handler.

See [`PartCommand.cpp`](../sources/Commands/PartCommand.cpp).

### `LIST [<#channel>[,#channel...]]`

Sends `321` (list start), one `322` per visible matching channel, and `323`
(list end). With no channel parameter, it lists every channel visible to the
requesting user. Secret and private channels are hidden from non-members.

The handler does not raise command-specific IRC errors. The dispatcher can
still raise `451` before execution.

See [`ListCommand.cpp`](../sources/Commands/ListCommand.cpp).

### `TOPIC <#channel> [:<topic>]`

With only a channel parameter, reads its topic: `331` if no topic is set, or
`332` if a topic exists. With a second parameter, sets the topic and
broadcasts the change to channel members. An explicitly empty trailing
parameter clears the topic.

| Error | Trigger |
|---|---|
| `461` | No channel parameter. |
| `403` | Channel does not exist. |
| `442` | Requesting user is not a channel member. |
| `482` | The channel has `+t` and the requester is not an operator (setting only). |

See [`TopicCommand.cpp`](../sources/Commands/TopicCommand.cpp).

### `KICK <#channel>[,...] <nickname>[,...] [:reason]`

An operator removes one or more users from a channel. The server broadcasts
the `KICK` line, removes the member, and removes the channel if it becomes
empty. The default reason is the operator's nickname.

| Error | Trigger |
|---|---|
| `461` | Missing/empty channel or nickname list, or incompatible channel and nickname list lengths. |
| `403` | Channel does not exist. |
| `442` | Sender is not a member of the channel. |
| `482` | Sender is not a channel operator. |
| `401` | Target nickname does not exist. |
| `441` | Target exists but is not a member of the channel. |

Multiple channels must either contain one channel (applied to each nickname)
or have the same number of entries as the nickname list. See
[`KickCommand.cpp`](../sources/Commands/KickCommand.cpp).

### `INVITE <nickname> <#channel>`

Adds a target client to the channel's invitation list, sends `341` to the
inviter, and sends an `INVITE` message to the target.

| Error | Trigger |
|---|---|
| `461` | Fewer than two parameters. |
| `403` | Channel does not exist. |
| `442` | Inviter is not a channel member. |
| `482` | Inviter is not a channel operator. |
| `401` | Target nickname does not exist. |
| `443` | Target is already a channel member. |

The current handler requires an operator for every invite, even when the
channel is not invite-only. See
[`InviteCommand.cpp`](../sources/Commands/InviteCommand.cpp).

## Messaging and connection commands

### `PRIVMSG <target>[,<target>...] <text>`

Sends a private message to a nickname or a channel message to its members
except the sender. Multiple comma-separated recipients are processed one at a
time.

| Error | Trigger |
|---|---|
| `451` | Sender is not registered. |
| `461` | No parameter at all (caught by the dispatcher). |
| `411` | Recipient is missing or empty. |
| `412` | Text is missing or empty. |
| `401` | Nickname target does not exist. |
| `403` | Channel does not exist, or is secret and hidden from the sender. |
| `404` | Sender is not a member, is banned, or cannot speak due to channel modes. |

For a single parameter, the implementation distinguishes its type:
`PRIVMSG target` raises `411` (there is no message text), while
`PRIVMSG :text` raises `412` (there is no recipient).

The implementation forwards the message but does not clamp the total output
line to the IRC 512-byte limit. See
[`PrivmsgCommand.cpp`](../sources/Commands/PrivmsgCommand.cpp).

### `PING <token>`

Returns `PONG` with the supplied token. It requires a registered client in
this implementation.

| Error | Trigger |
|---|---|
| `451` | Client is not registered. |
| `409` | No token was supplied. |

See [`PingCommand.cpp`](../sources/Commands/PingCommand.cpp).

### `PONG <token>`

Accepts a PONG and otherwise does nothing. It requires registration here.

| Error | Trigger |
|---|---|
| `451` | Client is not registered. |
| `409` | No token was supplied. |

See [`PongCommand.cpp`](../sources/Commands/PongCommand.cpp).

## MODE command

### `MODE <target> [<mode string> [parameters...]]`

The first parameter selects a channel (starts with `#`) or a user.

For channels, the implementation supports:

| Flag | Parameter | Effect |
|---|---|---|
| `i` | None | Invite-only. |
| `m` | None | Moderated channel. |
| `n` | None | Block channel messages from non-members. |
| `p` | None | Private channel. |
| `s` | None | Secret channel. |
| `t` | None | Operator-only topic changes. |
| `b` | Existing user's full prefix when adding/removing; no parameter lists bans. | Add/remove an exact-prefix ban or request the ban list. |
| `k` | Key when adding. | Set or remove a channel key. |
| `l` | Positive integer when adding. | Set or remove the channel member limit. |
| `o` | Nickname. | Grant or remove operator status. |
| `v` | Nickname. | Grant or remove voice status. |

For users, only `i` (invisible) is supported. A client can only change its
own user mode.

| Error/reply | Trigger |
|---|---|
| `461` | Missing target/mode parameters, or a required mode argument is missing or invalid. |
| `403` | Channel target does not exist. |
| `472` | Unknown channel mode or malformed mode string. |
| `482` | Requester is not a channel operator for a channel-mode change. |
| `467` | Attempt to set `+k` when a key is already set. |
| `525` | New key is too long or contains a rejected character. |
| `401` | `+o`/`+v` target nickname does not exist. |
| `441` | `+o`/`+v` target is not a channel member. |
| `501` | Unknown user mode. |
| `502` | Attempt to change another user's mode. |
| `221` | User-mode information reply. |
| `324` | Channel-mode information reply. |
| `367`, `368` | Ban-list entries and end of ban list. |

Mode letters are processed in order, and parameter-taking letters consume
parameters in that order. The current code catches some errors inside the
mode loop and continues, so a single command can produce an error and still
apply later flags. Also, the `+b` list path is handled before the operator
check; this means the current code does not consistently enforce operator
access when viewing the ban list. Ban matching is exact-prefix matching:
wildcard masks are not applied, and adding a ban first looks up a connected
client with the supplied full prefix.

See [`ModeCommand.cpp`](../sources/Commands/ModeCommand.cpp) and the
`Channel` data in [`Channel.hpp`](../headers/Channel.hpp).

## Dispatcher-level errors

Before calling a command handler, `Dispatcher::dispatch()` can raise:

| Numeric | Trigger |
|---|---|
| `421` | Command name is not in the command map. `PART`, `QUIT`, `CAP`, `NAMES`, and `NOTICE` currently reach this path. |
| `451` | A registered-only command is sent by a client whose auth state is false. |
| `461` | A handler's declared minimum parameter count is not met. |

For commands such as `NICK` and `USER`, the dispatcher allows the initial call
before registration, and the command itself applies additional registration
checks.

## Numeric error meanings emitted by `IRCError`

The formatter in [`IRCError.cpp`](../sources/IRCError.cpp) has text cases for:

| Numeric | Name | Meaning in the project |
|---|---|---|
| `401` | `ERR_NOSUCHNICK` | No such nick/channel. |
| `403` | `ERR_NOSUCHCHANNEL` | No such channel. |
| `404` | `ERR_CANNOTSENDTOCHAN` | Cannot send to channel. |
| `409` | `ERR_NOORIGIN` | No origin specified. |
| `411` | `ERR_NORECIPIENT` | Formatter currently says “No origin specified” (text does not match the usual meaning). |
| `412` | `ERR_NOTEXTTOSEND` | No text to send. |
| `421` | `ERR_UNKNOWNCOMMAND` | Unknown command. |
| `431` | `ERR_NONICKNAMEGIVEN` | No nickname given. |
| `432` | `ERR_ERRONEUSNICKNAME` | Erroneous nickname. |
| `433` | `ERR_NICKNAMEINUSE` | Nickname already in use. |
| `441` | `ERR_USERNOTINCHANNEL` | Formatter currently says “Nickname is already in use” (text does not match the usual meaning). |
| `442` | `ERR_NOTONCHANNEL` | You're not on that channel. |
| `443` | `ERR_USERONCHANNEL` | Already on channel. |
| `451` | `ERR_NOTREGISTERED` | You have not registered. |
| `461` | `ERR_NEEDMOREPARAMS` | Not enough parameters. |
| `462` | `ERR_ALREADYREGISTERED` | You may not reregister. |
| `464` | `ERR_PASSWDMISMATCH` | Password incorrect. |
| `467` | `ERR_KEYSET` | Formatter currently says “Password incorrect” (text does not match the usual meaning). |
| `471` | `ERR_CHANNELISFULL` | Cannot join channel (`+l`). |
| `472` | `ERR_UNKNOWNMODE` | Unknown mode character. |
| `473` | `ERR_INVITEONLYCHAN` | Cannot join channel (`+i`). |
| `474` | `ERR_BANNEDFROMCHAN` | Cannot join channel (`+b`). |
| `475` | `ERR_BADCHANNELKEY` | Cannot join channel (`+k`). |
| `476` | `ERR_BADCHANMASK` | Bad channel mask. |
| `482` | `ERR_CHANOPRIVSNEEDED` | Formatter currently says “Bad Channel Mask” (text does not match the usual meaning). |
| `501` | `ERR_UMODEUNKNOWNFLAG` | Unknown user MODE flag. |
| `502` | `ERR_USERSDONTMATCH` | Cannot change mode for another user. |
| `525` | `ERR_INVALIDKEY` | Invalid key. |

These labels and meanings help identify the intended error. They do not mean
every command currently produces a fully standard reply.

## Error reply format in this project

The usual IRC numeric layout is:

```text
:server <numeric> <recipient> [parameters] :<text>\r\n
```

For example:

```text
:ircserv 421 alice FOO :Unknown command\r\n
```

The current `Dispatcher` catches an `IRCError` and calls
`e.format(clientNickname)`. The formatter writes the nickname, a colon, then
the error label/text; it does not include the command-specific argument stored
in the exception. A typical current line is therefore shaped like:

```text
:ircserv 421 alice: ERR_UNKNOWNCOMMAND: Unknown command\r\n
```

This differs from the standard layout: there is no space and command
parameter after `alice`, and the usual separating ` :` before the text is
missing. With an empty nickname argument, it writes `*` instead. Several error
text labels also differ from their numeric meanings, as listed above.

Successful registration and many command replies are built directly in their
handlers and have a more standard shape, for example:

```text
:ircserv 001 alice :Welcome to the Internet Relay Network alice!alice@127.0.0.1\r\n
```

## Reference links

- [RFC 2812, Client Protocol](https://datatracker.ietf.org/doc/html/rfc2812)
- [RFC 1459, original IRC protocol](https://datatracker.ietf.org/doc/html/rfc1459)
- [Modern IRC Documentation](https://modern.ircdocs.horse/)
- [Local server overview and sources](./server_info.md)
