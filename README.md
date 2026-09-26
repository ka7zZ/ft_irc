# ft_irc: sockets, framing, parsing, registration, users, channels, permissions, replies, and message routing

## 1. What IRC is

> IRC is a text protocol over a reliable byte stream, normally TCP. A client does
not talk directly to another client: it sends commands to a server, and the
server locates recipients, validates the request, changes state when necessary,
and sends messages to the recipients.

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

## 3. The server's core state

The sketch correctly identifies `server`, `sockets`, `clients`, `channels`,
`nick`, `state`, `permissions`, `admin`, and message parsing as the central
pieces. The important design decision is to make ownership and indexes explicit.

### 3.1 Connection state

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

### 3.2 Server indexes

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

### 3.3 Channel state

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

## 4. Socket setup and the event loop

The server must never block on one client. One slow or idle client must not
stop all other clients from being served.

### 4.1 Listener setup

1. Create a TCP socket with `socket`.
2. Set `SO_REUSEADDR` if appropriate for restart behavior.
3. Bind to the configured address and port.
4. Mark it non-blocking.
5. Call `listen`.
6. Add the listener to `poll`/`select`/`epoll` and watch for readability.

### 4.2 Accepting clients

When the listener is readable, accept in a loop until the non-blocking socket
reports that no more connections are ready. For every accepted socket:

1. Set it non-blocking.
2. Create a `Connection` with `registration = unregistered`.
3. Add it to the poll set and `connections`.
4. Do not read as though one `read` equals one IRC message.

### 4.3 One event-loop iteration

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

## 5. Byte stream framing and IRC messages

TCP is a stream. A single `read` can contain half a command, one command, or
many commands. The input buffer in the sketch is therefore essential.

### 5.1 Framing rules

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

### 5.2 Message grammar

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

## 6. Registration as a state machine

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

## 7. Command dispatch

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

## 8. Message delivery

There are three useful local delivery paths.

### 8.1 Private message

For `PRIVMSG nick :text`:

1. Validate recipient and text.
2. Look up the recipient in `users_by_nick`.
3. Create a message with the sender prefix.
4. Queue it on the recipient connection.
5. If the nickname is absent, send `401` to the sender.

### 8.2 Channel message

For `PRIVMSG #channel :text`:

1. Resolve the channel.
2. Confirm the sender is a member unless `+n` is disabled.
3. If moderated, permit only operators and voiced members.
4. Apply ban and other project-required checks.
5. Send one copy to every member except the sender, unless the protocol/client
   behavior requires an echo.

Use a recipient set when a command accepts multiple targets so one connection
does not receive duplicate copies through overlapping targets.

### 8.3 NOTICE

NOTICE is similar to PRIVMSG, but the server must not generate automatic error
replies to a NOTICE. This prevents automated clients from entering reply loops.

### 8.4 Output queues

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

## 9. Channels and permissions

### 9.1 Channel lifecycle

For ordinary `#`, `&`, and `+` channels, creation is implicit on the first
successful JOIN and deletion occurs when the last member leaves. The first user
normally receives channel-operator status, except where the channel type or
project rules say otherwise.

Channel names are case-insensitive and cannot contain spaces, comma, control-G,
or CR/LF. RFC 2812 allows prefixes `#`, `&`, `+`, and `!`, with a maximum length
of 50 characters.

### 9.2 JOIN decision

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

### 9.3 PART, KICK, and QUIT

All three remove membership, but their authority and notification differ:

- `PART`: the user voluntarily leaves one or more channels.
- `KICK`: a channel operator removes another user.
- `QUIT`: the user connection ends; remove the user from every channel and then
  notify the remaining members.

After removal, delete an ordinary channel if it has no members. Every removal
path must update both sides of the relationship: the channel's member map and
the user's joined-channel set.

### 9.4 MODE and TOPIC

`MODE` has two domains: user modes and channel modes. Parse the complete mode
change before applying it, because modes such as `+k`, `+l`, `+o`, `+v`, and `+b`
consume parameters. Check the sender's authority for each change and send one
canonical MODE notification after the mutation.

`TOPIC #channel` reads the topic. `TOPIC #channel :new text` changes it, subject
to membership and `+t` rules. An empty trailing topic clears it.

## 10. Connection loss, PING, and cleanup

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

## 11. Security and resource limits

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

## 12. Recommended implementation order

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

## 13. Server-to-server extension

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

## 14. Review of the Excalidraw sketch

The sketch is directionally correct and already suggests a clean decomposition:

```text
sockets -> text parser -> commands -> arguments/messages -> state changes
									  |                   |
									  +-> clients         +-> channels
```

Its strongest parts are:

- separating the server, sockets, parser, commands, arguments, and messages;
- identifying users with nick, state, and permissions;
- modeling channels with names, owners, and clients;
- remembering offline messages and message limits as future concerns;
- distinguishing regular users from administrators.

The changes needed before coding are:

1. Add an explicit poll/select event-loop node between sockets and parser.
2. Add an input buffer per socket; `read()` does not return messages.
3. Add an output queue per socket; `send()` does not send everything.
4. Replace one vague `state` with connection registration state, user state,
   and channel membership state.
5. Add the `users_by_nick` and `channels_by_name` indexes.
6. Add a single cleanup path for QUIT, EOF, errors, and timeouts.
7. Separate parsing from command authorization and state mutation.
8. Make permissions a decision made by each handler, not a property trusted from
   the client message.
9. Mark offline messages, DNS/Ident lookups, and server-to-server networking as
   later phases rather than dependencies of the first event loop.

The resulting algorithm is:

```text
poll sockets
  -> accept/read/write
  -> buffer bytes and extract CRLF lines
  -> parse Message
  -> reject if registration/limits fail
  -> handler validates permissions and targets
  -> mutate indexes, users, channels
  -> queue numerics and broadcasts
  -> clean up disconnected objects
```

## 15. Minimal acceptance checklist

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

## References

- [RFC 1459: Internet Relay Chat Protocol](https://www.rfc-editor.org/info/rfc1459/)
- [RFC 2810: Internet Relay Chat: Architecture](https://www.rfc-editor.org/info/rfc2810/)
- [RFC 2811: Internet Relay Chat: Channel Management](https://www.rfc-editor.org/info/rfc2811/)
- [RFC 2812: Internet Relay Chat: Client Protocol](https://www.rfc-editor.org/info/rfc2812/)
- [RFC 2813: Internet Relay Chat: Server Protocol](https://www.rfc-editor.org/info/rfc2813/)