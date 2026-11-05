# `ft_irc` Bonus: Bot and DCC

This document describes the bonus code that is actually in this repository,
then explains DCC as a possible next feature. The distinction matters:
**the current bonus has a dice bot; it does not implement DCC.**

## Current bonus at a glance

The Makefile builds a second executable, `bot`, from:

- [`bonus/main.cpp`](../bonus/main.cpp)
- [`bonus/Bot.cpp`](../bonus/Bot.cpp)
- [`bonus/Dice.cpp`](../bonus/Dice.cpp)
- the shared IRC parser object from `sources/IRCParser.cpp`

The bot is a separate TCP client process. It connects to the IRC server,
prompts for a password, username, real name, and nickname, sends `PASS`,
`NICK`, and `USER`, then reads IRC lines and dispatches server messages.
On numeric welcome `001` it joins the fixed channel `#game`.

## Bot architecture

```text
bonus/main.cpp
  check address/port arguments, install signal handlers
       |
       v
Bot constructor
  parse port, create socket, connect, configure handlers,
  prompt for user details, send login commands
       |
       v
Bot::run()
  recv bytes --> split on CRLF --> shared IRCParser --> Message
       |
       +--> 001 --> JOIN #game
       |
       +--> PRIVMSG --> look for !roll or !help
                             |
                             +--> Dice parses/runs dice
```

`Bot` handles the connection and command routing. `Dice` parses the
`NdM`-style input, validates the requested dice count and side count, creates
the results, and formats the reply.

## Current bot commands

| User text | Current purpose |
|---|---|
| `!help` | Sends a short list of bot commands to the channel. |
| `!roll 2d6` | Intended to roll two dice with six sides. |
| `!roll` | Help text advertises one 20-sided die, but the current local code rejects this empty argument with a syntax reply instead of rolling it. |

The bot only responds to messages addressed to a channel (`#...`) in
`onPrivmsg()`. It has no configurable channel: the welcome handler joins
`#game`.

## How the bot connects

`bonus/main.cpp` checks that the address is `127.0.0.1` and that the port is
between 1024 and 65535. The `Bot` constructor opens and connects the socket,
then prompts for login details and sends the registration commands.

The order is worth knowing: because the constructor connects before showing
the prompt, the server connection is already open while the user types.

The receive loop in `Bot::run()` collects bytes and processes lines ending in
`\r\n`. The server and bot both use the shared parser, so the bot's handler
key for `PRIVMSG` is lowercase (`privmsg`), matching parser normalization.

## Current dice parsing and limits

The dice parser accepts a dice count from 1 to 10 and a face count from 1 to
20. The intended input shape is `NdM`, such as `2d6`.

The implementation checks that both numbers can be extracted and that the
separator character is `d`. It does not currently verify that the whole
argument was consumed; extra trailing characters may therefore be accepted.
Also, `!roll` with no argument currently fails syntax validation, despite the
help text describing a default `1d20`.

The random number generation uses `srand(time(NULL))` each time dice are
rolled and then `rand() % faces`. This is simple for a learning example, but
rolls started in the same second can repeat the same sequence, and modulo
reduction can make outcomes slightly uneven. It is not suitable for
security-sensitive randomness.

## What is DCC?

**DCC** means Direct Client-to-Client. It is commonly negotiated through a
special CTCP message carried inside an IRC `PRIVMSG`. In a basic DCC file
transfer:

1. One client asks to start a direct transfer and sends connection details
   through the IRC server.
2. The receiving client accepts or rejects the request.
3. The clients connect to each other over a separate TCP connection.
4. The file data travels between the clients instead of through the IRC
   server.

The IRC server usually sees the negotiation text but does not relay the
file's contents. DCC behavior is not a core command in this repository and
is not implemented by its server or bonus bot. DCC conventions also vary
between clients, especially around addresses, ports, passive transfers, and
file-size fields.

## What a DCC extension would need

To add DCC to this project, the work would be larger than adding a bot
command:

- recognize and safely parse the CTCP-framed DCC request inside a message;
- ask the receiving user for explicit approval;
- open a separate TCP connection or listener for the transfer;
- handle partial reads/writes and transfer completion;
- define timeouts, file-name rules, size limits, and cancellation;
- avoid treating untrusted file names as paths to write without validation;
- account for firewall/NAT constraints and failed direct connections.

The ordinary IRC socket remains the control channel; the DCC socket is a
separate data channel. A robust design should keep those roles distinct and
should never accept a transfer silently.

## Other bot ideas

These are examples of possible extensions, not existing project features:

| Bot idea | What it could do |
|---|---|
| Welcome bot | Greet users when they join a configured channel. |
| Trivia bot | Ask questions and track answers in memory. |
| Reminder bot | Send a reminder after a simple timer expires. |
| Channel helper | Show command help or explain channel modes. |
| Moderation helper | Notify operators about configured word or flood rules; any moderation action should be explicit and permission-checked. |
| File-transfer helper | Guide users through an approved DCC request after DCC support exists. |

## Reading the code

- [`bonus/main.cpp`](../bonus/main.cpp) validates command-line arguments and starts the bot.
- [`bonus/Bot.hpp`](../bonus/Bot.hpp) declares bot state and handlers.
- [`bonus/Bot.cpp`](../bonus/Bot.cpp) connects, logs in, parses server replies, and handles `!roll`/`!help`.
- [`bonus/Dice.hpp`](../bonus/Dice.hpp) declares dice state and operations.
- [`bonus/Dice.cpp`](../bonus/Dice.cpp) parses dice input and creates/formats results.
- [`sources/IRCParser.cpp`](../sources/IRCParser.cpp) is the shared line parser used by the bot.
- [`Makefile`](../Makefile) lists the bonus sources and builds `bot`.

## References

- [Modern IRC Documentation](https://modern.ircdocs.horse/) — readable guide to IRC message structure and common client/server behavior.
- [CTCP documentation](https://modern.ircdocs.horse/ctcp.html) — explains CTCP framing used for feature negotiation over IRC messages.
- [RFC 2812](https://datatracker.ietf.org/doc/html/rfc2812) — client/server IRC protocol reference; it does not define this project's dice bot or standardize all DCC variants.

