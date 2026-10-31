# ft_irc Learning Companion

## A first- and second-year student's guide to the project

This guide explains the ideas a student can learn from this repository: its
architecture, C++98 patterns, standard-library types, POSIX networking calls,
IRC message processing, and command implementation. Every library and API
named here is one that appears in the project's source or build configuration.
It is a guide to this codebase, not a catalog of every feature in C++ or IRC.

The project is a small, single-server IRC implementation. It accepts TCP
clients, reads IRC-style text commands, tracks users and channels, and replies
to clients. It is not a network of linked IRC servers.

---

## 1. The project in one picture

```text
main.cpp
  validates arguments, installs signal handlers, constructs Server
       |
       v
Server
  owns the listening socket, connected Client objects, Channels,
  poll descriptors, and Dispatcher
       |
       +-- socket bytes --> Client input buffer
       |                       |
       |                       v
       |                  IRCParser --> Message
       |                                    |
       |                                    v
       |                               Dispatcher
       |                                    |
       |                  +-----------------+-----------------+
       |                  v                                   v
       |             Command object                      IRCError
       |                  |                                   |
       +<-----------------+------------------- reply text ----+
       |
       +-- output buffer --> send() when poll reports writable
```

### The short mnemonic: **B-P-D-C-Q**

- **B — Bytes:** TCP delivers bytes, not IRC commands.
- **P — Parse:** collect a complete line and turn it into a `Message`.
- **D — Dispatch:** find a command object and check its registration rule.
- **C — Change:** validate permissions, update `Client` or `Channel` state.
- **Q — Queue:** append reply text to an output buffer; send it when writable.

When following any command, ask: **Where do its bytes come from? Which object
owns the state? Who is allowed to change it? Where is the reply queued?**

---

## 2. Source tree and the job of each area

| Area | Responsibility |
|---|---|
| `main.cpp` | Argument validation, signal handling, server lifetime, top-level exceptions. |
| `headers/` | Class declarations, data structures, and command interfaces. |
| `sources/Server.cpp` | Listener setup, `poll` loop, accepting clients, input framing, output sending, and server state lookup. |
| `sources/Client.cpp` | Per-connection identity, registration flags, input/output buffers, user mode state. |
| `sources/Channel.cpp` | Channel membership, roles, modes, invitations, bans, key, limit, and topic. |
| `sources/IRCParser.cpp` | Splits one IRC line into prefix, command, ordinary parameters, and trailing parameter. |
| `sources/Dispatcher.cpp` | Maps lowercase command names to command objects, checks registration and minimum parameter count. |
| `sources/IRCError.cpp` | Formats the project's numeric-style error messages. |
| `sources/Commands/` | One class per implemented command. |
| `Makefile` | C++98 build configuration and source-file list. |

### Commands currently registered by `Dispatcher`

`PASS`, `NICK`, `USER`, `JOIN`, `PING`, `INVITE`, `PRIVMSG`, `KICK`, `MODE`,
`TOPIC`, and `LIST` are registered. Command lookup is case-insensitive:
`IRCParser` lowercases the parsed command, and `Dispatcher` lowercases again
before looking up the command object.

The existence of a channel field or a numeric constant does not mean every
related command is implemented. For example, this project has topic storage
and a TOPIC command, but that does not make the entire IRC protocol available.

---

## 3. Build and language level

The Makefile builds an executable named `ircserv`:

```sh
make
./ircserv 6667 password
make clean
make fclean
make re
```

The build uses:

```make
CXX       = c++
CXXFLAGS  = -Wall -Wextra -Werror -std=c++98
CPPFLAGS  = -Iheaders
```

What each setting means:

- `c++`: invoke the C++ compiler.
- `-std=c++98`: use the language standard required by the project.
- `-Wall -Wextra`: ask for common and additional compiler warnings.
- `-Werror`: treat warnings as errors; a warning can stop the build.
- `-Iheaders`: allow includes such as `#include "Server.hpp"`.
- `-MMD -MP`: generate dependency files so changes to included headers can
  cause the relevant object file to rebuild.

`SOURCES` is the authoritative list of `.cpp` files that are linked. When
adding a new command, add its implementation file to `SOURCES`, include its
header in the dispatcher implementation, and register it in the command map.

The project uses object files (`.o`) and dependency files (`.d`):

- `make clean` removes object and dependency files.
- `make fclean` additionally removes `ircserv`.
- `make re` does a clean rebuild.

---

## 4. Libraries and APIs used by this project

This section stays within the headers included by the project and the APIs
actually used in its implementation. It does not teach unneeded alternatives
such as `epoll`, `select`, threads, or modern C++ facilities.

### 4.1 C++ standard-library headers

| Header | Project use | Example of the project's use |
|---|---|---|
| `<string>` | Text for commands, names, prefixes, buffers, replies, and passwords. | `user.prefix()` builds `nickname!username@hostname`. |
| `<vector>` | Ordered collections of poll entries, channels, parameters, flags, and command lists. | `Message::params` stores parsed IRC parameters. |
| `<map>` | Keyed lookup for clients, members, and registered commands. | `_clients` maps a socket descriptor to its `Client`. |
| `<algorithm>` | Finds an invitation in the invitation vector. | `std::find(_invited.begin(), _invited.end(), fd)`. |
| `<sstream>` | Builds text containing numeric values. | `std::ostringstream` formats a channel limit for a reply. |
| `<iostream>` | Writes startup, error, and shutdown messages to standard streams. | `std::cerr` prints invalid command-line usage. |
| `<string>`/`<vector>` iterator APIs | Traverse strings and containers without copying every element. | `std::vector<Channel>::const_iterator` traverses channels in LIST. |
| `<cstddef>` | Defines `size_t`, used for lengths, indexes, and parameter counts. | `ICommand::minParams()` returns `size_t`. |
| `<exception>` | Base exception type for `IRCError`. | `IRCError` derives from `std::exception`. |
| `<stdexcept>` | Standard runtime-error exception. | `Server::run()` throws `std::runtime_error` on a fatal poll failure. |
| `<cstdlib>` | C conversion and process-status functions. | `std::strtol` validates the port; MODE uses `std::atoi` for a limit. |
| `<cctype>` | Character conversion. | `std::tolower` normalizes command letters. |
| `<cstring>` | Memory utilities for C structures. | `std::memset` zeroes `sockaddr_in`. |
| `<ctime>` | Timestamp type and clock function for ban records. | `time(&now)` stores when a ban was added. |
| `<csignal>` | Signal types and registration. | `sig_atomic_t` records whether the server should keep running. |
| `<cerrno>` | Error number state used with system calls. | `errno == EINTR` lets the poll loop retry after an interrupted wait. |

> **Header discipline:** include the header that declares the API being used.
> A successful compile caused by a transitive include is not a reliable
> substitute for including the correct declaration.

### 4.2 POSIX and networking headers

| Header | Project APIs or types | Why they are used |
|---|---|---|
| `<sys/socket.h>` | `socket`, `setsockopt`, `bind`, `listen`, `accept`, `recv`, `send`, `socklen_t`, socket flags. | Creates and operates TCP sockets. |
| `<netinet/in.h>` | `sockaddr_in`, `INADDR_ANY`, `htons`, `htonl`. | Describes an IPv4 listener address and converts port/address integers to network byte order. |
| `<arpa/inet.h>` | `inet_ntoa`, `ntohs`. | Converts the peer IPv4 address to text and the peer port to host byte order. |
| `<poll.h>` | `poll`, `struct pollfd`, `POLLIN`, `POLLOUT`, `POLLERR`, `POLLNVAL`, `POLLHUP`. | Waits for readiness on the listener and client sockets. |
| `<fcntl.h>` | `fcntl`, `F_SETFL`, `O_NONBLOCK`. | Sets listener and client sockets to nonblocking mode. |
| `<unistd.h>` | `close`. | Closes sockets during disconnect and shutdown. |

These are operating-system APIs, not C++ classes. They return integer
descriptors or status values, and many report failure through `-1` and
`errno`.

### 4.3 API inventory: C++ string operations used

| Operation | Meaning in this project | Example situation |
|---|---|---|
| `size()` / `length()` | Number of characters. | Check buffer length or iterate through command letters. |
| `empty()` | Whether a string contains no characters. | Distinguish an unset topic from a stored topic. |
| `find()` | Find a character or substring; `npos` means not found. | Find a newline in a receive buffer; search a mode string. |
| `find_first_of()` | Find the first character matching any character in a set. | Find a delimiter or a forbidden character. |
| `substr()` | Copy a section of a string. | Extract a complete line from a receive buffer. |
| `erase()` | Remove characters. | Remove a consumed line from the input buffer. |
| `append()` and `operator+=` | Add text. | Queue output or build an IRC prefix. |
| `c_str()` | Get a null-terminated view for a C conversion function. | Pass a limit string to `std::atoi`. |
| `npos` | Sentinel meaning a search did not find a match. | Detect that there is no newline yet. |

### 4.4 API inventory: containers and iteration

#### `std::vector<T>`

The project uses `std::vector` for ordered collections:

- `_channels`: channel instances owned by `Server`.
- `_pollSockets`: the `pollfd` entries passed to `poll`.
- `Message::params`: parsed parameters in source order.
- `_banList`: ban records in a channel.
- `_invited`: invited client descriptors.
- temporary command lists and MODE flags.

Used operations include `push_back`, `begin`, `end`, `erase`, `size`, and
`clear`. Iterators point to elements; erasing a vector element can invalidate
iterators at and after it. In `Server::removeClient`, the loop uses the
iterator returned by `erase` to continue safely.

#### `std::map<Key, Value>`

The project uses ordered key/value lookup:

- `Server::_clients`: socket descriptor → `Client`.
- `Channel::_members`: socket descriptor → `Member`.
- `Dispatcher::_commands`: lowercase command text → `ICommand*`.

Used operations include `find`, `end`, `begin`, `erase`, `clear`, and
`operator[]`. A `find` result equal to `end()` means the key is absent.
`operator[]` inserts a default entry when the key is not already present.

#### Iterators and const iterators

An iterator is a position in a container. A `const_iterator` permits reading
but not modifying the container element through that iterator. Common loop
shape:

```cpp
for (std::map<int, Member>::const_iterator it = members.begin();
     it != members.end(); ++it)
{
    // it->first is the key; it->second is the mapped value.
}
```

### 4.5 Streams and conversion

- `std::ostringstream` accumulates text with `<<`; the result is retrieved
  with `.str()`. LIST and MODE use this to include integer counts and limits
  in textual replies.
- `std::strtol` converts the port argument while exposing where parsing
  stopped through an end pointer. `main` checks empty input, trailing
  characters, and the legal TCP port range.
- `std::atoi` converts the channel limit parameter in MODE. It is shorter,
  but does not report malformed input as carefully as `strtol`; students
  should notice that parsing a number and validating a number are separate
  tasks.
- `std::tolower` takes an `int` value representing an unsigned character or
  `EOF`. The dispatcher converts through `unsigned char` to avoid passing a
  negative signed `char`.

---

## 5. C++ building blocks demonstrated here

### 5.1 Classes, objects, and headers

A class groups data and functions that operate on that data. `Client` contains
per-connection state; `Channel` contains shared channel state; `Server`
coordinates them.

- A `.hpp` file declares the class shape and public interface.
- A `.cpp` file defines functions declared in the header.
- Header guards (`#ifndef`, `#define`, `#endif`) stop a header from being
  included more than once in one compilation unit.
- `#include` brings declarations into a translation unit.

For example, `Client.hpp` declares `getNickname()`, while `Client.cpp`
provides its definition.

### 5.2 Constructors and initialization lists

The server constructor initializes its port, password, and listener descriptor:

```cpp
Server::Server(int port, const std::string& password)
    : _port(port), _password(password), _serverFd(-1) {}
```

An initialization list initializes members as the object is constructed. It
is the correct place to initialize members, especially references and
const-qualified members. In this codebase, connection constructors also
initialize registration flags and socket state.

### 5.3 Encapsulation and access

- `private` members are implementation state.
- `public` methods are the supported interface other classes use.
- Getters such as `Channel::getTopic()` expose state for reading.
- Setters such as `Channel::setTopic()` update state.
- Predicates such as `Channel::isAdmin(fd)` answer a yes/no question.

Command objects call public interfaces instead of reaching into private
members. This keeps command policy separate from storage details.

### 5.4 References, pointers, and ownership

- A reference parameter such as `const std::string&` avoids copying a string
  and communicates that the function will not modify it.
- `Client*` and `Channel*` are pointers. A null pointer (`NULL` in this C++98
  code) represents “not found” or “not available.”
- Channel membership stores `Client*` pointers, but `Server::_clients` owns
  the `Client` objects. These member pointers are therefore non-owning.
- `Dispatcher` allocates command objects with `new` and deletes them in
  `clear()`. Its destructor calls `clear()`.

**Ownership question for students:** if an object stores a pointer, identify
who creates and destroys the pointed-to object. A non-owning pointer must not
be used after its owner erases the object.

### 5.5 Inheritance and virtual methods

Each command implements the abstract `ICommand` interface:

```cpp
virtual bool needsRegistration() const = 0;
virtual size_t minParams() const = 0;
virtual void execute(Server&, Client&, const Message&) const = 0;
```

The `= 0` declarations are pure virtual functions; they require a derived
command class to supply an implementation. The dispatcher can store
`ICommand*` pointers and call `execute()` without knowing whether the object is
a `JoinCommand`, `ListCommand`, or another command.

### 5.6 `const`

`const` expresses a read-only contract:

- `const Message& msg`: a command should read the parsed request, not alter
  it.
- `...() const`: a method promises not to modify the object through `this`.
- `const std::vector<Channel>&`: a caller can inspect the channel list but
  cannot mutate the vector through this reference.

The codebase does not use `const` perfectly consistently; use the actual
declaration as the contract when studying a specific method.

### 5.7 Enums and small records

- `TokenType` identifies parser token categories: `PREFIX`, `COMMAND`,
  `PARAMETER`, `TRAILING`, and `END_OF_LINE`.
- `Action` identifies a MODE change as `ADD` or `REMOVE`.
- `Token`, `Parameter`, and `Message` are small data records.
- `Member` stores a client pointer and channel role booleans.
- `BanFormat` stores the banned value, author prefix, and time.

These types make relationships explicit rather than passing unrelated values
without labels.

### 5.8 Exceptions and error flow

`IRCError` represents a command-level protocol error, such as a missing
parameter or missing channel. The dispatcher catches `IRCError` and queues
its formatted response. The server loop catches broader `std::exception`
errors around parsing/dispatch and treats them as internal errors.

The intended distinction is:

- **Expected client mistake:** throw `IRCError`; report it to that client.
- **Unexpected server failure:** throw a standard exception or handle a
  system-call error; do not disguise it as a valid command response.

### 5.9 C++98 constraints

The project intentionally builds as C++98. Its style uses:

- `NULL`, not `nullptr`.
- Explicit iterator types rather than range-based `for`.
- `std::map` and `std::vector` with no modern smart pointers.
- Ordinary `new`/`delete` for the dispatcher-owned command objects.

This guide does not introduce later language features into the project's
examples.

---

## 6. The IRC text message model

An IRC message is a line of text. In this project's parser, a message is
represented by:

```cpp
struct Message {
    std::string prefix;
    std::string command;
    std::vector<Parameter> params;
};
```

Each `Parameter` contains a `value` and an `isTrailling` flag. The spelling
`isTrailling` is the spelling used by this project.

### Example: parsing `PRIVMSG #class :Hello, students`

Expected conceptual result:

```text
prefix  = ""
command = "privmsg"
params  = [
  { value: "#class", isTrailling: false },
  { value: "Hello, students", isTrailling: true }
]
```

The leading colon before `Hello, students` means “the rest of this line is one
trailing parameter,” so its internal space is preserved. The parser lowers
the command text but does not lowercase nicknames or other parameters.

### Parser sequence

1. The server removes one complete line from the client input buffer.
2. `IRCParser` receives that line.
3. `tokenizer()` optionally reads a prefix, reads the command, and then reads
   ordinary parameters or one trailing parameter.
4. The command token is lowercased.
5. `parseTokens()` constructs a `Message`.
6. `Dispatcher::dispatch()` passes it to the matching command.

`appendUntil(src, delimiters)` uses `find_first_of()` to find the next
delimiter, then `substr()` to copy the text before it. The parser uses a
`std::vector<Token>` as an intermediate representation.

### TCP is a byte stream, not a message stream

One `recv()` may produce:

- part of a command;
- exactly one command;
- several commands at once.

Therefore, the server appends received bytes to `Client::_inputBuffer` and
processes lines ending in `\n`. It removes a preceding `\r` if present. It
checks the IRC line-size limit before dispatching.

**Study exercise:** send `NI`, wait, then send `CK student\r\n`. A correct
stream-framing design must preserve the first fragment and process `NICK`
only after the line is complete.

---

## 7. Networking: the exact socket workflow used

### 7.1 Start-up

`main()` expects two arguments after the program name:

```text
ircserv <port> <password>
```

It uses `std::strtol` to parse the port, rejects empty or trailing text, and
accepts only values from 1 through 65535. It installs SIGINT and SIGTERM
handlers that change a `volatile sig_atomic_t` flag. It then constructs
`Server` and calls `run()`.

### 7.2 Listener setup

`Server::initServer()` does the following:

1. `socket(AF_INET, SOCK_STREAM, 0)` creates an IPv4 TCP socket.
2. `setsockopt(..., SO_REUSEADDR, ...)` requests address reuse.
3. `fcntl(..., F_SETFL, O_NONBLOCK)` makes the listener nonblocking.
4. `sockaddr_in` describes the address and port.
5. `htonl(INADDR_ANY)` selects all local IPv4 interfaces.
6. `htons(_port)` converts the port to network byte order.
7. `bind()` associates the address with the socket.
8. `listen()` starts accepting incoming connections.

### 7.3 Accepting clients

When `poll()` marks the listener readable, `registerClient()` calls `accept()`.
The accepted descriptor is made nonblocking, the remote IPv4 address is
converted with `inet_ntoa`, and a `Client` is inserted into `_clients`.

The accepted socket is a separate descriptor from the listener:

```text
_serverFd  -> accepts new TCP connections
clientFd   -> exchanges bytes with one connected user
```

### 7.4 The `poll` event loop

`rebuildPollFds()` creates a `pollfd` for the listener and each client.
Clients always request `POLLIN`; a client also requests `POLLOUT` when its
output buffer is nonempty.

`run()` repeatedly:

1. rebuilds the poll descriptor vector;
2. calls `poll(..., -1)` to wait indefinitely;
3. retries if interrupted (`errno == EINTR`);
4. calls `handlePollEvents()` to process readiness.

The relevant readiness flags are:

- `POLLIN`: data can be read, or a connection can be accepted.
- `POLLOUT`: queued output can be written.
- `POLLERR` / `POLLNVAL`: socket error or invalid descriptor.
- `POLLHUP`: peer hung up.

### 7.5 Input and output

- `recv()` reads available bytes into a fixed-size local buffer.
- `Client::appendBuffer()` accumulates bytes.
- `extractValidData()` separates complete lines and dispatches messages.
- `sendTo()` appends a reply to the recipient's output buffer.
- `sendData()` calls `send()` when `poll()` reports `POLLOUT`.
- `eraseOutBuffer()` removes the successfully sent prefix.
- `close()` releases descriptors when clients or the server stop.

### 7.6 Socket-call study table

| API | Project role | Key learning point |
|---|---|---|
| `socket` | Create the listening endpoint. | The returned integer is a file descriptor. |
| `setsockopt` | Configure `SO_REUSEADDR`. | Socket options are configured before binding. |
| `fcntl` | Set `O_NONBLOCK`. | A nonblocking call must be integrated with readiness handling. |
| `bind` | Claim a local address and port. | The address structure uses network byte order. |
| `listen` | Mark the socket as a listener. | It does not itself accept clients. |
| `accept` | Create a connected client descriptor. | Listener and client descriptors have different jobs. |
| `poll` | Wait for descriptor readiness. | Readiness is an opportunity to attempt I/O, not a complete IRC message. |
| `recv` | Receive client bytes. | Positive bytes, zero (peer closed), and negative error are distinct cases. |
| `send` | Send queued bytes. | A successful call can send fewer bytes than requested; the queue must track the remainder. |
| `close` | Release a socket descriptor. | Every accepted descriptor needs a cleanup path. |

---

## 8. Data model: who owns what?

### 8.1 `Server`

`Server` is the coordinator and owner of top-level state:

```text
Server
  _serverFd
  _clients: socket fd -> Client
  _channels: vector of Channel
  _pollSockets: vector of pollfd
  _dispatcher
```

The command handlers receive `Server&`, so they can find channels and users,
queue replies, broadcast to members, and create channels.

### 8.2 `Client`

One `Client` is created for each accepted connection. It stores:

- socket descriptor;
- nickname, username, hostname, real name;
- input and output buffers;
- PASS, NICK, and registration state;
- close-related flags;
- invisible-user-mode state.

The `Client` is stored by value in `Server::_clients`, keyed by descriptor.
Channel membership stores a non-owning pointer to that stored object.

### 8.3 `Channel`

One `Channel` stores:

- channel name;
- members keyed by socket descriptor;
- invitation descriptors;
- ban records;
- key and member limit;
- topic;
- active mode letters.

The `Member` record stores a `Client*`, operator status, and voice status.
`getChannelSize()` is derived from the member map size.

### 8.4 Important invariants to learn

An invariant is a condition the program expects to remain true.

1. A socket descriptor identifies one connected `Client` while it remains in
   `_clients`.
2. A channel member-map key is the member's socket descriptor.
3. `Member::cl` points to a `Client` stored by the server.
4. A channel operator is represented by `Member::isAdmin`.
5. A voiced member is represented by `Member::isVoiced`.
6. A channel's modes are stored as letters in `_modes`; key and limit have
   separate fields.
7. An invited user is currently represented by a socket descriptor, not by a
   nickname.

Before changing a class, identify the invariant it enforces. Before removing a
client, check every channel that may contain its pointer.

---

## 9. Registration and command dispatch

### 9.1 Registration state

This project uses separate PASS and NICK state flags and a registered/auth
flag. The flow represented in the command handlers is:

```text
accepted TCP connection
  -> PASS correct
  -> NICK accepted
  -> USER accepted
  -> registered; welcome numerics queued
```

`PASS` validates the configured server password. `NICK` checks a simple set
of prohibited characters and checks for an existing nickname. `USER` requires
four parameters, stores username and real name, then marks the client
registered.

The current implementation expects PASS before NICK, and NICK before USER.
The project guide's proposed “either NICK/USER order” is not a guarantee of
the implementation. This distinction is useful: documentation can describe a
desired protocol while the source reveals actual current behavior.

### 9.2 The command interface pattern

Every command implements:

```cpp
bool needsRegistration() const;
size_t minParams() const;
void execute(Server&, Client&, const Message&) const;
```

`Dispatcher::dispatch()`:

1. lowercases the command name;
2. looks it up in `_commands`;
3. reports unknown commands;
4. enforces `needsRegistration()`;
5. enforces `minParams()`;
6. invokes `execute()`;
7. catches `IRCError` and queues an error response.

The minimum-parameter check is a first guard, not a replacement for checking
the parameter shape inside a command. For example, PRIVMSG has a variable
number of parameters and must check that a message body exists.

### 9.3 Why use one command class per command?

The dispatcher does not contain all command logic. Each command object
encapsulates:

- the registration policy;
- its minimum parameter count;
- command-specific validation;
- state changes;
- replies and broadcasts.

This allows a student to add one command without inserting its full behavior
into the server event loop.

---

## 10. Command guide: examples and use cases

The examples below show the command syntax and the behavior implemented by
this repository. `password` means the password supplied on the server
command-line.

### PASS

```text
PASS password
```

- Requires one parameter.
- Does not require the user to be registered.
- On a matching password, marks the PASS state and queues a local approval
  message.
- A wrong password marks the client for close and raises an error.

Study it to see how configuration from `main()` reaches a command via
`Server::getPassword()`.

### NICK

```text
NICK robin
```

- Requires a successful PASS first.
- Rejects empty or prohibited nicknames.
- Checks whether another client already uses the nickname.
- Stores the nickname and sets the “has nick” state.

Study it to see validation before mutation: the object should not save a
nickname that has failed validation.

### USER

```text
USER robin 0 * :Robin Student
```

- Requires four parameters and an accepted NICK.
- Stores the username from parameter 1 and real name from parameter 4.
- Marks the client registered and queues welcome replies.

### JOIN

```text
JOIN #class
JOIN #class,#lab key-for-lab
```

- Requires registration.
- Splits comma-separated channel and key lists.
- Validates the channel name.
- Creates a channel if it does not exist; its creator is added as an operator.
- For an existing channel, checks membership, limit, invite-only state, bans,
  and key before adding the client.
- Broadcasts JOIN and sends topic/NAMES replies.

Use JOIN to study a multi-step command: validation must happen before the
membership map is changed.

### PRIVMSG

```text
PRIVMSG robin :Hello
PRIVMSG #class :The lab begins now
PRIVMSG #class,#lab :Shared note
```

- Requires registration.
- Sends to a nickname or broadcasts to a channel's members, excluding the
  sender.
- For channel messages it checks channel existence, secret-channel
  membership, ban state, `+n` external-message restriction, and `+m`
  moderation (operator or voiced member may speak).
- Comma-separated recipients are processed individually.

### PING

```text
PING :token123
```

- The command's minimum parameter count is zero, but execution requires a
  token and raises an error if it is missing.
- Queues a PONG containing the supplied token.

This is a good example of why “minimum parameter count” and “required by a
particular execution path” can differ.

### INVITE

```text
INVITE robin #class
```

- Requires registration.
- Requires the channel to exist and the sender to be a member and operator.
- Requires the target nickname to exist and not already be a member.
- Stores the target's socket descriptor in the invitation list.
- Sends a numeric reply to the inviter and an INVITE message to the target.

### KICK

```text
KICK #class robin :Please rejoin after the break
```

- Requires registration.
- Requires the channel to exist, the sender to be a member and operator, and
  the target to be a channel member.
- Broadcasts a KICK message, then removes the target from membership.
- A list of nicknames can be used; errors for individual targets are caught
  and sent to the requester.

### MODE

Examples:

```text
MODE #class
MODE #class +m
MODE #class +v robin
MODE #class +k secret
MODE #class +l 20
MODE robin +i
```

The implementation supports channel mode letters `i`, `t`, `m`, `n`, `p`,
`s`, `b`, `k`, `l`, `o`, and `v`; the user-mode implementation handles `i`.
Channel changes are checked against operator status. Parameterized modes
consume parameters in mode-letter order.

The current `+b`/`-b` path is narrower than IRC ban masks: it looks up the
parameter as a connected client's exact prefix before adding or removing it.
The mode letters are not all connected to every possible IRC command. For
example, a stored mode should not be assumed to implement every behavior in
the IRC specification.

### TOPIC

```text
TOPIC #class
TOPIC #class :The test starts at 14:00
TOPIC #class :
```

- Requires registration and channel membership.
- A query returns `331` if no topic exists or `332` with the current topic.
- Setting or clearing a topic updates the channel and broadcasts a TOPIC
  message.
- With `+t`, only a channel operator can set or clear the topic.
- An empty trailing parameter clears the topic; it is different from omitting
  the parameter entirely.

### LIST

```text
LIST
LIST #class,#lab
```

- Requires registration.
- Sends start (`321`), channel rows (`322`), and end (`323`) replies.
- A row includes channel name, current membership count, optional `/limit`,
  and topic.
- Private and secret channels are shown only to their members.
- A comma-separated argument filters the channels displayed.

Example row shapes:

```text
:ircserv 322 robin #open 4 :Topic visible to members
:ircserv 322 robin #limited 4/12 :Twelve user limit
```

---

## 11. IRC replies and error handling

IRC servers use three-digit numeric replies for many responses. In this
project, examples include:

- `001`: welcome;
- `321`, `322`, `323`: LIST start, row, end;
- `331`, `332`: TOPIC is unset / TOPIC text;
- `341`: INVITE confirmation;
- `353`, `366`: NAMES list and end;
- `401`, `403`, `404`, `442`, `461`, `482`: common errors.

`IRCError` stores an integer code and an argument. `format()` builds text with
`std::ostringstream`. The dispatcher catches the error and calls
`Server::sendTo()`.

**Important learning point:** protocol correctness includes exact spacing,
prefix, numeric fields, parameter order, and CRLF termination. A message that
looks understandable to a human may still not be understood by a client.

The current error formatter is a teaching example of mapping codes to text,
but its output formatting is not a full RFC-compliant numeric-reply formatter.
Treat the source as the project's current behavior, not as the final protocol
standard.

---

## 12. Methods by class: a source-level index

This is an index of the principal methods students encounter in this
repository. Private helper methods are included where they explain the
implementation flow.

### `main.cpp`

| Function | What it does |
|---|---|
| `handleSignal(int)` | Sets the global run flag to false when SIGINT or SIGTERM arrives. |
| `main(int, char**)` | Checks argument count and port, installs signal handlers, constructs `Server`, calls `run()`, and reports exceptions. |

### `Server`

| Method | What it does |
|---|---|
| `Server(int, const std::string&)` | Initializes configured port/password and listener descriptor. |
| `~Server()` | Calls `stop()` for cleanup. |
| `run()` | Initializes listener and repeatedly waits for/handles `poll` events. |
| `stop()` | Calls server shutdown cleanup. |
| `initServer()` | Creates, configures, binds, and listens on the server socket. |
| `passCreateErrors()` | Checks socket creation, `SO_REUSEADDR`, and nonblocking setup. |
| `passBindListenErrors(sockaddr_in*)` | Performs `bind()` and `listen()`. |
| `rebuildPollFds()` | Builds poll entries for listener and connected clients. |
| `handlePollEvents()` | Handles readable, writable, and failed/hung-up descriptors. |
| `registerClient()` | Accepts a connection, makes it nonblocking, and inserts a `Client`. |
| `handleClientData(int)` | Receives bytes, appends them, extracts commands, and enforces pending input limits. |
| `extractValidData(Client*)` | Separates complete lines, strips CR, validates line size, parses and dispatches each command. |
| `sendData(int)` | Sends pending output bytes and removes successfully sent bytes from the queue. |
| `sendTo(Client&, const std::string&)` | Appends reply text to one client's output buffer. |
| `broadcast(const Channel&, const std::string&, int)` | Appends a message to channel members, optionally excluding one descriptor. |
| `disconnectClient(int, const std::string&)` | Logs a reason and removes/closes a client. |
| `removeClient(int)` | Removes invitation and membership references, broadcasts QUIT when appropriate, erases and closes the client. |
| `shutdownServer()` | Closes client and listener descriptors and clears poll state. |
| `getClientBySocket(int)` | Finds a client by descriptor. |
| `getClientByNickname(const std::string&)` | Scans connected clients for a nickname. |
| `getClientByPrefix(const std::string&)` | Scans connected clients for a complete prefix. |
| `getChannel(const std::string&)` | Finds a channel by exact name. |
| `getChannels() const` | Returns a const reference to the channel vector, used by LIST. |
| `getPassword() const` / `setPassword(...)` | Reads or updates the server password. |
| `createChannel(Client*, const std::string&)` | Creates a channel and adds its creator. |
| `isMemberOf(Channel*, Client*)` | Declared server query helper; check its current definition before relying on it. |
| `sendPrivateMessage(...)` / `sendChannelMessage(...)` | Declared communication helpers; command handlers in this project commonly perform routing directly. |
| `exceedsPendingLimit(size_t, size_t)` | Compares pending input bytes against a configured limit. |

### `Client`

| Method | What it does |
|---|---|
| `Client()` / `Client(int, const std::string&)` | Constructs an unbound or socket-associated client. |
| `getSocket()` | Returns the socket descriptor. |
| `getNickname()`, `getUsername()`, `getRealname()` | Return identity fields by const reference. |
| `getBuffer()`, `getOutBuffer()` | Return input and output buffers by const reference. |
| `getPassState()`, `getNickState()`, `getAuthState()` | Read registration flags. |
| `getMarkForClose()` | Reads the deferred-close marker used after an error response. |
| `isInvisible()` / `setInvisible(bool)` | Reads or changes user mode `+i`. |
| `setNickname`, `setUsername`, `setRealname` | Store identity values. |
| `setPassState`, `setNickState`, `setAuthState` | Update registration flags. |
| `setMarkForClose(bool)` | Marks the client for closure after output is flushed. |
| `prefix() const` | Builds `nickname!username@hostname`. |
| `appendBuffer`, `eraseBuffer`, `clearBuffer` | Accumulate or consume input bytes. |
| `appendOutbuffer`, `eraseOutBuffer`, `hasOutput` | Queue and consume output bytes. |

### `Channel`

| Method | What it does |
|---|---|
| `Channel(const std::string&)` | Creates a named channel with no member limit initially. |
| `getName`, `getKey`, `getTopic`, `getModes` | Read channel settings. |
| `getMember(int)` / `getMemberByNickname(...)` | Find a member. |
| `getAllMembers()` / `getBanList()` | Return const views of members or bans. |
| `getChannelSize()` / `getLimit()` | Read occupancy and configured limit. |
| `isMember`, `isAdmin`, `isVoiced` | Test membership and channel roles. |
| `isBanned`, `isInvited` | Test a ban string or invitation descriptor. |
| `isInviteOnly`, `isSecret`, `isNoExternalMsg`, `isModerated`, `isTopicOperatorOnly` | Test channel mode letters. |
| `setAdmin`, `setVoiced` | Change a member's operator/voice role; return whether a change occurred. |
| `setKey`, `setLimit`, `setTopic` | Store the corresponding channel property. |
| `setModes(char, bool)` | Add or remove a mode letter and report whether it changed. |
| `addMember`, `removeMember` | Add or remove a member record. |
| `addInvited`, `removeInvited` | Add/remove an invitation descriptor. |
| `findBannedIterator`, `banMember`, `removeBan`, `formattedBan` | Search, store, remove, and format ban records. |
| `hasMode(char)` | Search the stored mode-letter string. |

### `IRCParser`

| Method | What it does |
|---|---|
| `IRCParser(const std::string&)` | Initializes the cursor and tokenizes the input line. |
| `tokenizer(...)` | Identifies prefix, lowercased command, parameters, trailing parameter, and end token. |
| `parseTokens()` | Converts tokens into a `Message`. |
| `endOfSource(...)` | Checks whether the parser cursor reached the source end. |
| `currentChar(...)` / `advanceChar(...)` | Reads or advances the source cursor. |
| `skipWhitespaces(...)` | Moves to the next non-space character. |
| `appendUntil(...)` | Copies text until one of a specified set of delimiters. |
| `addToken(...)` | Appends a typed token to the token vector. |

### `Dispatcher`

| Method | What it does |
|---|---|
| `Dispatcher()` | Allocates and registers command objects. |
| `~Dispatcher()` / `clear()` | Deletes command objects and clears the command map. |
| `dispatch(Server&, Client&, const Message&)` | Normalizes and looks up a command, checks registration/parameter count, executes, and handles `IRCError`. |

### `IRCError`

| Method | What it does |
|---|---|
| `IRCError(int, const std::string&)` | Stores error code and associated argument. |
| `~IRCError()` | Supplies the exception destructor required by this class definition. |
| `format(const std::string&) const` | Builds the client-facing error line. |

### Command-specific helper methods

| Command class | Helpers and their roles |
|---|---|
| `NickCommand` | `isLegit(nick)` checks the implementation's nickname restrictions. |
| `JoinCommand` | `splitParams` parses comma lists; `validName` checks channel names; `freeSpace` checks a member limit; `joinChannel` performs one join; `sendReplies` sends topic and NAMES replies. |
| `PrivmsgCommand` | `splitParams` parses recipients; `sendOne` validates and routes one message. |
| `KickCommand` | `splitParams` parses lists; `kickOne` validates, broadcasts, and removes one target. |
| `ModeCommand` | `takesParam` identifies parameterized modes; `getFlagArray` maps mode letters to actions/parameters; `applyFlag` dispatches a mode operation; helpers handle channel details, bans, keys, limits, member roles, and user mode. |
| `ListCommand` | `splitParams` parses a channel filter; `isRequested` checks whether a channel matches it. |
| Other command classes | Implement `needsRegistration`, `minParams`, and `execute`. |

---

## 13. Procedures students should learn from the code

### Procedure A: Add a new command

1. Create a header under `headers/Commands/`.
2. Derive a class from `ICommand`.
3. Implement `needsRegistration()`, `minParams()`, and `execute()`.
4. Include `Server.hpp`, `Client.hpp`, `IRCParser.hpp`, and any required
   `Channel.hpp`/`IRCError.hpp` in the `.cpp`.
5. Validate parameters before accessing indexed elements.
6. Read state through public model methods.
7. Change state only after validation and permission checks pass.
8. Queue direct replies using `Server::sendTo()`.
9. Broadcast shared events using `Server::broadcast()`.
10. Include the command header and register the lowercase name in
    `Dispatcher.cpp`.
11. Add the `.cpp` file to `Makefile` `SOURCES`.
12. Run `make`, then test the command with at least two clients when the
    command broadcasts or changes shared state.

### Procedure B: Trace a bug

Use this four-step trace:

1. **Input:** What exact bytes were sent?
2. **Parse:** What does `Message` contain?
3. **Policy/state:** Which command checks run, and which state changes?
4. **Output:** Which client's output buffer gets which reply?

For example, if `PRIVMSG #class :hello` fails under `+m`, inspect:

- whether `params[0]` is `#class`;
- whether `params[1]` is trailing text;
- whether the sender is a member;
- whether `isAdmin(fd)` or `isVoiced(fd)` is true;
- whether the error is queued to the sender.

### Procedure C: Validate a channel operation

For JOIN, TOPIC, INVITE, KICK, or MODE, use this order:

```text
1. Required parameter count?
2. Does the target channel exist?
3. Is the sender a member, if required?
4. Does the channel mode add a condition?
5. Does the sender have the needed role?
6. Is the target member/user valid?
7. Change state.
8. Reply/broadcast.
```

This is a reasoning checklist, not a claim that every handler currently uses
exactly this order.

### Procedure D: Test a TCP command by hand

1. Start the server with a port and password.
2. Connect using `nc` or another TCP client.
3. Send `PASS`, `NICK`, and `USER`, each terminated by CRLF.
4. Read the welcome numerics.
5. Send one command and inspect its exact reply.
6. Use a second connection for broadcasts and permissions.

Example wire text:

```text
PASS classroom
NICK ana
USER ana 0 * :Ana Student
JOIN #lab
TOPIC #lab :Read the wire, not just the code
```

In a raw TCP test tool, make sure each command ends with `\r\n`. A terminal
newline alone may behave differently depending on the client.

---

## 14. How to test the whole lifecycle

### Registration

```text
PASS classroom
NICK ana
USER ana 0 * :Ana Student
```

Expected learning outcome: observe when the client moves from an accepted
socket to a registered user, and which replies appear in order.

### Channel membership and listing

```text
JOIN #open
LIST
```

Expected: JOIN notification, topic/NAMES replies, then LIST numeric sequence
with a member count.

### Topic and operator permission

```text
TOPIC #open
TOPIC #open :First topic
MODE #open +t
```

Use a second non-operator client to try changing the topic after `+t`.

### Moderated channel

```text
MODE #open +m
PRIVMSG #open :A member without voice tries this
MODE #open +v ana
```

Use a non-operator member and then grant voice. Check both the rejected and
accepted message paths.

### Multi-client observation

For a broadcast command, always inspect both:

- the sender's output;
- the other member's output.

This catches bugs where a state change succeeds but the event is not sent, or
where the sender receives an event intended only for other members.

---

## 15. Reading source without overgeneralizing

The source is the record of actual behavior; comments and study notes can
describe intent but may be ahead of implementation.

When analyzing a method:

1. Read its declaration in the header.
2. Read its definition in the `.cpp`.
3. Read the call site.
4. Read the data structure the method reads or mutates.
5. Check the Makefile to confirm the implementation is compiled.

Examples:

- `Channel::isTopicOperatorOnly()` is meaningful because it checks the
  channel mode string, and `TopicCommand` calls it before setting a topic.
- `ICommand::minParams()` is used by `Dispatcher`, but commands may still
  perform more detailed checks themselves.
- A field named `_limit` alone does not enforce a limit; `JOIN` must compare
  it with `getChannelSize()` before adding a member.
- A mode letter stored by MODE does not automatically implement all related
  commands. Each command must consult the mode explicitly.

---

## 16. Known learning caveats in this codebase

These are reminders to distinguish “what this implementation does” from “what
a robust production IRC server should do.” They are not additional changes to
the project.

- `Client` represents a connected client and a registered user in the same
  class; its registration booleans track the difference.
- Several user/channel lookups scan maps or vectors linearly. This is easy to
  understand, but lookup cost grows with the number of users/channels.
- The parser assumes it receives a line without its CRLF; framing is handled
  by `Server`.
- Some commands use project-specific or simplified reply text. Compare exact
  wire output with the protocol requirements expected by the course.
- Mode state, channel visibility, message permissions, topic restrictions,
  and membership operations are implemented in different places. A new mode
  needs checks in every relevant command, not only a place to store its
  letter.
- There are no automated unit-test files in the current repository snapshot;
  the Makefile compiles the program but does not run a test suite.
- The current source tree may contain uncommitted changes. Before submitting,
  inspect `git status` and avoid discarding work that is not yours.

---

## 17. Mnemonic cards

### Socket card: **C-A-W-R-Q**

- **C**reate with `socket`.
- **A**dd address with `bind`.
- **W**ait for a connection with `listen` + `poll`.
- **R**eceive bytes with `recv`.
- **Q**ueue and send replies with output buffers + `send`.

### Command card: **F-P-A-S-R**

- **F**ind command in dispatcher.
- **P**ermission and parameter checks.
- **A**ccess model state through public methods.
- **S**tore validated state change.
- **R**eply or broadcast.

### Ownership card: **O-P-R**

- **O**wner: who destroys the object?
- **P**ointer: is it owning or just referring?
- **R**emoval: are all references cleaned up before destruction?

### Stream card: **N-L-M**

- **N**etwork gives arbitrary byte chunks.
- **L**ines are reconstructed using LF and optional CR.
- **M**essages are parsed only after a complete line exists.

---

## 18. Self-check questions

1. Why can one `recv()` contain half of a `NICK` command?
2. Which class owns the `Client` object referenced by a channel member?
3. Why does the dispatcher call `needsRegistration()` before `execute()`?
4. What is the difference between a parameter and a trailing parameter?
5. Why does `+m` need to be checked inside PRIVMSG rather than only stored in
   the channel?
6. What must a JOIN handler verify before inserting the member?
7. Why is `poll()` used instead of blocking to read one client at a time?
8. What does `std::map::find()` return when a key is absent?
9. What should happen to the remaining bytes when `send()` sends only part of
   the output buffer?
10. Why is a `Client*` in a `Channel` a lifetime concern?
11. How does an empty trailing parameter differ from an omitted parameter in
    `TOPIC #channel :` versus `TOPIC #channel`?
12. Which two files must be updated when adding a command so it is compiled
    and dispatchable?

### Suggested answers

1. TCP is a byte stream and does not preserve application message boundaries.
2. `Server::_clients` owns the `Client`; the channel member pointer refers to
   it.
3. To reject commands not allowed before registration and prevent handlers
   from receiving an unregistered caller.
4. An ordinary parameter ends at whitespace; a trailing parameter consumes
   the rest of the line and may contain spaces.
5. Storage alone does not enforce policy; PRIVMSG must consult the mode before
   broadcasting.
6. Validate the channel, membership, limit, invitation, ban, and key checks
   that apply before adding the client.
7. So one idle client does not prevent the server from serving others.
8. The container's `end()` iterator.
9. Keep and later send the unsent suffix.
10. The pointer becomes invalid if the owning client is erased.
11. The first is a request to read; the second supplies an empty value and
    clears the topic.
12. Add/register it in `sources/Dispatcher.cpp` and add its `.cpp` to
    `Makefile` `SOURCES` (and create the matching header/source files).

---

## 19. Scope boundary

This companion intentionally teaches only facilities visible in this
repository: its C++98 classes and containers, the listed standard headers,
the listed POSIX socket/poll APIs, and its implemented command flow. It does
not attempt to teach every C++ method, every operating-system interface, or
every IRC command. That boundary keeps each example tied to code a student can
open, search, compile, and trace.
