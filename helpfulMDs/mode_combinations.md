# MODE: combined options, in implementation order

Scope: `MODE <#chan> <modestring> [params...]` for the flags required by the subject: `i`, `t`, `k`, `o`, `l`.
Items marked **(decide)** are choices the RFC leaves open: pick one, then document it.

Notation: `:n!u@h` is the prefix of the operator who sent the command.

---

## Step 0. Ground rules (implement first)

### Layout of the parameters

```
MODE #a +kl parola 10
     |   |   |      |
     |   |   +------+--- params consumed by modes, in letter order
     |   +-------------- params[1] = the ONLY modestring
     +------------------ params[0] = channel
```

Only `params[1]` is a modestring. Everything after it is a parameter for the modes, never a second modestring.
Example: `MODE #a +i +t` applies `+i`; `+t` is just an unused extra parameter and is ignored.

### Order of checks

| # | check | reply |
|---|---|---|
| 1 | registered | 451 |
| 2 | `params.size() >= 1` | 461 |
| 3 | channel exists | 403 |
| 4 | no modestring: this is a query, go to Step 12 | 324 |
| 5 | sender is a member | 442 |
| 6 | sender is an operator | 482 |
| 7 | parse and apply modestring | per flag |

### Which flags consume a parameter

| flag | parameter |
|---|---|
| `+k` | yes (the key) |
| `-k` | no (see the `-k` decision in Step 9) |
| `+l` | yes (the limit) |
| `-l` | no |
| `+o` / `-o` | yes (a nick) |
| `+i` `-i` `+t` `-t` | no |

### Core algorithm

```cpp
bool   adding   = true;
size_t paramIdx = 2;                       // params[0]=channel, params[1]=modestring

for (size_t i = 0; i < modes.size(); ++i) {
    char c = modes[i];
    if (c == '+') { adding = true;  continue ; }
    if (c == '-') { adding = false; continue ; }
    switch (c) {
        case 'i': case 't': /* no parameter */ break ;
        case 'k': case 'l': case 'o':
            /* before consuming: paramIdx < params.size() */ break ;
        default: /* 472, keep going */ break ;
    }
}
```

Design rule: two passes. Pass 1 validates and collects the valid changes. Pass 2 applies them and builds the broadcast. A failure in the middle must never leave the channel half modified.

---

## Step 1. Flags without parameters: `i`, `t`

| input | effect | broadcast |
|---|---|---|
| `+i` / `-i` | invite-only on / off | `+i` / `-i` |
| `+t` / `-t` | topic restricted on / off | `+t` / `-t` |
| `+it` / `+ti` | both on | same order as input |
| `-it` | both off | `-it` |
| `+i-t` | `i` on, `t` off | `+i-t` |
| `-i+t` | `i` off, `t` on | `-i+t` |
| `+ii` | second `i` changes nothing | `+i` |
| `+i` when already `+i` | no change | nothing |
| `-t` when already `-t` | no change | nothing |
| `+it-i` | on, then off | `+it-i` or only `+t` **(decide)** |

Rule: broadcast only effective changes.

---

## Step 2. One flag with a parameter: `k`, `l`

### `k` (key)

| input | effect | broadcast / reply |
|---|---|---|
| `+k parola` | sets the key | `+k parola` |
| `+k parola` when a key exists | rejected | 467 `Channel key already set` |
| `+k` | missing parameter | 461 |
| `+k :` (empty key) | invalid | 461 or ignore |
| `-k` | removes the key | `-k *` (modern) or `-k` |
| `-k` when no key | no change | nothing |

### `l` (limit)

| input | effect | broadcast / reply |
|---|---|---|
| `+l 10` | limit 10 | `+l 10` |
| `+l 10` when limit is 5 | replaces | `+l 10` |
| `+l` | missing parameter | 461 |
| `-l` | removes the limit | `-l` |
| `-l` when no limit | no change | nothing |

---

## Step 3. Operator flag: `o`

| input | effect | broadcast / reply |
|---|---|---|
| `+o bob` | bob becomes operator | `+o bob` |
| `+o bob` when already operator | no change | nothing |
| `-o bob` | bob loses operator | `-o bob` |
| `-o bob` when not operator | no change | nothing |
| `-o ana` (self-demotion) | allowed | `-o ana` |
| `+o` | missing parameter | 461 |
| `+o nobody` | nick does not exist | 401 |
| `+o bob`, bob not in channel | not a member | 441 |
| `+o @bob` | invalid, the real nick is `bob` | 401 |

---

## Step 4. No-parameter flags mixed with one parameter flag

| input | consumption | effect |
|---|---|---|
| `+ik parola` | `k` takes `parola` | `+i`, `+k parola` |
| `+ki parola` | `k` takes `parola` | same, different order in the broadcast |
| `+itk parola` | `k` takes `parola` | three modes |
| `+tl 10` | `l` takes `10` | `+t`, `+l 10` |
| `+io bob` | `o` takes `bob` | `+i`, `+o bob` |
| `+itl 10` | `l` takes `10` | three modes |
| `+ito bob` | `o` takes `bob` | three modes |
| `-ik` | `-k` takes none | `-i`, `-k` |
| `+i-k` | none | `+i`, `-k` |

---

## Step 5. Several parameter flags: consumption order

Parameters are consumed in the order of the letters, not by the position of the sign.

| input | assignment | effect |
|---|---|---|
| `+kl parola 10` | `k`<-`parola`, `l`<-`10` | both |
| `+lk 10 parola` | `l`<-`10`, `k`<-`parola` | same result |
| `+ko parola bob` | `k`<-`parola`, `o`<-`bob` | |
| `+ok bob parola` | `o`<-`bob`, `k`<-`parola` | same result |
| `+lo 10 bob` | `l`<-`10`, `o`<-`bob` | |
| `+klo parola 10 bob` | `k`, `l`, `o` in order | three parameter modes |
| `+iklo parola 10 bob` | `i` takes none | four modes |
| `+itklo parola 10 bob` | `i`, `t` take none | all five flags |
| `+oo bob eve` | `o`<-`bob`, `o`<-`eve` | two operators |
| `+o bob +o eve` | second `+o` and `eve` are extra parameters | only `+o bob` |

Keep one index (`paramIdx`) that advances only when a mode consumes a parameter.

---

## Step 6. Alternating signs

| input | assignment | effect |
|---|---|---|
| `+o-o bob eve` | `+o`<-`bob`, `-o`<-`eve` | bob gains, eve loses |
| `-o+o bob eve` | `-o`<-`bob`, `+o`<-`eve` | reverse |
| `+k-l parola` | `+k`<-`parola`, `-l` none | key set, limit cleared |
| `-k+l 10` | `-k` none, `+l`<-`10` | |
| `+kl-i parola 10` | `k`, `l` take params, `-i` none | |
| `+i-t+k parola` | `k`<-`parola` | three sign changes |
| `+k-k parola` | `+k`<-`parola`, `-k` none | set, then cleared |
| `+l-l 10` | `+l`<-`10`, `-l` none | set, then cleared |
| `+o-o bob bob` | same nick twice | operator, then not |

In the broadcast, keep the sign grouping: `+o-o bob eve`.

---

## Step 7. Repeated modes

| input | behavior |
|---|---|
| `+ii` | second one has no effect |
| `+kk a b` | first sets `a`, second gets 467 |
| `+ll 5 10` | second overwrites with 10, or ignore **(decide)** |
| `+oo bob bob` | second has no effect |
| `+k a -k +k b` | valid sequence: set, clear, set to `b` |

---

## Step 8. Problematic parameter values

| input | behavior |
|---|---|
| `+k +i` | key is the text `+i`, valid |
| `+k -o` | key is the text `-o`, valid |
| `+k :with spaces` | reject **(decide)**: `JOIN` cannot send a key with spaces |
| `+k a,b` | reject **(decide)**: comma splits `JOIN #a,#b k1,k2` |
| `+l abc`, `+l -5`, `+l 1.5`, `+l 10abc` | invalid, ignore (modern: 696). All characters must be digits |
| `+l 0` | ignore, or treat as no limit **(decide)** |
| `+l 99999999999999999999` | check the length before converting, never a bare `atoi` |

---

## Step 9. Missing or extra parameters

| input | behavior |
|---|---|
| `+k` / `+l` / `+o` | 461, the rest of the line still applies |
| `+kl parola` | `k` applied, `l` gets 461 |
| `+ooo bob` | first `o` takes `bob`, the others get 461 or are ignored. Never read outside the vector |
| `+klo a` | `k`<-`a`, `l` and `o` have none |
| `+it extra` | `extra` ignored |
| `+i extra1 extra2` | ignored |

A missing parameter for one mode never cancels the other modes in the same line.

**The `-k` decision.** Some clients send `-k parola`. If `-k` consumes no parameter, then `-ko parola bob` desynchronizes (`o` would take `parola`). Pick one policy and document it. Evaluators usually test only a plain `-k`.

---

## Step 10. Invalid signs and modestrings

| input | behavior |
|---|---|
| `+` / `-` / `+-` | no letters, ignore |
| `++i` | extra `+` ignored, result `+i` |
| `+-i` | current sign becomes `-`, result `-i` |
| `-+i` | result `+i` |
| `i`, `t`, `kl` (no sign) | ignore, or treat as `+`, but never crash |
| `+i+t` | `+i`, `+t` (one modestring) |
| `+ i` (space) | `+` is the modestring, `i` is an ignored parameter |
| `+ik` (no parameter) | `i` applied, `k` gets 461 |
| modestring of 400 characters | process without crashing (the line is bounded by 512 anyway) |

---

## Step 11. Unknown modes mixed with valid ones

| input | behavior |
|---|---|
| `+x` | 472 `x :is unknown mode char to me` |
| `+ix` | `i` applied, `x` gets 472 |
| `+xi` | `x` gets 472, `i` applied |
| `+xyz` | three 472, or one **(decide)** |
| `+I`, `+T`, `+K` | unknown (modes are case-sensitive) |
| `+b` / `b` | clients send it after `JOIN` (ban list): ignore or 368, never crash |
| `+v`, `+m`, `+n`, `+s`, `+p` | not required: 472 or ignore |
| `+ix bob` | `i` applied, `x` gets 472, `bob` unused and ignored |

An unknown mode consumes no parameter and does not stop the rest.

---

## Step 12. Query and permissions

| input | reaction |
|---|---|
| `MODE #a` | 324 with current modes, e.g. `+itkl key 10` (hide the key from non-members **(decide)**) |
| any change by a non-operator | 482, nothing applied |
| any change by a non-member | 442 |
| unknown channel | 403 |
| `MODE bob +i` (nick as target) | ignore or 502 |

Viewing never requires operator rights.

---

## Step 13. Broadcast rules

- Send **one line** with what was applied, not what was requested.
- Parameters appear in the order of their letters: `:n!u@h MODE #a +kl parola 10`.
- Rejected or unchanged flags do not appear.
- If nothing was applied, send nothing.

```
client:  MODE #a +itx-k
server:  :n!u@h MODE #a +it-k          (x rejected, so it is absent)
```

---

## Step 14. Safety limits

| situation | recommendation |
|---|---|
| `+oooooooooooo a b c d e f g h i j k l` | cap at **3 or 4** parameter modes per command (real servers do), ignore the rest |
| broadcast line would exceed 512 bytes | truncate or limit the number of changes |
| key of 400 characters | limit the key length (for example 23) |
| huge `+l` value | reasonable upper bound |
| up to 15 parameters | the lexer accepts them, the handler uses only what it consumes |

---

## Test checklist (send with `nc -C`, with a second client in the channel)

```
MODE #a +it               -> :n!u@h MODE #a +it
MODE #a +itklo k 10 bob   -> :n!u@h MODE #a +itklo k 10 bob
MODE #a +kl cheie 10      -> both applied
MODE #a +lk 10 cheie      -> same result
MODE #a +ix               -> 472 for x, then :n!u@h MODE #a +i
MODE #a +ooo bob          -> +o bob applied, rest without crash
MODE #a +k +i             -> :n!u@h MODE #a +k +i
MODE #a ++i               -> :n!u@h MODE #a +i
MODE #a +l abc            -> ignored, no crash
MODE #a +kl parola        -> k applied, l gets 461
```

Pass condition: no crash, and the broadcast contains only the effective changes.
