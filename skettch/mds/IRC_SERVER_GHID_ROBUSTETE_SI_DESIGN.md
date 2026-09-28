# Serverul IRC în C++98: robustețe, cazuri-limită, design și lucru în echipă

> **Ghid unic**, rezultat din fuziunea a șase documente scrise de doi colegi
> (audituri de robustețe, catalog de cazuri-limită, consultări de design,
> comparația celor două demo-uri). Nu aplică modificări în surse. Conține
> analiza, soluțiile propuse (fragmente C++98), testele necesare și regulile
> de lucru în echipă.
>
> **Pentru cine este:** dezvoltatori juniori care intră într-un proiect de
> comunicații de rețea (TCP/IP, socket-uri, procese). Fiecare problemă este
> explicată în ordinea: *ce se întâmplă → de ce crapă → cum se repară → cum se testează*.
>
> **Convenții:** cod compatibil `-std=c++98 -Wall -Wextra -Werror` (fără
> `nullptr`, `unique_ptr`, lambda, move). Cazurile au ID-uri stabile
> (`T1…`, `L1…`, `P1…`) și sunt mapate la documentele originale în Anexa B.

---

## Cuprins

1. Rezumat executiv
2. Starea actuală: cele două demo-uri
3. Noțiuni de bază
4. Cum se caută cazurile-limită
5. Arhitectura țintă
6. Catalogul cazurilor-limită
7. Scheletul integrat al event loop-ului
8. Ordinea de implementare
9. Teste și instrumente
10. Lucrul în echipă (două persoane, un repository)
11. Catalog de algoritmi și pattern-uri (C++98 și modern)
12. Surse de documentare
- Anexa A: Erori și ambiguități corectate față de documentele sursă
- Anexa B: Trasabilitate (document original → secțiune)

---

## 1. Rezumat executiv

Prototipul are o separare conceptuală bună (socket, utilizator, lexer, AST,
dispatcher), dar **transportul TCP este fragil**. Problemele prioritare:

| # | Problemă | Consecință |
|---|---|---|
| 1 | Un `recv()` tratat ca o comandă completă | comenzi tăiate/lipite, interpretate greșit |
| 2 | `send()` presupus complet | mesaje pierdute tăcut |
| 3 | `SIGPIPE` neprotejat | procesul moare la un peer închis |
| 4 | `User` nu eliberează socketul | leak de memorie și de fd la fiecare client |
| 5 | `pollfd`/liste modificate în timpul parcurgerii | use-after-free, crash |
| 6 | Handler de semnal cu STL/`cout` | comportament nedefinit |
| 7 | Fără limite de input/output/clienți | memorie nelimitată, busy-loop |
| 8 | Nickname fără validare/unicitate | stare inconsistentă |
| 9 | Parser care nu raportează inputul invalid | AST aparent valid din input rupt |

**Obiectivul realist:** un client defect sau ostil poate fi deconectat
controlat, fără să afecteze ceilalți clienți și fără crash. Serverul nu
promite că acceptă orice input; promite că îl respinge controlat și că un
client nu poate să corupă memoria, să blocheze ceilalți, să consume memorie
nelimitat, să piardă tăcut output valid, să termine procesul (`SIGPIPE`) sau
să lase fd-uri/obiecte în urmă.

### Cele cinci principii care rezolvă aproape tot catalogul

1. **Nu modifica o colecție cât o parcurgi.** Marchezi, apoi aplici după (`T7`, `L2`, `L4`, `L6`).
2. **Un obiect care trăiește în mai multe colecții are un singur punct de „moarte”**, cu ordine fixă a pașilor (`L3`).
3. **Nu păstra pointeri peste mai mulți pași.** Re-caută prin identificator stabil (nume, fd) și tratează „nu mai există” ca pe un caz normal (`L5`, `L6`).
4. **Orice input extern este validat explicit**: rețea, argumente de pornire, parametri numerici (`P1`, `P5`, `P6`).
5. **Eroarea unui participant nu doboară restul sistemului**: nici alt client, nici tot procesul (`T2`, `L5`, `P2`).

---

## 2. Starea actuală: cele două demo-uri

Există două direcții de implementare, fiecare cu puncte tari complementare.
(Auditul `ANALIZA_ROBUSTETE…` se referă la un al treilea folder, `skettch`, cu
aceeași structură modulară ca `another_demo`: `User`, `Socket`, `Server`,
`CommandDispatcher`. Concluziile pentru `another_demo` se aplică și acolo;
diferențele de fișier trebuie confirmate de echipă.)

| Criteriu | `demo_sources` (integrat) | `another_demo` (modular) |
|---|---|---|
| Arhitectură | Monolitic: totul în `Server.cpp` | Lexer → ASTParser → CommandDispatcher |
| I/O | `poll()`, listă refăcută la fiecare iterație | `poll()`, vector persistent de `pollfd` |
| Framing TCP | **Da**: buffer per socket, `\r\n`, limite 4096/512 | **Nu**: un `recv()` = o comandă |
| Scriere | Buffer de ieșire, `send()` parțial, `MSG_NOSIGNAL`; **fără limită** | Rezultatul `send()` ignorat; fără protecție `SIGPIPE` |
| Protocol | `NICK`, `USER`, `JOIN`, `PING`, numerice `001–004`, `353/366` | `NICK`, `USER`, `PING` (`PONG:` nestandard), `PRIVMSG` parțial |
| Canale | Da (membri, operator inițial, `NAMES`); **canalele goale rămân în map** | Nu există |
| Validare | Nick, canal, nr. parametri, stare de înregistrare | Minimă; comenzile sunt case-sensitive (`nick` nu e recunoscut) |
| Ownership | Pointeri raw, dar ciclu coerent | `User::~User()` nu șterge socketul → leak; `Socket` copiabil → risc double-close |
| Build strict | Compilează curat | **Nu**: parametri `server` neutilizați + `-Werror` |
| Testabilitate | Cuplare mare la rețea | `ISocket` permite mock-uri |
| Extensibilitate | Greu (lanț de `if`) | Bună (tabel de handler-e) |

**Probleme specifice `demo_sources`:** schimbarea de nickname poate
desincroniza indexurile; output nelimitat; erorile `accept()` diferite de
`EAGAIN` sunt ignorate; `atoi` pentru port; `JOIN` doar pentru un canal;
membri publici în `User`/`Channel`; case-folding aproximativ.

**Probleme specifice `another_demo`:** framing lipsă; `send()` ignorat;
`SIGPIPE`; leak de socket; handler de semnal nesigur; `POLLERR/HUP/NVAL`
netratate; `NICK` acceptă orice; `USER` incomplet; `PRIVMSG` către canal
neimplementat; comenzi necunoscute fără răspuns numeric.

### Verdict

- **Pentru o demonstrație imediată:** `demo_sources`.
- **Pentru dezvoltarea pe termen lung:** structura modulară din `another_demo`, dar numai după repararea fundației.
- **Rețeta recomandată:** *transportul robust și modelul de canale din `demo_sources` + Lexer/Parser/Dispatcher din `another_demo` + ownership explicit + teste de protocol.*

---

## 3. Noțiuni de bază

- **File descriptor (fd):** număr întreg prin care kernelul identifică un socket/fișier/pipe. Trebuie închis **exact o dată**: zero ori = leak, de două ori = crash sau închiderea unui fd al altcuiva.
- **Socket de listening vs. socket de client:** cel de listening nu transportă mesaje IRC, doar anunță prin `accept()` că a apărut o conexiune. Fiecare client primește propriul fd.
  ```text
  socket → setsockopt(SO_REUSEADDR) → bind → listen → accept
  recv/send … → close
  ```
- **`recv()`/`send()`** lucrează cu octeți, nu cu „comenzi”. Ambele sunt **parțiale**: `recv()` poate da mai puțin decât există, `send()` poate scrie mai puțin decât ai cerut.
- **TCP este un flux, nu o secvență de pachete.** Clientul poate trimite `PI` + `NG :abc\r\nPING :xyz\r\n` iar serverul poate primi aceleași date în orice fragmentare. Doar `\r\n` marchează sfârșitul unei comenzi IRC.
- **`poll()`** nu citește și nu scrie. Răspunde la: „pe ce fd merită să încerc acum o operație fără să blochez?”. `POLLIN` → se poate încerca `recv()`; `POLLOUT` → se poate încerca `send()`.
- **IPC:** mecanismul prin care procese separate schimbă date; un socket TCP este un canal IPC.
- **Iterator invalidation:** modifici un `vector`/`map` cât îl parcurgi și iteratorii/indicii devin invalizi → UB (uneori crash imediat, uneori mult mai târziu).
- **Use-after-free / dangling pointer:** folosești un pointer către un obiect deja `delete`-uit.
- **Async-signal-safe:** funcție apelabilă în siguranță dintr-un signal handler. `new`, `malloc`, `std::cout`, orice metodă STL **nu** sunt.
- **Backpressure:** când destinatarul nu consumă, producătorul trebuie să se oprească sau să fie deconectat, nu să acumuleze la nesfârșit.
- **Level-triggered:** `poll()` raportează un fd ca „gata” **cât timp** condiția persistă, nu doar la schimbare. Consecința: dacă nu consumi evenimentul, `poll()` revine imediat, la infinit (busy-loop). Reține, este cheia lui `T6`.

---

## 4. Cum se caută cazurile-limită

Pentru fiecare funcție care primește date din exterior (client, `argv`, alt server), pune aceleași șase întrebări:

1. Ce se întâmplă dacă **lipsesc date** (parametru absent, buffer gol)?
2. Ce se întâmplă dacă sunt **prea multe** (linie uriașă, mii de conexiuni, mesaj repetat la infinit)?
3. Ce se întâmplă dacă sunt **malformate** (octeți invalizi, NUL, numere în afara plajei, text în loc de cifre)?
4. Ce se întâmplă dacă operația reușește **doar parțial** (`send`/`recv`)?
5. Ce se întâmplă dacă **două evenimente se suprapun** (clientul pleacă în mijlocul procesării altei comenzi de-a lui; semnal în mijlocul unei modificări de container)?
6. Cine este **proprietarul** resursei și cine are voie să o șteargă?

---

## 5. Arhitectura țintă

### 5.1 Graful de ownership

Regula de aur: **un singur proprietar pentru fiecare resursă; nu se amestecă modelul „Server șterge” cu „User șterge”.**

```text
Server
 ├── Listener (fd de listening, non-blocking)
 ├── std::map<int, User*>   ← Server deține User
 │      └── User            ← User deține Connection
 │            └── Connection ← deține Fd (RAII), input buffer, output queue
 ├── UserRegistry (nick normalizat → User*, NON-owning)
 ├── ChannelRegistry (nume normalizat → Channel*, Server deține Channel)
 └── CommandDispatcher (nume comandă → handler)
```

**Varianta țintă (curată):** transportul și identitatea IRC separate.

```text
ClientConnection: socket, input buffer, output queue, stare transport, deadline
UserSession:      nickname, username, realname, hostname, stare autentificare, canale
```

**Varianta pragmatică (faza 1):** păstrezi clasa `User`, dar declari explicit
că deține `Connection`, buffer-ele, starea de înregistrare, canalele și starea
de închidere. Codul din acest ghid folosește varianta pragmatică
(`user.connection()`); refactorizarea spre varianta țintă este ulterioară.

### 5.2 Responsabilități

| Componentă | Face | **Nu** face |
|---|---|---|
| `Server` (Facade + Mediator) | config listener, event loop, coordonare registry-uri, shutdown ordonat | nu parsează linii, nu apelează `send()`, nu validează nickname-uri |
| `Listener` | `socket/setsockopt/bind/listen`, non-blocking, `accept()` până la `EAGAIN` | nicio logică IRC |
| `Fd` / `Socket` | singurul proprietar al fd-ului (`fd>=0` ⇒ închide) | nu se copiază |
| `Connection` | fd, `appendInput/nextLine` (framing), `queue/flush` (output) | nu tokenizează, nu execută comenzi |
| `IrcParser` | linie completă → `ParseResult{ok,error,…}` | nu atinge socketul |
| `CommandDispatcher` | normalizează comanda, verifică starea de înregistrare, deleagă, răspunde **prin `queue()`** | nu apelează `send()` direct |
| `UserRegistry` | unicitate, rezervare atomică, redenumire, eliminare | – |
| `ChannelRegistry`/`Channel` | membri, broadcast, curățarea canalelor goale | – |

### 5.3 Contracte între componente

```cpp
// Transportul produce numai linii IRC complete, fără "\r\n".
Connection::FrameStatus Connection::nextLine(std::string& line);
// Protocolul nu apelează send(): pune răspunsul în coadă.
bool Connection::queue(const std::string& message);
```

Contractele previn cele două erori clasice: parserul care presupune că un
`recv()` e o comandă completă și dispatcher-ul care presupune că `send()`
trimite tot mesajul.

### 5.4 Ciclul de viață (FSM)

```text
Accepted → Connected → Unregistered (PASS/NICK/USER) → Registered → Closing → Closed
```

Există **două feluri de închidere**:

- `markForRemoval(fd)`: eroare/EOF/`POLLERR`. Șterge la finalul iterației, fără să mai scrie.
- `markForClosing(fd)`: `QUIT`, input prea lung. Oprește citirea, încearcă să golească coada de output (ex. mesajul de adio), apoi șterge; cu **deadline** ca un client care nu citește să nu rămână în `Closing` la infinit.

Ambele sunt **idempotente** (două evenimente de închidere pentru același fd nu produc double-delete). Ordinea cleanup-ului este în `L3`.

### 5.5 Invarianți de verificat în cod și în review

```text
fiecare fd activ apare o singură dată în poll set
users_by_nick[normalize(user.nick)] == &user
user.channels conține channel  <=>  channel.members conține user
_input.size() <= MAX_PENDING_INPUT ; _outputBytes <= MAX_OUTPUT_BYTES
nicio ștergere de User/Channel în timpul parcurgerii unei colecții
```

### 5.6 Pattern-uri: obligatorii vs. fază a doua

Sfatul consultantului: *nu alege un pattern pentru că „sună bine”; alege-l
pentru problema exactă din audit.* Într-un proiect mic, supra-ingineria e un risc.

**Obligatorii, în această ordine:**

| Pattern | Unde | Rezolvă |
|---|---|---|
| RAII + non-copyable + Rule of Three | `Fd`, `Connection`, `Socket` | ownership fd, dublu-close, leak (`L1`) |
| Registry (index) | `UserRegistry`, `ChannelRegistry` | unicitate nick/canal, redenumire atomică (`P4`) |
| State (FSM) | înregistrarea: `CONNECTED → PASS_OK → NICK_SET → USER_SET → REGISTERED` | comenzi în ordine greșită (`JOIN` înainte de `NICK`) |
| Command | fiecare comandă IRC un handler, dispatcher = `map<nume, handler>` | `if/else` gigantic |

**Fază a doua (utile mai ales pentru teste unitare fără TCP real):**
Facade (`Server`), Mediator (`Server` între `User` și `Channel`), Strategy
(validare per comandă), Factory Method (numerice IRC: `makeNumericError`,
`makeWelcome`), Observer (broadcast), Adapter + Dependency Injection
(`ISocket` mock-abil), Non-Virtual Interface, Named Constructor
(`Connection::fromAcceptedSocket`), Null Object (`findChannel()==0`).

---

## 6. Catalogul cazurilor-limită

### 6.0 Taxonomie

| Categorie | Simptom tipic | Cazuri |
|---|---|---|
| Transport / I/O | mesaje pierdute, blocare, busy-loop, crash la peer închis | `T1`–`T8` |
| Lifecycle și memorie | `SIGSEGV`, `SIGABRT`, leak de fd | `L1`–`L6` |
| Protocol și input | UB, `terminate()`, stare inconsistentă | `P1`–`P6` |

### 6.1 Constante comune (un singur loc, `Limits.hpp`)

```cpp
static const std::size_t MAX_IRC_LINE      = 512;    // inclusiv "\r\n" (RFC 2812)
static const std::size_t MAX_PENDING_INPUT = 4096;   // buffer de intrare per conexiune
static const std::size_t MAX_OUTPUT_BYTES  = 65536;  // buffer de ieșire per conexiune
static const std::size_t MAX_CLIENTS       = 512;    // < (ulimit -n) - rezervate, vezi T6
static const std::size_t MAX_CHANNEL_MEMBERS = 200;
static const int         MAX_LINES_PER_TURN  = 32;   // echitate între clienți
```

---

## Transport și I/O

### T1. Framing TCP: un `recv()` nu este o comandă

**Problemă.** `handleClientData()` din `another_demo` dă blocul primit direct lexerului.
**Consecințe.** Comenzi fragmentate respinse sau interpretate greșit; două comenzi într-un bloc devin una; inputul fără `\r\n` nu are comportament clar. În plus, `std::string(buffer)` după `recv()` trunchiază la primul NUL, pentru că lungimea validă este valoarea întoarsă de `recv()`.
**Soluție.** Buffer persistent per conexiune. `recv` adaugă octeți, parserul primește **doar linii complete**.

```cpp
// Connection.hpp (extras)
class Connection
{
public:
    enum FrameStatus { FRAME_NEED_MORE, FRAME_LINE, FRAME_TOO_LONG };
    enum IoStatus    { IO_DONE, IO_WOULD_BLOCK, IO_ERROR };

    explicit Connection(int fd) : _fd(fd), _outputBytes(0) {}

    int  fd() const { return _fd.get(); }
    std::size_t inputSpace() const { return MAX_PENDING_INPUT - _input.size(); }
    bool appendInput(const char* data, std::size_t size);
    FrameStatus nextLine(std::string& line);
    bool queue(const std::string& message);   // false => limită depășită
    IoStatus flush();
    bool wantsWrite() const { return _outputBytes > 0; }

private:
    Fd _fd;                                   // RAII, vezi L1
    std::string _input;
    std::deque<std::string> _output;
    std::size_t _outputBytes;

    Connection(const Connection&);            // non-copyable (C++98)
    Connection& operator=(const Connection&);
};
```

```cpp
bool Connection::appendInput(const char* data, std::size_t size)
{
    if (size > MAX_PENDING_INPUT - _input.size())   // fără overflow aritmetic
        return false;
    _input.append(data, size);
    return true;
}

Connection::FrameStatus Connection::nextLine(std::string& line)
{
    std::string::size_type end = _input.find("\r\n");
    if (end == std::string::npos)
        // Fără terminator și deja >= 512 octeți: linia nu mai poate fi validă.
        return (_input.size() >= MAX_IRC_LINE) ? FRAME_TOO_LONG : FRAME_NEED_MORE;
    if (end + 2 > MAX_IRC_LINE)
        return FRAME_TOO_LONG;
    line.assign(_input, 0, end);
    _input.erase(0, end + 2);
    return FRAME_LINE;
}
```

Am folosit un **rezultat cu trei stări**, nu `bool`: „nu am linie încă”
(normal) trebuie distins de „linie prea lungă” (eroare) (vezi Anexa A, #1).

> **Decizie de documentat: `\r\n` strict sau tolerant?** RFC cere `\r\n`, dar
> `nc`/`telnet` de test trimit adesea doar `\n`. Multe servere acceptă `\n` și
> îndepărtează un eventual `\r` dinaintea lui. Alegeți explicit și scrieți în
> `Server.hpp`; testele din §9 presupun comportamentul ales.

### T2. Linie fără terminator: epuizare de memorie printr-un singur socket

**Scenariu.** Un client trimite octeți la infinit, fără `\r\n`.
**Riscul real** nu este doar bufferul, ci **ce se întâmplă când limita e depășită**: dacă reacția e un `throw` care scapă necaptat, un singur client oprește tot serverul. Mai rău: dacă excepția este prinsă doar în bucla principală și **starea clientului rămâne neschimbată**, același buffer produce aceeași excepție la fiecare iterație (*poison pill*: log infinit + CPU 100%).

**Soluție.** Excepția/eroarea se tratează **la granița clientului** și clientul este marcat pentru închidere în același loc.

```cpp
class ProtocolError : public std::runtime_error
{
public:
    explicit ProtocolError(const std::string& m) : std::runtime_error(m) {}
};

void Server::readClient(User& user)
{
    Connection& conn = user.connection();
    std::size_t room = conn.inputSpace();
    if (room == 0)                       // recv(len=0) întoarce 0 = fals EOF!
        return;

    char buffer[4096];
    ssize_t count = ::recv(conn.fd(), buffer,
                           room < sizeof(buffer) ? room : sizeof(buffer), 0);
    if (count == 0)  { markForRemoval(conn.fd()); return; }          // EOF ordonat
    if (count < 0)
    {
        if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
            return;                                                   // T5
        markForRemoval(conn.fd());
        return;
    }
    try
    {
        if (!conn.appendInput(buffer, static_cast<std::size_t>(count)))
            throw ProtocolError("input buffer overflow");
        processLines(user);               // poate arunca ProtocolError (T1: FRAME_TOO_LONG)
    }
    catch (const ProtocolError&)
    {
        conn.queue(makeNumericError(417, "Input line was too long")); // 417 = extensie modernă
        markForClosing(conn.fd());       // din acest moment nu mai citim de la el
    }
    catch (const std::bad_alloc&)
    {
        markForRemoval(conn.fd());
    }
}
```

**Regula generală:** o eroare provocată de un client se tratează la granița
acelui client (`readClient`), niciodată lăsată să urce până în bucla
principală. Aceasta rămâne doar plasa finală pentru bug-uri neprevăzute (`P2`).

### T3. `send()` parțial, client lent și backpressure

**Problemă.** Pe socket non-blocking, `send()` poate scrie doar o parte sau poate întoarce `EAGAIN`. Rezultat ignorat ⇒ mesaje pierdute. Coadă nelimitată ⇒ un client care nu citește consumă memoria serverului.
**Soluție.** Coadă de output per client, cu limită; `POLLOUT` activ **doar** cât coada nu e goală.

```cpp
bool Connection::queue(const std::string& message)
{
    if (message.size() > MAX_OUTPUT_BYTES - _outputBytes)
        return false;                     // clientul lent va fi deconectat de apelant
    _output.push_back(message);
    _outputBytes += message.size();
    return true;
}

Connection::IoStatus Connection::flush()
{
    while (!_output.empty())
    {
        std::string& cur = _output.front();
        ssize_t sent = ::send(_fd.get(), cur.data(), cur.size(), MSG_NOSIGNAL);
        if (sent > 0)
        {
            _outputBytes -= static_cast<std::size_t>(sent);
            if (static_cast<std::size_t>(sent) == cur.size())
                _output.pop_front();
            else
                cur.erase(0, static_cast<std::size_t>(sent));   // păstrăm restul
            continue;
        }
        if (sent < 0 && errno == EINTR)
            continue;
        if (sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
            return IO_WOULD_BLOCK;        // restul rămâne, așteptăm POLLOUT
        return IO_ERROR;                  // EPIPE, ECONNRESET, altele
    }
    return IO_DONE;
}
```

Am ales **coduri de stare**, nu excepții, pentru `queue`/`flush` (vezi Anexa A,
#9): apelantul decide (marchează pentru închidere) fără ca eroarea unui client
să traverseze stiva. Dispatcher-ul apelează doar `queue()`, niciodată `send()`.
Pentru pipeline-uri mari de output, înlocuiește `std::deque<std::string>` cu
un singur `std::string` + offset ca să eviți copiile.

### T4. `SIGPIPE`/`EPIPE`

Dacă peer-ul a închis conexiunea exact înaintea unui `send()`, kernelul poate trimite `SIGPIPE`, iar acțiunea implicită **termină procesul**. Sursele recomandă `MSG_NOSIGNAL`; aici recomandăm **ambele**, pentru portabilitate:

```cpp
#ifndef MSG_NOSIGNAL          // macOS/BSD nu îl definesc
# define MSG_NOSIGNAL 0
#endif
// … și la pornire, ignorarea globală (vezi installSignalHandlers în T8):
//    sigaction(SIGPIPE, {SIG_IGN}, 0)
```

Erorile de scriere (`EPIPE`, `ECONNRESET`) duc la închiderea clientului
respectiv, nu la oprirea unui broadcast (`L5`).

### T5. Rezultatele POSIX, tratate diferențiat

| Rezultat | Semnificație | Acțiune |
|---|---|---|
| `recv > 0` | date disponibile | append în buffer |
| `recv == 0` | peer-ul a închis ordonat (EOF) | `markForRemoval` |
| `recv < 0`, `EINTR` | întrerupt de semnal | reîncearcă / revino la `poll` |
| `recv < 0`, `EAGAIN`/`EWOULDBLOCK` | nu mai sunt date acum | revino la `poll` (**nu** e disconnect) |
| `recv < 0`, alt errno | eroare permanentă | `markForRemoval` |

Aceleași reguli pentru `send()`. Pentru `accept()` (buclă până la `EAGAIN`):

```cpp
void Server::acceptPendingConnections()
{
    for (;;)
    {
        if (_users.size() >= MAX_CLIENTS)      // vezi T6: aici NU doar `break`
        {
            pauseListener();
            return;
        }
        int clientFd = ::accept(_listenFd, 0, 0);
        if (clientFd >= 0)  { registerClient(clientFd); continue; } // setează O_NONBLOCK aici
        if (errno == EINTR) continue;
        if (errno == EAGAIN || errno == EWOULDBLOCK) return;
        handleAcceptFailure(errno);            // EMFILE/ENFILE/ECONNABORTED/… vezi T6
        return;
    }
}
```

### T6. Epuizarea fd-urilor (`EMFILE`/`ENFILE`) și limita de clienți

**Scenariu.** Procesul are o limită de fd-uri (`ulimit -n`, adesea 1024). Fără limită proprie, `accept()` începe să întoarcă `EMFILE`.
**Capcana din documentele sursă.** Ideea „ne oprim din `accept()` și lăsăm conexiunile în backlog” este corectă doar pe jumătate: `poll()` este **level-triggered**, deci listenerul rămâne `POLLIN` cât timp există conexiuni în coadă și `poll()` revine imediat, la infinit ⇒ **busy-loop cu CPU 100%**, exact ce se voia evitat (Anexa A, #3). De asemenea `MAX_CLIENTS = 1024` egal cu `ulimit -n` nu ajunge: stdin/stdout/stderr, listenerul și eventualele fișiere de log consumă și ele fd-uri.

**Soluție (trei părți).**

1. `MAX_CLIENTS` strict sub limita procesului (verifică cu `getrlimit(RLIMIT_NOFILE)` la pornire și scade o rezervă de ~16).
2. Când limita e atinsă, **scoate `POLLIN` de pe listener** (`pauseListener()`), și îl repui când un client pleacă (`resumeListener()`). Alternativă simplă: `accept()` + trimite `ERROR :Server full` + `close()`.
3. Pentru `EMFILE`, tehnica clasică *„fd de rezervă”*: ține deschis un `open("/dev/null")`; la `EMFILE` îl închizi, faci `accept()` + `close()` (refuz curat, golește coada), apoi redeschizi rezerva. `ECONNABORTED` se ignoră și se continuă.

### T7. Event loop: invalidarea listelor și interpretarea `revents`

**Problemă.** `push_back` poate realoca `_pollFds`; ștergerea elementului curent deplasează indicii; un handler poate șterge clientul căruia îi procesezi încă evenimentele.
**Soluții, în ordinea preferinței:**

- **Reconstruiești `pollfd` la fiecare iterație** (ca în `demo_sources`): vectorul iterat e propria copie, iar clienții noi apar de la iterația următoare. Cel mai simplu; suficient pentru un proiect educațional.
- **Snapshot** al unui vector persistent + eliminări/adăugări amânate.
- **Regula generală:** handler-ele nu șterg; **marchează** (`markForRemoval`). Ștergerea fizică are loc **o singură dată, la finalul iterației** (`reapDisconnectedUsers`). Astfel nu apar nici *fd reuse* în aceeași iterație (un client nou primește fd-ul unuia închis și eventualele evenimente „vechi” din snapshot îi sunt aplicate din greșeală).

**Ordinea corectă de tratare a `revents`:**

1. `POLLNVAL` → eliminare (bug: fd deja închis).
2. `POLLERR` → diagnostic și eliminare.
3. `POLLIN` → **citește întâi**, chiar dacă `POLLHUP` e setat: un client care trimite `QUIT`/`PRIVMSG` și închide imediat produce `POLLIN|POLLHUP`; dacă elimini pe `POLLHUP` înainte de citire, pierzi ultimele date (Anexa A, #8). EOF-ul vine oricum ca `recv()==0`.
4. `POLLHUP` fără `POLLIN` → eliminare.
5. Dacă userul nu e marcat, `POLLOUT` → `flush()`.

**Cerință:** listenerul se golește prin `accept()` repetat până la `EAGAIN` (vezi `T5`).

**`select()` este interzis** (fost caz `C1`): `FD_SET` nu verifică limita `FD_SETSIZE` (de obicei 1024); un fd ≥ 1024 scrie în afara bitmap-ului ⇒ corupere de stivă, care trece testele cu 500 de clienți și pică sub sarcină reală. Folosim exclusiv `poll()` și documentăm interdicția în `Server.hpp` (plus un `grep -n "select(\|FD_SET" srcs/` în review).

### T8. Semnale și shutdown

**Problemă.** Handlerul din `another_demo` scrie în `std::cout` și apelează `stop()`: nu este async-signal-safe.

```cpp
#include <csignal>
volatile std::sig_atomic_t g_stop = 0;

extern "C" void handleSignal(int) { g_stop = 1; }   // singura operație permisă

void installSignalHandlers()
{
    struct sigaction sa;
    std::memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handleSignal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;                       // fără SA_RESTART: poll() iese cu EINTR
    sigaction(SIGINT,  &sa, 0);
    sigaction(SIGTERM, &sa, 0);

    struct sigaction ign;
    std::memset(&ign, 0, sizeof(ign));
    ign.sa_handler = SIG_IGN;
    sigemptyset(&ign.sa_mask);
    sigaction(SIGPIPE, &ign, 0);           // vezi T4
}
```

**Reguli:**

- `g_stop` se verifică **o singură dată pe iterație**, la începutul buclei; niciodată în mijlocul unei funcții care trebuie să fie logic atomică (ex. `removeClient` cu toți pașii ei).
- `poll()` întrerupt de semnal întoarce `-1/EINTR`: **nu este eroare fatală**.
- **Cursă:** dacă semnalul sosește între verificarea `g_stop` și intrarea în `poll(…, -1)`, serverul rămâne blocat până la următorul eveniment. Folosește **timeout periodic** (ex. 1000 ms) sau self-pipe/`ppoll`/`eventfd`.
- Shutdown în context normal: oprește acceptarea → (opțional) mesaj de shutdown → închide clienții → distruge registry-urile → închide listenerul. Toate obiectele trebuie să rămână într-o stare destructibilă și idempotentă.

---

## Lifecycle și memorie

### L1. Ownership pentru socket-uri și fd-uri

`User` primește un `ISocket*` dar nu-l șterge ⇒ leak de memorie **și** de fd la fiecare client. Invers, dacă și `Server`, și `User` închid același fd ⇒ double-close (poate închide fd-ul altui client, deja realocat de kernel).

```cpp
class Fd
{
public:
    explicit Fd(int fd) : _fd(fd) {}
    ~Fd() { if (_fd >= 0) ::close(_fd); }
    int  get() const   { return _fd; }
    bool valid() const { return _fd >= 0; }
    int  release()     { int r = _fd; _fd = -1; return r; }   // transfer explicit
private:
    int _fd;
    Fd(const Fd&);                // non-copyable: un singur proprietar
    Fd& operator=(const Fd&);
};
```

- Dacă `bind()`/`listen()` eșuează după `socket()`, destructorul închide fd-ul automat.
- `Socket` conține un `Fd`, nu un `int` copiabil.
- Decizia (`Server` → `User` → `Connection` → `Fd`) se scrie în header; nu se amestecă modelele.
- În C++11 același rol îl are un tip move-only / `unique_ptr` cu deleter.

### L2. Ștergerea dintr-un container în timpul parcurgerii

```cpp
// GREȘIT
for (std::map<int, User*>::iterator it = _users.begin(); it != _users.end(); ++it)
    if (shouldDisconnect(it->second))
        _users.erase(it);          // `it` e mort; ++it din for = UB
```

Cu `std::vector`, `erase` mai și **deplasează** elementele de după, deci indicii păstrați separat ajung să indice alt client. **Soluție: colectezi, apoi ștergi.**

```cpp
void Server::reapDisconnectedUsers()
{
    std::vector<int> toRemove(_pendingRemoval.begin(), _pendingRemoval.end());
    _pendingRemoval.clear();              // _pendingRemoval: std::set<int> => idempotent
    for (std::vector<int>::const_iterator id = toRemove.begin();
         id != toRemove.end(); ++id)
        removeClient(*id);                // un singur punct de ștergere (L3)
}
```

Prima fază nu modifică containerul; a doua lucrează pe o listă separată și
apelează **o singură** funcție de cleanup, deci logica nu se duplică (sursă clasică de leak-uri).

### L3. Un `User` trăiește în trei structuri: un singur punct de „moarte”

Un `User*` apare în `_users` (fd → User), în `UserRegistry` (nick → User) și în lista de membri a fiecărui `Channel`. Dacă `removeClient()` îl șterge doar din `_users`, celelalte două rămân cu pointeri morți; următorul `PRIVMSG` către nick-ul vechi sau următorul broadcast ⇒ use-after-free.

```cpp
void Server::removeClient(int fd)
{
    std::map<int, User*>::iterator it = _users.find(fd);
    if (it == _users.end())
        return;                                   // idempotent: apel dublu = nimic

    User* user = it->second;

    // 1) Din toate canalele. Copiem lista (removeMember o modifică pe parcurs).
    //    Aici se trimite și QUIT/PART către ceilalți membri: userul încă există.
    std::vector<Channel*> channels = user->channels();
    for (std::size_t i = 0; i < channels.size(); ++i)
        removeMember(*channels[i], *user);

    // 2) Din registry-ul de nickname-uri.
    _userRegistry.forget(*user);

    // 3) Din harta de fd-uri.
    _users.erase(it);

    // 4) Abia acum distrugem obiectul: Connection -> Fd -> ::close(fd).
    delete user;
}
```

**Ordinea contează:** pasul 1 înainte de `delete`, pentru că `removeMember` mai
citește starea userului (nickname pentru mesajul `PART`). Invers ar fi un
use-after-free chiar în funcția de cleanup. `delete` nu apare nicăieri în afara
acestei funcții.

### L4. `QUIT` urmat de alte comenzi în același pachet TCP

Cel mai subtil caz: clientul trimite într-un singur pachet
`QUIT :bye\r\nPRIVMSG #c :mai vorbesc?\r\n`. Dacă handlerul `QUIT` șterge imediat `User`-ul, a doua iterație a buclei de linii folosește un obiect mort.
Nu apare niciodată la teste manuale (tastezi comenzile pe rând), dar apare garantat cu un script.

**Soluție:** deconectarea nu are loc „pe loc”; se **marchează**, iar bucla verifică marcajul **înaintea fiecărei linii**.

```cpp
void Server::processLines(User& user)
{
    Connection& conn = user.connection();
    std::string line;
    for (int budget = MAX_LINES_PER_TURN; budget > 0; --budget)
    {
        if (isMarkedForRemoval(conn.fd()) || isMarkedForClosing(conn.fd()))
            return;                       // ce a mai rămas în pachet se ignoră
        Connection::FrameStatus st = conn.nextLine(line);
        if (st == Connection::FRAME_NEED_MORE) return;
        if (st == Connection::FRAME_TOO_LONG)  throw ProtocolError("line too long");
        if (line.empty()) continue;                       // linii goale: ignorate (RFC)
        ParseResult pr = parseLine(line);                 // P3
        if (!pr.ok) { continue; }                         // sau 421, după caz
        _dispatcher.dispatch(pr, user);
    }
    // Budget epuizat: mai pot fi linii în buffer. Ține un flag `hasBacklog`
    // și procesează-le și în iterația următoare, chiar dacă poll() nu mai
    // raportează POLLIN (altfel rămân blocate).
}

void Server::handleQuit(User& user, const std::string& reason)
{
    user.connection().queue(makeQuitNotice(reason));
    markForClosing(user.connection().fd());   // doar marchează; nu șterge nimic acum
}
```

Bugetul `MAX_LINES_PER_TURN` previne *starvation*: fără el, un client care
trimite mii de comenzi într-un pachet monopolizează iterația.

### L5. Broadcast: use-after-free, izolarea erorilor și amplificarea memoriei

**(a) Use-after-free.** Iterația directă peste `members` e riscantă dacă vreun pas modifică lista. Iterăm o **copie** a pointerilor. Cu ștergere strict amânată (`L2`/`L4`), nicio ștergere nu are loc în timpul broadcast-ului, deci verificarea se reduce la „nu e marcat”; snapshot-ul rămâne a doua linie de apărare.

> Nu verifica validitatea unui pointer comparându-l cu registry-ul *după adresă*: după un `delete`, un `User` nou poate primi aceeași adresă (problema ABA) și verificarea trece pe un obiect greșit. Verifică după **fd/id** sau după marcajul de ștergere (Anexa A, #7).

**(b) Izolarea erorilor.** Un `send()` eșuat către un membru nu trebuie să oprească livrarea către ceilalți 49. De aceea broadcast-ul doar pune mesajul în coada fiecăruia (`queue()`), iar dacă coada unuia e plină, doar acela e marcat.

```cpp
void Channel::broadcast(const std::string& msg, const User* except)
{
    std::vector<User*> snapshot = _members;              // copie de pointeri
    for (std::size_t i = 0; i < snapshot.size(); ++i)
    {
        User* member = snapshot[i];
        if (member == except || _server.isMarked(member->connection().fd()))
            continue;
        if (!member->connection().queue(msg))            // T3: limită depășită
            _server.markForRemoval(member->connection().fd());
    }
}
```

**(c) Amplificare (fan-out).** Un canal cu 500 de membri și un mesaj de 400 B
înseamnă 200 000 B alocați dintr-o singură comandă; limita per client
(`MAX_OUTPUT_BYTES`) este respectată și memoria totală tot crește. Două soluții:

- **Simplă (recomandată pentru proiectul de școală):** `MAX_CHANNEL_MEMBERS` + `MAX_CLIENTS`; e ușor de explicat și de testat.
- **Avansată:** mesaj partajat, cu numărare de referințe (în C++98 nu există `shared_ptr`):

```cpp
class SharedMessage
{
public:
    explicit SharedMessage(const std::string& text)
        : _text(new std::string(text)), _refs(new long(1)) {}
    SharedMessage(const SharedMessage& o) : _text(o._text), _refs(o._refs) { ++*_refs; }
    ~SharedMessage() { if (--*_refs == 0) { delete _text; delete _refs; } }
    const std::string& text() const { return *_text; }
private:
    std::string* _text;
    long*        _refs;
    SharedMessage& operator=(const SharedMessage&);   // nedefinit
};
```

(Coada ar stoca `SharedMessage` și un offset per destinatar; nu e obligatoriu din prima.)

### L6. Canale: `KICK` în masă și canale goale

**`KICK #c n1,n2,n3`.** Dacă `removeMember()` șterge canalul când rămâne fără membri și bucla mai are nick-uri de procesat, iterația următoare folosește un `Channel` șters.
**Soluții** (oricare, sau ambele): (1) **amâni ștergerea canalelor goale** în faza de reap (`pruneEmptyChannels()`); (2) **re-cauți canalul prin nume** la fiecare pas:

```cpp
void Server::handleKick(const std::string& channelName,
                        const std::vector<std::string>& nicks)
{
    for (std::size_t i = 0; i < nicks.size(); ++i)
    {
        Channel* channel = findChannel(channelName);   // re-căutare, nu pointer păstrat
        if (!channel)
            break;                                     // "nu mai există" = caz normal
        User* target = findUser(nicks[i]);
        if (!target || !channel->hasMember(*target))
            continue;
        removeMember(*channel, *target);
    }
}
```

**Canale goale.** La deconectare, `demo_sources` elimină membrul dar păstrează
canalul ⇒ memoria crește cu canalele temporare. Curățarea trebuie să fie
într-un singur loc:

```cpp
void Server::removeMember(Channel& channel, User& user)
{
    channel.remove(user);
    user.leave(channel);          // invariantul bidirecțional, doar prin metode controlate
    if (channel.memberCount() == 0)
        _channelsToPrune.insert(normalizeIrc(channel.name()));   // ștergere fizică în reap
}
```

`User`/`Channel` nu trebuie să expună membri publici (`members`, `channels`):
altfel invariantul `user.channels ∋ channel ⇔ channel.members ∋ user` nu poate fi garantat.

---

## Protocol și input

### P1. Acces la parametri fără verificare

```cpp
// GREȘIT: JOIN\r\n fără canal => params e gol, operator[] nu verifică => UB
std::string channel = params[0];
```

Soluție: un helper folosit **peste tot** în handlere, niciodată `params[i]` direct, și un răspuns IRC corect în loc de crash (`461 ERR_NEEDMOREPARAMS`).

```cpp
inline bool paramAt(const std::vector<std::string>& params,
                    std::size_t index, std::string& out)
{
    if (index >= params.size()) return false;
    out = params[index];
    return true;
}

void handleJoin(const std::vector<std::string>& params, User& user)
{
    std::string channel;
    if (!paramAt(params, 0, channel))
    {
        user.connection().queue(makeNumericError(461, "JOIN :Not enough parameters"));
        return;
    }
    joinChannel(user, channel);
}
```

Clientul află exact ce a greșit, iar un singur client neatent nu mai poate
doborî serverul pentru toți.

### P2. Excepții STL neprinse: `terminate()` omoară toți clienții

`std::string::at()` și `substr(pos)` cu `pos > size()` aruncă `std::out_of_range`; concatenări uriașe pot arunca `length_error`/`bad_alloc`. Necaptate, declanșează `std::terminate()`: **moare tot procesul**.
(Notă: `substr(0, 500)` pe un șir mai scurt **nu** aruncă; doar `pos > size()` aruncă. Exemplul original din sursă era greșit, vezi Anexa A, #5.)

**Două straturi, ambele obligatorii:**

```cpp
// Stratul 1: verifici înainte, în loc să te bazezi pe excepție.
inline std::string safeSubstr(const std::string& s, std::string::size_type pos,
                              std::string::size_type len = std::string::npos)
{
    if (pos > s.size()) return std::string();
    return s.substr(pos, len);
}

// Stratul 2: plasa finală în bucla principală.
void Server::run()
{
    installSignalHandlers();
    while (!g_stop)                        // singurul loc unde se citește g_stop (T8)
    {
        try
        {
            pollOnce();                    // accept + recv + dispatch + reap
        }
        catch (const std::exception& e)
        {
            logSystemError(e.what());      // fără să oprești procesul
        }
    }
    shutdown();
}
```

Stratul 1 dă un mesaj IRC prietenos la locul problemei; stratul 2 garantează că
un client defect nu oprește niciodată tot serverul. **Avertisment:** stratul 2
nu înlocuiește tratarea la granița clientului (`T2`): dacă aceeași stare
proastă rămâne, aceeași excepție se repetă la fiecare iterație.

### P3. Parser și AST cu rezultat explicit

`ASTParser::peekToken()` trebuie apelat doar când `_index < _tokens.size()`. Linia goală, comanda lipsă sau parametrii invalizi nu trebuie să producă un AST care *pare* valid. Comenzile se normalizează la majuscule (`nick` = `NICK`).

```cpp
struct ParseResult
{
    bool ok;
    std::string prefix, command, error;
    std::vector<std::string> params;
    ParseResult() : ok(false) {}
};                                         // fără pointeri raw: nimic de eliberat

ParseResult parseLine(const std::string& line)
{
    ParseResult r;
    if (line.empty() || line.find_first_of(std::string("\r\n\0", 3)) != std::string::npos)
    { r.error = "empty or invalid characters"; return r; }

    std::string::size_type i = 0, n = line.size(), start;
    if (line[0] == ':')
    {
        std::string::size_type sp = line.find(' ');
        if (sp == std::string::npos || sp == 1) { r.error = "bad prefix"; return r; }
        r.prefix.assign(line, 1, sp - 1);
        i = sp;
    }
    while (i < n && line[i] == ' ') ++i;
    start = i;
    while (i < n && line[i] != ' ') ++i;
    if (start == i) { r.error = "missing command"; return r; }
    r.command.assign(line, start, i - start);
    for (std::size_t k = 0; k < r.command.size(); ++k)
        if (r.command[k] >= 'a' && r.command[k] <= 'z')
            r.command[k] = static_cast<char>(r.command[k] - 'a' + 'A');

    for (;;)
    {
        while (i < n && line[i] == ' ') ++i;
        if (i >= n) break;
        if (line[i] == ':') { r.params.push_back(line.substr(i + 1)); break; }  // trailing
        start = i;
        while (i < n && line[i] != ' ') ++i;
        r.params.push_back(line.substr(start, i - start));
        if (r.params.size() > 15) { r.error = "too many parameters"; return r; } // RFC 2812
    }
    r.ok = true;
    return r;
}
```

Dispatcher-ul decide răspunsul: comandă necunoscută ⇒ `421 ERR_UNKNOWNCOMMAND`;
linii goale ⇒ ignorate. `PING :tok` răspunde `PONG :tok` (nu `PONG: tok`).

### P4. Nickname: validare, unicitate, redenumire atomică

`NICK` verifică: nenul, lungime maximă (configurabilă: 9 în RFC 2812, 30 des în practică), primul caracter permis (literă sau caracter special `[]\`_^{|}`), restul (litere, cifre, special, `-`), unicitate **case-insensitive**. Schimbarea este atomică la nivelul registry-ului; la eșec, nick-ul vechi rămâne intact. Forma originală se păstrează pentru afișare, cea normalizată pentru lookup.

**Case-folding IRC** (`tolower` generic nu ajunge; nici dependența de locale): conform RFC 1459/2812, `[ ] \ ~` sunt majusculele lui `{ } | ^`.

```cpp
std::string normalizeIrc(const std::string& s)
{
    std::string r(s);
    for (std::size_t i = 0; i < r.size(); ++i)
    {
        char c = r[i];
        if (c >= 'A' && c <= 'Z') r[i] = static_cast<char>(c - 'A' + 'a');
        else if (c == '[')  r[i] = '{';
        else if (c == ']')  r[i] = '}';
        else if (c == '\\') r[i] = '|';
        else if (c == '~')  r[i] = '^';
    }
    return r;
}

bool UserRegistry::rename(User& user, const std::string& requested)
{
    if (!isValidNick(requested))
        return false;
    const std::string key = normalizeIrc(requested);

    std::map<std::string, User*>::iterator it = _byNick.find(key);
    if (it != _byNick.end() && it->second != &user)
        return false;                        // ocupat de ALTCINEVA; "Ana" -> "ana" e permis

    const std::string oldKey = user.nick().empty() ? "" : normalizeIrc(user.nick());
    _byNick[key] = &user;                    // 1) rezervă cheia nouă (poate arunca; nimic schimbat încă)
    if (!oldKey.empty() && oldKey != key)
        _byNick.erase(oldKey);               // 2) eliberează cheia veche (nu aruncă)
    user.setNick(requested);                 // 3) actualizează obiectul
    return true;
}
```

Handlerul `NICK` nu modifică direct câmpuri din `Socket`/`User` în locuri
diferite: apelează un singur serviciu care validează, verifică unicitatea și
actualizează toate indexurile. Invariant: `_byNick[normalize(user.nick)] == &user`.
`ERR_NICKNAMEINUSE` este `433`, `ERR_ERRONEUSNICKNAME` este `432`.

### P5. Parametri numerici din comenzi (`MODE #c +l 100`)

`atoi("abc")` dă `0` în tăcere; `atoi("999999999999999")` dă overflow (UB). Un `0` acceptat poate însemna „canal cu limită zero”, adică un canal blocat sau interpretat greșit ca „fără limită”. Un singur wrapper strict, reutilizabil pentru port, limite de canal, etc.:

```cpp
inline bool parseStrictLong(const std::string& text, long minValue, long maxValue, long& out)
{
    if (text.empty()) return false;
    char* end = 0;
    errno = 0;
    long value = std::strtol(text.c_str(), &end, 10);
    if (*end != '\0')        return false;   // "100abc"
    if (errno == ERANGE)     return false;   // overflow în strtol însuși
    if (value < minValue || value > maxValue) return false;   // regula domeniului
    out = value;
    return true;
}
// MODE +l:  parseStrictLong(arg, 1, 100000, limit)  -> altfel numeric de eroare
//           (696 ERR_INVALIDMODEPARAM e extensie modernă, nu apare în RFC 2812)
```

### P6. Argumente de pornire

Un server „de apărare” trebuie să **eșueze devreme și clar**, nu să pornească într-o stare ambiguă (`atoi` dă `0` ⇒ `bind()` poate alege un port aleatoriu).

```cpp
int main(int argc, char** argv)
{
    if (argc != 3)
    { std::cerr << "Utilizare: " << argv[0] << " <port> <parola>\n"; return 1; }

    long port = 0;
    if (!parseStrictLong(argv[1], 1, 65535, port))
    { std::cerr << "Port invalid: " << argv[1] << "\n"; return 1; }
    // (porturile < 1024 cer privilegii; bind() va raporta clar eroarea)

    const std::string password(argv[2]);
    if (password.empty())
    { std::cerr << "Parola nu poate fi goala\n"; return 1; }

    try
    {
        Server server(static_cast<int>(port), password);   // ctor aruncă la eșec socket/bind/listen
        server.run();
    }
    catch (const std::exception& e)
    { std::cerr << "Eroare fatala la pornire: " << e.what() << "\n"; return 1; }
    return 0;
}
```

---

## 7. Scheletul integrat al event loop-ului

Toate soluțiile de mai sus, puse împreună (fragment de referință, nu cod final):

```cpp
void Server::pollOnce()
{
    std::vector<struct pollfd> fds = buildPollSet();   // copia locală = snapshot natural
    int rc = ::poll(&fds[0], fds.size(), 1000);        // timeout: T8
    if (rc < 0)
    {
        if (errno == EINTR) return;                    // semnal: nu e eroare
        throw std::runtime_error("poll failed");
    }
    for (std::size_t i = 0; rc > 0 && i < fds.size(); ++i)
    {
        const struct pollfd& ev = fds[i];
        if (ev.revents == 0) continue;

        if (ev.fd == _listenFd) { acceptPendingConnections(); continue; }

        User* user = findUserByFd(ev.fd);
        if (!user || isMarked(ev.fd)) continue;        // deja în ieșire

        if (ev.revents & (POLLNVAL | POLLERR)) { markForRemoval(ev.fd); continue; }
        if (ev.revents & POLLIN)                readClient(*user);       // drenează și la HUP
        else if (ev.revents & POLLHUP)        { markForRemoval(ev.fd); continue; }
        if ((ev.revents & POLLOUT) && !isMarked(ev.fd))
            writeClient(*user);                        // flush(); IO_ERROR => markForRemoval
    }
    reapDisconnectedUsers();      // L2/L3: singura ștergere fizică, la final
    pruneEmptyChannels();         // L6
    resumeListenerIfRoom();       // T6
}

// buildPollSet(): listener (POLLIN, dacă nu e în pauză) + pentru fiecare User:
//   POLLIN (dacă nu e Closing), POLLOUT doar dacă connection().wantsWrite().
```

---

## 8. Ordinea de implementare

Ordinea reconciliază cele două audituri (fundația întâi; funcționalitatea abia apoi):

1. **Ownership** explicit: `Fd`, `User`, `Socket`; copiere dezactivată (`L1`).
2. **Buffer persistent de input** + limite (`T1`, `T2`).
3. **Coadă de output** + backpressure (`T3`).
4. **Erori POSIX** explicite: `EINTR`, `EAGAIN`, EOF, `POLL*`, `accept` (`T5`, `T6`).
5. **Event loop stabil**: marcare + reap, ordinea `revents` (`T7`, `L2`, `L4`).
6. **Lifecycle explicit** și `removeClient` unic, idempotent (`L3`, FSM din §5.4).
7. **Semnale** (`T8`) și `SIGPIPE` (`T4`).
8. **Parser** cu `ParseResult`, comenzi case-insensitive, răspunsuri numerice standard (`P3`, `P1`).
9. **Registry de nickname** și redenumire atomică (`P4`).
10. **Registry de canale**, broadcast sigur, curățare canale goale (`L5`, `L6`).
11. **Numerice IRC** complete (`001–004`, `353/366`, `421`, `432`, `433`, `461`, …).
12. **Teste** unitare, integrare și stres (§9), rulate în același PR cu modificarea pe care o protejează.

---

## 9. Teste și instrumente

### 9.1 Matricea de teste obligatorii (înainte de integrare)

**Framing și input**
- [ ] `PI` + `NG :abc\r\n` produce un singur `PING` (`T1`).
- [ ] `PING :a\r\nPING :b\r\n` produce două comenzi (`T1`).
- [ ] O linie incompletă rămâne în buffer, nu este dispatch-uită (`T1`).
- [ ] Linie de 512 B **cu** CRLF acceptată; 513 B respinsă; `\r` la finalul unui `recv` și `\n` în următorul funcționează (`T1`).
- [ ] Octeți la infinit fără terminator: **doar** acel client e deconectat, restul merge normal (`T2`).
- [ ] Input cu NUL în mijloc nu trunchiază tăcut / este respins controlat (`T1`, `P3`).

**Output și backpressure**
- [ ] `send()` parțial păstrează restul în coadă (`T3`).
- [ ] `EAGAIN` nu închide clientul (`T3`, `T5`).
- [ ] Coada de output depășită ⇒ disconnect controlat, ceilalți nu sunt afectați (`T3`).
- [ ] Broadcast într-un canal cu un membru mort/lent nu oprește livrarea către ceilalți (`L5`).
- [ ] `SIGPIPE` nu termină procesul (`T4`).

**Lifecycle**
- [ ] EOF elimină clientul din toate indexurile și închide fd-ul (`L1`, `L3`).
- [ ] `POLLHUP` cu date în așteptare: datele sunt procesate, apoi clientul eliminat (`T7`).
- [ ] `QUIT` + altă comandă în același pachet: a doua nu se execută, fără crash (`L4`).
- [ ] Doi clienți fac `QUIT` în aceeași iterație `poll()` (`L2`).
- [ ] Două evenimente de închidere pentru același fd: fără double-delete (`L3`).
- [ ] Peste `MAX_CLIENTS`: fără busy-loop (`top` arată CPU normal), refuz curat (`T6`).
- [ ] `ulimit -n` mic (ex. 20) + conexiuni multe: serverul rămâne în picioare (`T6`).
- [ ] `KICK` cu listă care golește canalul: fără acces la canal șters (`L6`).
- [ ] Un canal gol este eliminat (`L6`).
- [ ] `PART` de la ultimul membru în timp ce alt cod face broadcast (`L5`).

**Protocol**
- [ ] `JOIN`, `PART`, `KICK`, `MODE` fără parametri ⇒ `461`, fără crash (`P1`).
- [ ] Comandă în litere mici (`nick`) recunoscută; comandă necunoscută ⇒ `421` (`P3`).
- [ ] Linie goală, doar prefix, >15 parametri: respinse controlat (`P3`).
- [ ] Nickname duplicat respins **fără** modificarea stării existente; `Ana`→`ana` de același user permis (`P4`).
- [ ] Redenumirea actualizează simultan `User` și registry (`P4`).
- [ ] `MODE #c +l abc`, `+l -5`, `+l 99999999999999999999` respinse explicit (`P5`).
- [ ] Port `abc`, `0`, `70000`, `123abc`, fără parolă ⇒ mesaj clar, fără socket deschis (`P6`).

**Semnale**
- [ ] `SIGINT` pe server inactiv (blocat în `poll`) ⇒ oprire în ≤ timeout (`T8`).
- [ ] `SIGINT` sub sarcină ⇒ shutdown ordonat, fără leak-uri (`T8`).

### 9.2 Comenzi practice de test

```bash
# Fragmentare: "PI" .. pauză .. "NG :abc"
( printf 'PI'; sleep 1; printf 'NG :abc\r\n'; sleep 1 ) | nc 127.0.0.1 6667
# Două comenzi într-un singur pachet
printf 'PING :a\r\nPING :b\r\n' | nc 127.0.0.1 6667
# QUIT + comandă în același pachet (L4)
printf 'NICK a\r\nUSER a 0 * :A\r\nQUIT :bye\r\nPRIVMSG #c :x\r\n' | nc 127.0.0.1 6667
# Flux fără terminator (T2)
head -c 1000000 /dev/zero | tr '\0' 'A' | nc 127.0.0.1 6667
# Client care nu citește (T3): tine conexiunea deschisă fără să consume
( printf 'NICK slow\r\nUSER s 0 * :S\r\nJOIN #c\r\n'; sleep 600 ) | nc 127.0.0.1 6667
# Leak-uri de memorie ȘI de fd
valgrind --leak-check=full --track-fds=yes ./ircserv 6667 pass
# Ce fd-uri au rămas deschise
lsof -p $(pgrep ircserv)
# Ce syscall-uri se fac efectiv
strace -f -e trace=poll,accept,recv,send,close -p $(pgrep ircserv)
```

(Opțiunile `nc` diferă între variante: `-q`, `-N`, `-w`. Verifică `man nc`.)

### 9.3 Build și analiză

- `-std=c++98 -Wall -Wextra -Werror` la **fiecare** compilare (pentru `another_demo`, parametrii `server` neutilizați trebuie eliminați sau marcați `(void)server;`).
- AddressSanitizer + UBSan (prind exact use-after-free din `L3`/`L4`/`L6`), Valgrind Memcheck, `cppcheck`, `clang-tidy`.
- Testare cu `GoogleTest`/`Catch2` doar dacă dependențele sunt permise; altfel, un `main` de test propriu care folosește un `ISocket` fals.

---

## 10. Lucrul în echipă: două persoane, un repository GitHub

Scop: cele două persoane să nu editeze simultan aceleași blocuri din
`Server.cpp`, `Server.hpp` sau `Makefile` fără coordonare.

### 10.1 Împărțirea responsabilităților

| | Persoana A: transport și lifecycle | Persoana B: protocol și stare IRC |
|---|---|---|
| Fișiere | `Server.cpp/hpp`, `Socket.cpp/hpp`, `Socket.cpp` | `User`, `Channel`, `Lexer`, `ASTParser`, `CommandDispatcher` |
| Subiecte | framing, `poll`, `accept`, `recv`, `send`, shutdown, ownership fd (`T*`, `L1`–`L4`) | validare IRC, nickname-uri, canale, registry, numerice (`P*`, `L5`, `L6`) |

Împărțirea nu e absolută. Dacă ambii trebuie să modifice `Server.cpp`, se
stabilește **întâi contractul** (§5.3): A expune `queue()` și `nextLine()`, B
le folosește fără să rescrie event loop-ul.

### 10.2 Reguli de branch

```bash
git fetch origin
git switch main
git pull --ff-only origin main
git switch -c feature/transport-framing      # un branch = un scop clar
```

- Un branch nu se împarte între două persoane. Commit-uri mici, compilabile:
  `fix: preserve fragmented IRC lines`, `fix: bound per-client output queue`,
  `test: cover partial non-blocking writes`. Fără `misc`, `changes`, `final`.
- Fișiere comune (`Makefile`, `main.cpp`, documentație): se anunță cine le modifică; nu se combină reformatarea cu modificarea funcțională; niciodată `git checkout --` / `git reset --hard` peste munca celuilalt.

### 10.3 Ordinea integrării

1. A publică contractul de transport (`Connection`, buffer input, coadă output, semnalizare erori) **și testele lui**.
2. B scrie parserul/dispatcher-ul împotriva contractului, fără să scrie direct în socket.
3. Pull request-uri separate; se integrează întâi transportul, apoi protocolul.
4. După fiecare integrare: `make re`.

Dacă două schimbări sunt logic dependente, a doua persoană face rebase pe PR-ul
primei sau pornește branch-ul din commit-ul ei (nu copiază manual codul).
Fiecare persoană își sincronizează branch-ul cu `main` **înainte** de PR;
branch-urile lungi cresc probabilitatea de conflicte în `Server.cpp`.

### 10.4 Ce se întâmplă la `git push` și cum preia celălalt lucrul

```bash
git push -u origin alex          # publică `alex`, setează tracking; NU modifică main
```

```bash
git fetch origin                                   # descarcă, nu schimbă branch-ul curent
git branch -a ; git log --oneline origin/alex ; git diff origin/main..origin/alex
git switch -c alex --track origin/alex             # branch local din cel remote
git switch alex && git pull --ff-only              # dacă branch-ul local există deja
git status ; git branch --show-current             # verifică mereu pe ce branch ești
```

Amândoi **nu** fac commit-uri pe același branch. Pentru a continua lucrul
celuilalt: `git switch -c feature/protocol origin/alex` (branch derivat) sau
`git cherry-pick <hash>` pentru un singur commit; review prin PR
(`alex → main`). `git fetch` doar aduce informații; `git pull` și integrează.

### 10.5 Sincronizare după merge și conflicte

```bash
git fetch origin && git switch main && git pull --ff-only origin main
git switch alex && git rebase origin/main          # rebase DOAR pe branch-ul propriu
# la conflict: rezolvi manual doar fișierele indicate, păstrând intenția ambelor părți
git add <fisiere-rezolvate> && git rebase --continue
git diff --check && make re
git push --force-with-lease origin alex            # nu --force: refuză dacă altcineva a publicat între timp
```

Branch-urile garantează **izolare** până la integrare (un `push` nu schimbă
branch-ul celuilalt, `main` se schimbă doar prin merge aprobat), dar **nu
elimină conflictele** dacă ambii modifică aceleași linii. Reducerea riscului:
fișiere și responsabilități împărțite dinainte, commit-uri mici, sincronizare
frecventă, fără reformatare a fișierelor comune, contractele comune primele.
La comiterea din exemple, preferă `git add <fișiere>` în loc de `git add .`.

### 10.6 Review obligatoriu și checklist de PR

Niciun PR nu se integrează fără verificarea celeilalte persoane:

```text
[ ] Nu există raw pointer owning nou fără justificare scrisă.
[ ] Nu există send() direct din dispatcher.
[ ] Nu se modifică un vector/map în timpul parcurgerii lui.
[ ] Nu există delete de User/Channel în afara punctelor unice (removeClient/prune).
[ ] Erorile POSIX (EINTR, EAGAIN, EOF, POLLHUP, accept) sunt tratate explicit.
[ ] Orice limită nouă are un test.
[ ] Nu apare select()/FD_SET.
[ ] make re (-Wall -Wextra -Werror -std=c++98) și git diff --check trec.
```

---

## 11. Catalog de algoritmi și pattern-uri

Este un **index de recunoaștere**, nu o listă de „de făcut acum”: când
întâlnești o problemă nouă, cauți denumirea care îi descrie forma.

### 11.1 De la problemă la tehnică

| Forma problemei | C++98 | Echivalent modern |
|---|---|---|
| Ownership de resursă (fd) | RAII, non-copyable, Rule of Three, `auto_ptr` (depreciat) | `unique_ptr` cu deleter, move-only |
| „Verifică dacă obiectul mai există” | Registry + identificator stabil | `weak_ptr` |
| Copiere sigură pentru clase cu resurse | Copy-and-swap | move semantics |
| Prea multe căi de eroare la parsare | `ParseResult` | `std::expected` (C++23), `optional`/`variant` (C++17) |
| Sfârșit de comandă într-un flux | Delimiter-based framing, sliding window | `string_view`/`span` pentru procesare fără copii |
| Mutare/ștergere în timpul iterării | Two-phase removal, snapshot, erase-remove | ranges (C++20) |
| Lookup rapid nick/canal | `std::map` (O(log n)) | `unordered_map`, `flat_map` (C++23) |
| Cozi de output | producer–consumer, `std::deque` | – |
| Limitarea ratei de comenzi (flood) | Token bucket / leaky bucket | – |
| Oprire cooperantă | `volatile sig_atomic_t` | `std::atomic`, `jthread` + `stop_token` |
| Multiplexare I/O | `poll()` (proiect), `epoll`/`kqueue` (scalare) | Reactor/Proactor, coroutines, `io_uring` |

### 11.2 Idiomuri și pattern-uri C++98 (doar nume și context)

- **Resurse și obiecte:** RAII (fd, fișiere, mutex-uri) · Rule of Three · Non-copyable idiom · Pimpl (izolare la recompilare) · `std::auto_ptr` · Copy-and-swap · Named Constructor · Construct-on-first-use · Meyers' Singleton (cu grijă).
- **OOP/polimorfism:** Non-Virtual Interface · Template Method · Strategy · Command (dispatcher) · State (FSM de înregistrare) · Observer (broadcast) · Visitor/double dispatch (parcurgere AST) · Adapter (`ISocket`) · Facade (`Server`) · Mediator · CRTP · Dependency Inversion/Injection · Null Object · Object Pool.
- **STL:** erase-remove · `find/find_if` · `sort`/introsort (ex. `NAMES`) · `binary_search`/`lower_bound` · `transform` (normalizare în masă) · `for_each` · functori (`unary_function`, `mem_fun`, `bind1st/2nd`) · `std::map/set` (arbore echilibrat) · `std::deque` (`OutputQueue`).
- **Rețea/sistem:** `select()` (interzis: `FD_SETSIZE`) · `poll()` · `epoll`/`kqueue` · buclă non-blocking cu retry pe `EAGAIN/EWOULDBLOCK/EINTR` · partial read/write loop · edge-safe drain loop (`accept` până la `EAGAIN`) · producer–consumer queue · watermark/backpressure · token/leaky bucket · exponential backoff (dacă serverul inițiază conexiuni) · fd de rezervă pentru `EMFILE` · self-pipe · handler async-signal-safe.
- **Parsare/validare:** FSM explicit (enum + switch) · two-pointer/index scanning · `strtol` strict · fail-fast validation · idempotent cleanup · ownership graph (documentat) · snapshot iteration · Single Responsibility.

### 11.3 C++ modern (C++11 → C++23), direcții de evoluție

Proiectul rămâne **C++98**; aceste facilități nu se introduc ca dependențe.

- **C++11/14:** move semantics, `unique_ptr`/`make_unique`, `shared_ptr`/`weak_ptr`, lambda-uri (generice în 14), `std::function`/`std::bind`, variadic templates, `auto`/`decltype`, range-for, `thread`/`mutex`/`condition_variable`, `std::atomic`, `nullptr`, `enum class`, `constexpr`, `static_assert`, `std::array`, `std::chrono`, `unordered_map`, delegating constructors, `override`/`final`, literale binare.
- **C++17:** `optional`, `variant`+`visit`, `string_view`, `any`, structured bindings, `if constexpr`, `filesystem`, fold expressions, CTAD, `scoped_lock`, `shared_mutex`, `pmr`, `invoke`.
- **C++20:** Concepts, Ranges, Coroutines, `span`, `jthread`/`stop_token`, `latch`/`barrier`, Modules, `operator<=>`, calendar/time-zone.
- **C++23:** `std::expected`, `print`/`format`, `mdspan`, `generator`, `stacktrace`, `flat_map`, `byteswap`, deducing `this`, operații monadice.
- **Design de nivel înalt:** Reactor (≈ bucla `poll()` a acestui proiect), Proactor (`io_uring`, IOCP), policy-based design, type erasure.

---

## 12. Surse de documentare

**Standarde și specificații**
- RFC 1459, RFC 2810/2811/2812 (protocol IRC, canale, mesaje, limita de 512, numerice), RFC 7194 (IRC peste TLS). Pentru comportamentul modern al numericelor și al extensiilor (ex. `417`, `696`), „Modern IRC Client Protocol” (modern.ircdocs.horse).
- POSIX.1-2008 (Open Group Base Specifications); ISO/IEC 14882:1998 (C++98) și ediții ulterioare.
- *C++ Core Guidelines* (Stroustrup & Sutter), pentru ownership, RAII, exception safety.

**Manual pages (citește codurile de eroare exacte, niciodată din memorie)**
`socket(2)`, `setsockopt(2)`, `bind(2)`, `listen(2)`, `accept(2)`, `connect(2)`, `recv(2)`, `send(2)`, `poll(2)`, `fcntl(2)`, `close(2)`, `sigaction(2)`, `signal(7)`, `signal-safety(7)`, `errno(3)`, `getaddrinfo(3)`, `getrlimit(2)`, `tcp(7)`, `ip(7)`.

**Cărți**
- *Effective C++*, *More Effective C++*, *Effective STL* (Meyers); *Exceptional C++*, *Exceptional C++ Style* (Sutter); *C++ Coding Standards* (Sutter & Alexandrescu); *Design Patterns* (GoF); *Modern C++ Design* (Alexandrescu); *The C++ Programming Language* (Stroustrup).
- **Rețea/sisteme:** *Unix Network Programming vol. 1* (Stevens), sursa principală pentru `socket/poll/select/SIGPIPE`/framing; *Advanced Programming in the UNIX Environment* (Stevens & Rago), pentru semnale și fd-uri; *TCP/IP Illustrated* (Stevens), pentru garanțiile reale ale TCP; *Beej's Guide to Network Programming* (online, gratuit; primul loc pentru un începător).

**Online:** cppreference.com, isocpp.org (FAQ), *Guru of the Week* (Sutter).

**Instrumente:** GCC/Clang (`-Wall -Wextra -Werror -std=c++98`), GDB, Valgrind, AddressSanitizer, UBSan, `strace`, `lsof`, `nc`, `socat`, `telnet`, `clang-tidy`, `cppcheck`, Wireshark/`tcpdump`, GoogleTest/Catch2.

**Coduri-sursă pentru studiu de arhitectură (nu pentru copiat):** ngIRCd, InspIRCd, UnrealIRCd. Se studiază *cum separă transportul de protocol*; licențele diferă.

---

## Anexa A. Erori și ambiguități corectate față de documentele sursă

La fuziune am verificat fragmentele de cod și am corectat următoarele:

| # | Unde | Problemă | Corecție în acest ghid |
|---|---|---|---|
| 1 | `LineFramer::popLine` (analiza `skettch`) | `bool` întoarce `false` atât pentru „nu e linie încă”, cât și pentru „linie prea lungă”; apelantul nu le poate distinge | `FrameStatus` cu trei stări (`T1`) |
| 2 | `readClient` (audit cazuri-limită, §4.3) | `popLine()` (care poate arunca `ProtocolError`) era în afara blocului `try`; excepția ajungea în `run()`, clientul rămânea, aceeași linie reproducea excepția la fiecare iterație | tot procesarea liniilor în `try`, cu `markForClosing` (`T2`) |
| 3 | `MAX_CLIENTS` + `break` (`C7`) | Listenerul rămâne `POLLIN` (level-triggered) ⇒ busy-loop; `MAX_CLIENTS=1024` = `ulimit -n`, deci `EMFILE` vine înainte | pauză pe listener, limită sub `RLIMIT_NOFILE`, fd de rezervă (`T6`) |
| 4 | `assertNoSelectAssumptions` (`C1`) | Funcție fără efect, deși pretinde o gardă | eliminată; rămâne regula documentată + `grep` în review (`T7`) |
| 5 | Exemplu `substr(0, 500)` (`C5`) | `substr(0, n)` nu aruncă; doar `pos > size()` aruncă; comentariul era confuz | exemplu și explicație corectate (`P2`) |
| 6 | `SharedMessage` (`C9`) | Contor pornit de la 0 cu test `< 0`: funcțional dar neobișnuit | contor pornit de la 1, `== 0` (`L5`) |
| 7 | `isStillMember(User*)` (`C3`/`C8`) | Validarea pointerului prin adresă suferă de ABA după `delete` | verificare prin fd/marcaj + ștergere amânată (`L5`) |
| 8 | Ordinea `POLLHUP` (audit + consultare) | Eliminarea pe `POLLHUP` înainte de `POLLIN` pierde ultimele date (`QUIT`, `PRIVMSG`) | citire întâi, apoi eliminare (`T7`) |
| 9 | `flush()` cu `throw SocketError` vs `queue()` cu `throw SlowClient` | Excepții pe calea fierbinte, tratate diferit în trei locuri | coduri de stare + `markFor…` la apelant (`T3`) |
| 10 | `UserRegistry::rename` | Respinge schimbarea doar de majuscule a propriului nick (`Ana`→`ana`); șterge cheia veche înaintea celei noi | verificare `owner != &user`, insert-apoi-erase (`P4`) |
| 11 | Case-folding | Lista din analiză omitea perechea `~`↔`^` | tabel complet RFC 1459 (`P4`) |
| 12 | Numerice `417`, `696` | Prezentate ca „standard”, dar nu apar în RFC 2812 | marcate ca extensii moderne |
| 13 | `MSG_NOSIGNAL` | Neportabil (macOS/BSD) | `#ifndef` + `SIGPIPE` ignorat global (`T4`) |
| 14 | `recv(fd, buf, room, 0)` | Dacă `room == 0` întoarce `0` = fals EOF | gardă explicită (`T2`) |
| 15 | Terminator de linie | Documentele impun `\r\n` strict; `nc` trimite `\n` | decizie de documentat (`T1`) |
| 16 | `PONG: token` (`another_demo`) | Format nestandard | `PONG :token` (`P3`) |
| 17 | Link-uri | Referințe către căi absolute locale (`/home/alexkatz/...`) | eliminate; fișierele sunt numite relativ |

## Anexa B. Trasabilitate

Cei doi colegi au produs fiecare un set de fișiere cu suprapuneri. Cum au fost fuzionate:

| Document original | Conținut | Unde se regăsește |
|---|---|---|
| `AUDIT_ROBUSTETE_SI_AJUSTARI.md` | verdict comparativ, ajustări 1–10 (RAII, framing, write queue, POSIX, event loop, nickname, canale, semnale, parser), workflow Git | §2, §5, `T1`–`T8`, `L1`, `L6`, `P3`, `P4`, §10 |
| `ANALIZA_ROBUSTETE_SI_IMPLEMENTARE.md` (audit `skettch`) | model mental, aceleași probleme, ordine de implementare, teste | §3, §6, §8, §9 (varianta cea mai detaliată păstrată, cea redundantă eliminată) |
| `AUDIT_CAZURI_LIMITA_SI_IMPLEMENTARE.md` | cazurile `C1`–`C15`, implementare consolidată, teste suplimentare | `T2`, `T6`–`T8`, `L2`–`L6`, `P1`, `P2`, `P5`, `P6`, §6.1, §9 |
| `CONSULTANTA_DESIGN_SI_ALGORITMI.md` | tabel pattern → problemă, surse, catalog C++98 / modern | §5.6, §11, §12 |
| `CONSULTARE_DESIGN_ALGORITMI_CPP.md` | design de clase, ownership graph, lifecycle, algoritmi, capcane | §5, `T7`, `T8`, §11 |
| `COMPARATIE_DEMO_IRC.md` | comparație `demo_sources` vs `another_demo`, recomandare finală | §2 |

Corespondența ID-urilor: `C1`→`T7`, `C2`→`L2`, `C3`→`L5`, `C4`→`P1`, `C5`→`P2`, `C6`→`T2`, `C7`→`T6`, `C8`→`L5`, `C9`→`L5`, `C10`→`L3`, `C11`→`L4`, `C12`→`P5`, `C13`→`L6`, `C14`→`P6`, `C15`→`T8`.
