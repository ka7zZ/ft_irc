# ft_irc: all commands, inputs, replies and behavior (implementation order)

Scope: everything the subject requires, plus the commands real clients (irssi, HexChat) send on their own, without which they cannot connect.
Numeric codes and texts follow RFC 2812 and modern.ircdocs.horse. Confirm any wording you depend on there.
Items marked **(decide)** are choices the RFC leaves open: pick one, then document it.
`MODE` has its own file (`mode_combinations.md`); here it only gets its place in the order.

Notation:
- `:n!u@h` is the prefix of the sender, built from the `User` (never copied from the incoming line).
- `<nick>` in a reply is `*` while the user has no nick yet.
- Every line ends with `\r\n` and is at most 512 bytes.

---

## Implementation order at a glance

| step | what | why now |
|---|---|---|
| 0 | ground rules, reply helper, output queue | everything depends on it |
| 1 | PASS, NICK, USER, registration | nothing else works before it |
| 2 | CAP, PING, PONG, QUIT | the reference client needs them to connect |
| 3 | Channel class, JOIN, PART, NAMES | PRIVMSG to a channel needs channels |
| 4 | PRIVMSG, NOTICE | the first thing you can see working between two clients |
| 5 | TOPIC | simplest operator-aware command |
| 6 | KICK, INVITE | operator checks and membership edge cases |
| 7 | MODE | uses everything above (`+i`, `+k`, `+l` change `JOIN`; `+t` changes `TOPIC`) |
| 8 | unknown commands, lexer and transport edge cases | robustness |

---

## Step 0. Ground rules

### Order of checks (stop at the first failure)

| # | check | reply |
|---|---|---|
| 1 | command exists | 421 |
| 2 | user registered (if the command requires it) | 451 |
| 3 | enough parameters | 461 |
| 4 | syntax of names (nick, channel) | 432 / 403 / 476 |
| 5 | target exists | 401 / 403 |
| 6 | sender is a member of the channel | 442 |
| 7 | sender is a channel operator | 482 |
| 8 | channel modes (`+i`, `+k`, `+l`) | 473 / 475 / 471 |

Compare nicks and channel names case-insensitively. Never read `params[i]` before checking `params.size()`.

### Which commands work before registration

| command | allowed |
|---|---|
| PASS, NICK, USER, CAP, PING, PONG, QUIT | yes |
| everything else | 451 `:You have not registered`, connection stays open |

Unknown command wins over 451: look up the handler first (421), then check registration (451).

### What a reply is

```
:ircserv <code> <nick> <params> :<text>\r\n
```

One helper builds it (the `Error` class from the earlier discussion). Nothing is sent directly: replies go into the client's output queue, and `send()` runs only after `poll()` reports `POLLOUT`.

### When to disconnect

| situation | disconnect |
|---|---|
| wrong password (464) | yes, after the reply is flushed |
| QUIT | yes, after flush |
| line over 512 bytes, or 512+ bytes without `\n` | yes |
| `recv` returns 0 or -1 | yes |
| every other error code in this file | **no** |

### Data you need before the handlers

| where | what |
|---|---|
| `User` | `passOk`, `hasNick`, `hasUser`, `registered`, output queue, close-after-flush flag |
| `Channel` | name, topic, members, operators, invited list, `+i`/`+t` flags, key, limit |
| `Server` | `sendTo(fd, text)`, `broadcast(channel, text, exceptUser)`, `findUserByNickname`, `findChannel`, `removeChannelIfEmpty` |

---

## Step 1. Registration: PASS, NICK, USER

### PASS `<password>`

| input | reply | behavior |
|---|---|---|
| `PASS secret` (correct) | none | `passOk = true` |
| `PASS wrong` | 464 `:Password incorrect` | flush the reply, then close |
| `PASS` / `PASS :` | 461 `PASS :Not enough parameters` | connection stays |
| `PASS a b` | as `PASS a` | uses the first parameter **(decide)** |
| `PASS :with spaces` | none | the password is `with spaces` (trailing) |
| `PASS secret` after registering | 462 `:Unauthorized command (already registered)` | ignored |

### NICK `<nickname>`

Valid nick: starts with a letter or one of `[ ] \ ` _ ^ { | }`, then letters, digits, `-` or those specials.

| input | reply | behavior |
|---|---|---|
| `NICK ana` (valid, free) | none before registration | sets the nick |
| `NICK` | 431 `:No nickname given` | |
| `NICK 1ana`, `-ana`, `#ana`, `&ana` | 432 `<nick> :Erroneous nickname` | invalid first character |
| `NICK a b` | as `NICK a` | the extra parameter is ignored |
| `NICK a,b`, `a*b`, `a.b`, `a!b`, `a@b` | 432 | forbidden characters |
| `NICK :ana` | none | trailing with one word, valid |
| NUL or bytes above 0x7F in the nick | 432 | |
| 10+ characters | 432 **(decide: RFC max is 9)** | |
| `NICK ana` when taken (any case) | 433 `<nick> :Nickname is already in use` | connection stays, client may retry |
| `NICK ana` when you are already `ana` | none | no change, no broadcast |
| `NICK bob` after registering | broadcast `:ana!u@h NICK bob` | to the user and everyone sharing a channel, each recipient once |
| `NICK bob` without a valid PASS first | 451 | nothing stored (strict policy, see below) |

### USER `<username> <mode> <unused> :<realname>`

| input | reply | behavior |
|---|---|---|
| `USER ana 0 * :Ana P` | none, may complete registration | username = param 1, realname = last param |
| `USER ana 0 * Ana` | same | realname without `:` |
| `USER ana ana ana :Ana P` | same | what real clients send, valid |
| `USER`, `USER ana`, `USER ana 0 *` | 461 `USER :Not enough parameters` | needs 4 parameters |
| `USER ana 0 * :` | 461 **(decide: or accept an empty realname)** | |
| `USER` after registering | 462 | ignored |

### Completing registration

When `passOk && hasNick && hasUser` and the user is not yet `registered`:

```
registered = true
:ircserv 001 <nick> :Welcome to the Internet Relay Network <nick>!<user>@<host>
```

`002`, `003`, `004` are optional (irssi connects with `001` alone).

Rules:
- NICK and USER may come in any order, PASS must come before them.
- Use separate `hasNick` and `hasUser` flags. A single "named" flag lets two `NICK` lines count as a registration.
- Policy for NICK/USER sent before a valid PASS **(decide)**: strict (451, store nothing), or tolerant (store, finish when PASS arrives). Strict follows the RFC.

### Registration cases to test

```
NICK ana                      -> 451 (strict)
PASS wrong                    -> 464, connection closed
PASS secret / NICK ana / USER ana 0 * :Ana     -> 001
PASS secret / USER ... / NICK ana              -> 001 (any order)
JOIN #a before registering    -> 451, connection stays
```

---

## Step 2. Connection commands: CAP, PING, PONG, QUIT

| input | reply | behavior |
|---|---|---|
| `CAP LS`, `CAP LS 302` | `:ircserv CAP * LS :` | empty capability list |
| `CAP REQ :multi-prefix` | `:ircserv CAP * NAK :multi-prefix` | refuse everything |
| `CAP END` | none | ignored |
| `CAP` (no subcommand) | 461 or ignore | |
| `PING tok` / `PING :tok` | `:ircserv PONG ircserv :tok` | echo the token |
| `PING` | 409 `:No origin specified` | |
| `PONG ...` | none | ignored |
| `QUIT` | none (or `ERROR :Closing link`) | broadcast `:n!u@h QUIT :Client Quit` to channel peers, remove from all channels, close after flush |
| `QUIT :reason` | same, with the reason | |

Notes:
- After QUIT, ignore the rest of the lines in the same `recv` (your `getClientBySocket(fd) == NULL` check).
- Delete any channel that becomes empty.
- Do not send the QUIT broadcast to the quitter.
- Removal also happens on `recv == 0`, `POLLHUP` and errors: run the same cleanup (leave all channels, broadcast QUIT with a reason such as `Connection closed`).

---

## Step 3. Channels: JOIN, PART, NAMES

Valid channel name: starts with `#` (optionally `&`), 2 to 51 characters total, no space, comma, `:`, BEL, NUL, CR, LF.

### JOIN `<#chan>[,<#chan>...] [<key>[,<key>...]]`

| input | reply | behavior |
|---|---|---|
| `JOIN #a` (new channel) | success sequence | channel created, joiner becomes operator |
| `JOIN #a` (existing, allowed) | success sequence | joins as a regular member |
| `JOIN` | 461 | |
| `JOIN a`, `JOIN #`, `JOIN :` | 403 `<chan> :No such channel` (or 476) | |
| `JOIN :#a` | as `JOIN #a` | trailing treated as the name |
| `JOIN #a key` | success or 475 | key checked only if the channel has `+k` |
| `JOIN #a,#b` | per channel | each processed on its own |
| `JOIN #a,#b k1,k2` | per channel | keys matched by position |
| `JOIN #a,#b k1` | per channel | only `#a` has a key; check `i < keys.size()` |
| `JOIN #a,,#b` | 403 for the empty item | the others still processed |
| `JOIN #a,` / `JOIN ,#a` | 403 for the empty item | no crash |
| `JOIN #a` when already a member | none | ignored, no duplicate |
| `JOIN #Chan` when `#chan` exists | | same channel (case-insensitive) |
| `JOIN #a b c d` | | uses the first two parameters |
| channel `+i`, user not invited | 473 `<chan> :Cannot join channel (+i)` | |
| channel `+k`, key wrong or missing | 475 `<chan> :Cannot join channel (+k)` | |
| channel `+l`, full | 471 `<chan> :Cannot join channel (+l)` | |
| `JOIN 0` | | leave all channels **(optional)** |

Success sequence (to the joiner, plus the first line to every member):

```
:n!u@h JOIN #a                          -> to every member, including the joiner
:ircserv 332 <nick> #a :<topic>         -> or 331 <nick> #a :No topic is set
:ircserv 353 <nick> = #a :@op user1     -> member list, @ marks operators
:ircserv 366 <nick> #a :End of NAMES list
```

After a successful join, remove the user from the channel's invited list.

### PART `<#chan>[,<#chan>...] [:reason]`

| input | reply | behavior |
|---|---|---|
| `PART #a` / `PART #a :bye` | none | broadcast `:n!u@h PART #a [:bye]` to members, including the leaver, then remove |
| `PART` | 461 | |
| `PART #nochan` | 403 | |
| `PART #a` when not a member | 442 `<chan> :You're not on that channel` | |

- Delete the channel when it becomes empty.
- If the last operator leaves while others remain **(decide)**: promote someone, or leave the channel without an operator.

### NAMES `[<#chan>]`

| input | reply |
|---|---|
| `NAMES #a` | 353 + 366 |
| `NAMES #nochan` | 366 only (as real servers do) |
| `NAMES` | optional: 366 with `*`, or ignore |

---

## Step 4. Messages: PRIVMSG, NOTICE

### PRIVMSG `<target>[,<target>...] :<text>`

| input | reply | behavior |
|---|---|---|
| `PRIVMSG bob :hi` | none | to bob: `:n!u@h PRIVMSG bob :hi` |
| `PRIVMSG #a :hi` | none | to all members **except the sender** |
| `PRIVMSG bob,#a :hi` | none | each target gets one copy |
| `PRIVMSG bob,bob :hi` | none | deliver once or twice **(decide)**, no crash |
| `PRIVMSG bob hi` | none | sends `hi` (without `:` only the first word) |
| `PRIVMSG bob hi there` | none | sends `hi` only |
| `PRIVMSG`, `PRIVMSG :hi` | 411 `:No recipient given (PRIVMSG)` | |
| `PRIVMSG bob`, `PRIVMSG bob :` | 412 `:No text to send` | |
| `PRIVMSG nobody :hi` | 401 `<nick> :No such nick/channel` | |
| `PRIVMSG #nochan :hi` | 403 (or 401) | |
| `PRIVMSG #a :hi`, sender not a member | 404 `<chan> :Cannot send to channel` | **(decide)** |
| `PRIVMSG ana :hi` (to yourself) | none | delivered, no crash |
| `PRIVMSG bob ::)` | none | text is `:)` |
| `PRIVMSG bob :   ` | none | text of spaces only, usually delivered |
| text with `\x01` (CTCP), bytes above 0x7F, NUL | none | forward unchanged, do not use `c_str()` |
| lowercase `privmsg` | | uppercase the command first |

Length: with a prefix of about 60 bytes, a forwarded line can exceed 512. Truncate the text so the whole line is at most 510 bytes plus `\r\n`.

Decide the target type by its first character: `#` (or `&`) means channel, otherwise a nick. This is why nicks cannot start with `#`.

### NOTICE `<target> :<text>`

Same as PRIVMSG, but it never produces error replies (no 401, 403, 404, 411, 412).

---

## Step 5. TOPIC `<#chan> [:<topic>]`

| input | reply | behavior |
|---|---|---|
| `TOPIC #a` | 332 `<chan> :<topic>` or 331 `<chan> :No topic is set` | view; `333` (who set it, when) is optional |
| `TOPIC #a :new` | none | set, broadcast `:n!u@h TOPIC #a :new` to all members |
| `TOPIC #a :` | none | **clears** the topic (different from `TOPIC #a`) |
| `TOPIC #a word` | none | sets `word` (without `:` only the first word) |
| `TOPIC #a ::)` | none | topic is `:)` |
| `TOPIC` | 461 | |
| `TOPIC #nochan` | 403 | |
| sender not in `#a` | 442 | applies to view and set |
| `+t` and sender not operator, when setting | 482 | viewing is always allowed |
| `-t`, regular member sets | none | allowed |
| topic over 307 characters | | truncate or reject **(decide)** |

The distinction between view and clear is the presence of the trailing token, not its content. Check that the parameter exists, not that it is non-empty.

---

## Step 6. Operator commands: KICK, INVITE

### KICK `<#chan>[,<#chan>...] <nick>[,<nick>...] [:reason]`

| input | reply | behavior |
|---|---|---|
| `KICK #a bob :reason` | none | broadcast `:op!u@h KICK #a bob :reason` to all members, **including bob**, then remove bob |
| `KICK #a bob` | none | reason defaults to the kicker's nick |
| `KICK`, `KICK #a`, `KICK bob` | 461 | |
| `KICK bob #a` (swapped) | 403 | `bob` is read as a channel |
| `KICK #nochan bob` | 403 | |
| sender not in `#a` | 442 | |
| sender not operator | 482 `<chan> :You're not channel operator` | |
| `bob` not in `#a` | 441 `<nick> <chan> :They aren't on that channel` | |
| `bob` does not exist | 401 or 441 **(decide)** | |
| `KICK #a ana` (yourself) | none | allowed; then handle the channel becoming empty |
| `KICK #a bob,eve` | none | each target handled separately |
| `KICK #a,#b bob,eve` | none | pairs by position **(decide)** |
| `KICK #a bob,` | | empty item ignored, no crash |

### INVITE `<nick> <#chan>` (user first, channel second)

| input | reply | behavior |
|---|---|---|
| `INVITE bob #a` | 341 `<nick> bob #a` to the inviter | send `:op!u@h INVITE bob #a` to bob, add bob to the invited list |
| `INVITE`, `INVITE bob` | 461 | |
| `INVITE nobody #a` | 401 | |
| `INVITE #a bob` (swapped) | 401 | `#a` is read as a nick |
| `INVITE bob #nochan` | 403 | **(decide)**, the RFC allows nonexistent channels |
| sender not in `#a` | 442 | |
| `#a` is `+i`, sender not operator | 482 | needed only when `+i` |
| `bob` already in `#a` | 443 `<nick> <chan> :is already on channel` | |
| second invite for the same user | none | no duplicate in the list |
| `INVITE ana #a` (yourself) | 443 if in the channel | |
| `INVITE bob #a,#b` | | one channel accepted, no crash |

The invitation is stored per channel (a list of nicks or fds) and consumed when the invited user joins. If an invited user disconnects, remove the stale entry.

---

## Step 7. MODE `<#chan> [<modestring> [<params>...]]`

Full case list: see `mode_combinations.md`. Summary of what must exist before it:

| flag | needs |
|---|---|
| `i` | `Channel::_inviteOnly`, checked by JOIN (473) and INVITE (482) |
| `t` | `Channel::_topicRestricted`, checked by TOPIC (482) |
| `k` | `Channel::_key`, checked by JOIN (475) |
| `o` | operator set, checked by KICK, INVITE, TOPIC, MODE (482) |
| `l` | `Channel::_limit`, checked by JOIN (471) |

Minimum behavior: `MODE #a` returns 324, a non-operator gets 482, flags combine in one modestring, parameters are consumed in letter order, and the broadcast contains only the changes actually applied.

---

## Step 8. Unknown commands, lexer and transport edge cases

### Unknown or malformed command lines

| input | reply | behavior |
|---|---|---|
| `FOO bar`, `PR1VMSG x`, `433`, `123` | 421 `<cmd> :Unknown command` | connection stays |
| `NICKana` (no space) | 421 | the whole word is the command |
| `NICK\tana` (tab) | 421 | only space separates tokens |
| empty line, only spaces | none | ignore |
| prefix only (`:ana`), `:` alone | none | no command, ignore |
| `: PING x` (empty prefix) | treated as `PING x` | |
| `  :ana NICK x` (spaces, then prefix) | | skip spaces **before** testing for the prefix |
| `:ana!u@h NICK x` (client sends a prefix) | | ignore the prefix, identity comes from the fd |
| 15+ parameters | | the lexer accepts them, the handler uses only what it needs |

### Optional commands clients send on their own

| input | behavior |
|---|---|
| `WHO #a`, `WHOIS bob`, `LIST`, `USERHOST`, `MODE <nick>` | optional. A 421 or an empty end-of-list reply is acceptable, a crash is not |
| `MODE #a b` / `+b` | clients send it after JOIN (ban list): ignore or 368 |

### Transport cases (the "never crash" requirement)

| case | behavior |
|---|---|
| command split across several `recv` (`com^Dman^Dd`) | buffer and reassemble |
| several commands in one `recv` | process in order |
| commands after `QUIT` in the same `recv` | ignored |
| `\n` only (as `nc` sends) | accepted, strip an optional `\r` |
| line over 512 bytes with `\n` | disconnect |
| 512+ bytes without `\n` | disconnect (`exceedsPendingLimit`) |
| client disconnects mid-command | remove from users **and from every channel**, delete empty channels |
| client disconnects during a multi-channel `JOIN` | no pointers to a removed user left in channels |
| exception (`bad_alloc`, `out_of_range`) in a handler | catch per client, disconnect that client, server keeps running |
| 100 simultaneous clients | no leaked fds |

### Typical crash causes to guard against

| cause | example input |
|---|---|
| `params[i]` without a size check | `KICK #a`, `USER ana`, `MODE #a +k`, `INVITE bob` |
| `tokens[0]` on an empty or spaces-only line | `     ` |
| `substr` / `erase` with `npos` | `JOIN #a,`, a nick with no separator |
| `atoi` / `stoi` without validation | `MODE #a +l 99999999999999999999` |
| dangling `User*` after QUIT or self-kick | `QUIT` followed by more lines in the same `recv` |
| dangling `Channel*` after it is deleted | `KICK #a ana` as the last member, `PART` as the last member |
| iterating a `map` while erasing from it | broadcasting to a channel when a member disconnects |
| `operator[]` on a map for a missing key | `_users[fd]`, `_channels[name]` create phantom entries; use `find` |
| `c_str()` on a string containing NUL | `PRIVMSG bob :a\x00b` |
| nick comparison that is case-sensitive | `Ana` and `ana` as two users |

---

## Numeric reply reference

| code | name | text |
|---|---|---|
| 001 | RPL_WELCOME | `:Welcome to the Internet Relay Network <nick>!<user>@<host>` |
| 324 | RPL_CHANNELMODEIS | `<chan> +<modes> [params]` |
| 331 | RPL_NOTOPIC | `<chan> :No topic is set` |
| 332 | RPL_TOPIC | `<chan> :<topic>` |
| 341 | RPL_INVITING | `<nick> <chan>` (modern order after the client nick) |
| 353 | RPL_NAMREPLY | `= <chan> :@op user1 user2` |
| 366 | RPL_ENDOFNAMES | `<chan> :End of NAMES list` |
| 401 | ERR_NOSUCHNICK | `<nick> :No such nick/channel` |
| 403 | ERR_NOSUCHCHANNEL | `<chan> :No such channel` |
| 404 | ERR_CANNOTSENDTOCHAN | `<chan> :Cannot send to channel` |
| 409 | ERR_NOORIGIN | `:No origin specified` |
| 411 | ERR_NORECIPIENT | `:No recipient given (<command>)` |
| 412 | ERR_NOTEXTTOSEND | `:No text to send` |
| 421 | ERR_UNKNOWNCOMMAND | `<command> :Unknown command` |
| 431 | ERR_NONICKNAMEGIVEN | `:No nickname given` |
| 432 | ERR_ERRONEUSNICKNAME | `<nick> :Erroneous nickname` |
| 433 | ERR_NICKNAMEINUSE | `<nick> :Nickname is already in use` |
| 441 | ERR_USERNOTINCHANNEL | `<nick> <chan> :They aren't on that channel` |
| 442 | ERR_NOTONCHANNEL | `<chan> :You're not on that channel` |
| 443 | ERR_USERONCHANNEL | `<nick> <chan> :is already on channel` |
| 451 | ERR_NOTREGISTERED | `:You have not registered` |
| 461 | ERR_NEEDMOREPARAMS | `<command> :Not enough parameters` |
| 462 | ERR_ALREADYREGISTRED | `:Unauthorized command (already registered)` |
| 464 | ERR_PASSWDMISMATCH | `:Password incorrect` |
| 467 | ERR_KEYSET | `<chan> :Channel key already set` |
| 471 | ERR_CHANNELISFULL | `<chan> :Cannot join channel (+l)` |
| 472 | ERR_UNKNOWNMODE | `<char> :is unknown mode char to me` |
| 473 | ERR_INVITEONLYCHAN | `<chan> :Cannot join channel (+i)` |
| 475 | ERR_BADCHANNELKEY | `<chan> :Cannot join channel (+k)` |
| 476 | ERR_BADCHANMASK | `<chan> :Bad Channel Mask` |
| 482 | ERR_CHANOPRIVSNEEDED | `<chan> :You're not channel operator` |
| 696 | ERR_INVALIDMODEPARAM | `<chan> <mode> <param> :<reason>` |

---

## Final test script (send with `nc -C localhost 6667`, second client for broadcasts)

```
# registration
NICK ana                         -> 451
PASS wrong                       -> 464, closed
PASS secret / NICK ana / USER ana 0 * :Ana    -> 001

# channels
JOIN a                           -> 403
JOIN #a,,#b                      -> joins #a and #b, 403 for the empty item
PART #nochan                     -> 403

# messages
PRIVMSG                          -> 411
PRIVMSG bob                      -> 412
PRIVMSG bob ::)                  -> bob receives ":)"
PRIVMSG #a :hi                   -> other members receive it, the sender does not

# operator commands
KICK #a                          -> 461
KICK bob #a                      -> 403
INVITE #a bob                    -> 401
TOPIC #a :                       -> topic cleared (not just viewed)

# robustness
(spaces only)                    -> nothing, no crash
FOO bar                          -> 421
com^Dman^Dd                      -> processed as one command
```

Pass condition: the server stays alive after every line, every reply uses the right code, and the connection closes only in the cases listed under "When to disconnect".
