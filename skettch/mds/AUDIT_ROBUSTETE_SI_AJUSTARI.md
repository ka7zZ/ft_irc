# Audit comparativ si exemple de ajustari pentru robustete

## 1. Scop

Acest document reuneste rezultatele auditului pentru toate fisierele `.cpp` si
`.hpp` din:

- `demo_sources/`
- `another_demo/`

Nu este o implementare completa a serverului IRC. Fragmentele de mai jos sunt
exemple izolate, intentionat scurte, care arata **cum trebuie gandit codul
robust**, de ce este necesar si unde se aplica. Ele nu trebuie copiate ca o
solutie completa fara integrarea cu restul proiectului.

Build-ul principal foloseste C++98, conform `Makefile`. De aceea exemplele
evita sa presupuna ca `std::unique_ptr`, `nullptr` sau `std::from_chars` sunt
disponibile in codul actual. Unde o solutie moderna ar fi preferabila, este
marcata explicit.

---

## 2. Verdict comparativ

| Criteriu | `demo_sources` | `another_demo` |
|---|---|---|
| Separarea responsabilitatilor | Mai monolitic, cu multa logica in `Server.cpp` | Mai buna: socket, lexer, AST si dispatcher separate |
| Framing TCP | Prezent: buffer persistent si delimitare `\r\n` | Lipseste: fiecare `recv()` este tratat ca mesaj |
| Scriere partiala | Buffer de iesire si tratare de `send()` partial | Rezultatul `send()` este ignorat |
| `SIGPIPE` | Protejat prin `MSG_NOSIGNAL` | Neprotejat |
| Ownership | Raw pointers, dar cleanup partial mai coerent | Raw pointers si leak de socket la fiecare client |
| Validare IRC | Mai avansata, dar incompleta | Minima si partial neconforma |
| Testabilitate | Cuplare mai mare la networking | `ISocket` permite mock-uri mai usor |
| Robusteate operationala | Cea mai buna dintre cele doua | Prototip arhitectural, dar fragil in executie |

**Recomandare:** `demo_sources` trebuie folosit ca baza pentru transportul TCP
si lifecycle, iar din `another_demo` merita pastrate separarea parserului si
dispatcherul. Inainte de extinderea functionalitatii IRC trebuie reparate
ownership-ul, framing-ul, corespondenta nickname/index si backpressure-ul.

---

## 3. Probleme prioritare

### 3.1 `another_demo`

1. **Framing TCP incorect**: [Server.cpp:92](/home/alexkatz/Documents/GitHub/PENDING/ft_irc/another_demo/srcs/Server.cpp:92)
   paseaza blocul primit direct lexerului.
2. **Transmisii partiale pierdute**:
   [TCPSocket.cpp:16](/home/alexkatz/Documents/GitHub/PENDING/ft_irc/another_demo/srcs/TCPSocket.cpp:16)
   returneaza direct rezultatul `send()`, iar caller-ul il ignora.
3. **Risc de terminare prin `SIGPIPE`** la scriere pe peer inchis.
4. **Leak de socket si file descriptor**:
   [User.cpp:6](/home/alexkatz/Documents/GitHub/PENDING/ft_irc/another_demo/srcs/User.cpp:6)
   nu elibereaza socketul primit.
5. **Signal handler nesigur**:
   [main.cpp:9](/home/alexkatz/Documents/GitHub/PENDING/ft_irc/another_demo/srcs/main.cpp:9)
   apeleaza `stop()` si operatii STL din context de semnal.
6. **Validare IRC redusa**: nickname-uri duplicate/nevalide, comenzi
   case-sensitive, raspunsuri si autentificare incomplete.

### 3.2 `demo_sources`

1. **Schimbarea nickname-ului dupa inregistrare poate desincroniza indexurile**:
   [Server.cpp:267](/home/alexkatz/Documents/GitHub/PENDING/ft_irc/demo_sources/srcs/Server.cpp:267).
2. **Buffer de iesire nelimitat**:
   [Server.cpp:379](/home/alexkatz/Documents/GitHub/PENDING/ft_irc/demo_sources/srcs/Server.cpp:379).
3. **Canalele goale raman in map**:
   [Server.cpp:390](/home/alexkatz/Documents/GitHub/PENDING/ft_irc/demo_sources/srcs/Server.cpp:390).
4. **Erori `accept()` ignorate**:
   [Server.cpp:152](/home/alexkatz/Documents/GitHub/PENDING/ft_irc/demo_sources/srcs/Server.cpp:152).
5. **Ownership implicit si copiere implicita** in
   [Server.hpp](/home/alexkatz/Documents/GitHub/PENDING/ft_irc/demo_sources/headers/Server.hpp).
6. **Validare si case-folding doar aproximative**, desi framing-ul este bun.

---

## 4. Ajustarea 1: RAII pentru file descriptor

### Problema

In ambele variante descriptorii sunt gestionati prin pointeri si functii
dispersate. Daca apare o exceptie sau o cale de iesire neasteptata intre
`socket()` si inregistrarea conexiunii, descriptorul poate ramane deschis.
In `another_demo`, `TCPSocket` este copiable implicit; doi destructori pot
incerca sa inchida acelasi descriptor.

### Exemplu de wrapper RAII, compatibil cu C++98

```cpp
class Fd
{
private:
    int _fd;

    Fd(const Fd&);
    Fd& operator=(const Fd&);

public:
    explicit Fd(int fd) : _fd(fd) {}

    ~Fd()
    {
        if (_fd >= 0)
            ::close(_fd);
    }

    int get() const { return _fd; }

    int release()
    {
        int result = _fd;
        _fd = -1;
        return result;
    }
};
```

### De ce este mai robust

- Daca `bind()` sau `listen()` esueaza, destructorul inchide automat fd-ul.
- Copierea este dezactivata; descriptorul are un singur proprietar.
- `release()` face transferul de ownership explicit.
- Codul de cleanup nu mai trebuie duplicat in fiecare ramura de eroare.

In C++11, acelasi rol ar fi exprimat mai clar cu un tip move-only si
`std::unique_ptr`/deleter. Pentru proiectul actual C++98 este preferabil un
wrapper RAII local decat pointeri raw cu ownership implicit.

### Aplicare in proiect

- `TCPSocket` ar trebui sa contina un `Fd`, nu un `int` copiable.
- `User` trebuie sa fie fie proprietarul explicit al socketului, fie sa
  pastreze o referinta non-owning. Nu trebuie amestecate cele doua modele.
- `Server::removeClient()` trebuie sa aiba un singur punct de inchidere si
  stergere.

---

## 5. Ajustarea 2: framing TCP persistent

### Problema

Un apel `recv()` nu corespunde unei comenzi IRC. Poate primi:

- jumatate de comanda;
- mai multe comenzi;
- o comanda completa si jumatate din urmatoarea.

In `another_demo`, blocul este transmis direct lexerului, deci inputul
fragmentat sau concatenat este interpretat gresit.

### Exemplu de buffer per conexiune

```cpp
void Connection::appendInput(const char* data, std::size_t size)
{
    if (_input.size() + size > MAX_PENDING_INPUT)
        throw ProtocolError("input buffer too large");

    _input.append(data, size);
}

bool Connection::popLine(std::string& line)
{
    std::string::size_type end = _input.find("\r\n");
    if (end == std::string::npos)
        return false;

    if (end + 2 > MAX_IRC_LINE)
        throw ProtocolError("IRC line too long");

    line.assign(_input, 0, end);
    _input.erase(0, end + 2);
    return true;
}
```

### Fluxul corect

```cpp
char buffer[4096];
ssize_t count = ::recv(fd, buffer, sizeof(buffer), 0);

if (count > 0)
{
    connection.appendInput(buffer, static_cast<std::size_t>(count));

    std::string line;
    while (connection.popLine(line))
        dispatch(parse(line));
}
```

### De ce este mai robust

- Fragmentul ramas este pastrat pana la urmatorul `recv()`.
- Mai multe linii din acelasi pachet sunt procesate separat.
- Limita se aplica atat unei linii, cat si restului neprocesat.
- Lexerul primeste numai linii complete, deci are un contract simplu si
  testabil.

`demo_sources` are deja aceasta directie in
[Server.cpp:181](/home/alexkatz/Documents/GitHub/PENDING/ft_irc/demo_sources/srcs/Server.cpp:181)
si [Server.cpp:195](/home/alexkatz/Documents/GitHub/PENDING/ft_irc/demo_sources/srcs/Server.cpp:195).
Aceeasi idee trebuie mutata intr-o componenta `Connection`/`LineFramer`, nu
lasata ingropata in `Server.cpp`.

---

## 6. Ajustarea 3: write queue si `send()` partial

### Problema

Pe un socket non-blocking, `send()` poate scrie doar o parte din mesaj. Un
client lent nu trebuie sa blocheze serverul si nici sa piarda restul mesajului.

### Exemplu de coada de iesire

```cpp
void Connection::queue(const std::string& message)
{
    if (_outputBytes + message.size() > MAX_OUTPUT_BYTES)
        throw SlowClient("output queue limit exceeded");

    _output.push_back(message);
    _outputBytes += message.size();
}

bool Connection::flush()
{
    while (!_output.empty())
    {
        std::string& current = _output.front();
        ssize_t sent = ::send(_fd, current.data(), current.size(),
                              MSG_NOSIGNAL);

        if (sent > 0)
        {
            current.erase(0, static_cast<std::size_t>(sent));
            _outputBytes -= static_cast<std::size_t>(sent);
            if (current.empty())
                _output.pop_front();
            continue;
        }
        if (sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
            return false; // pastram restul si asteptam POLLOUT
        if (sent < 0 && errno == EINTR)
            continue;
        throw SocketError("send failed");
    }
    return true;
}
```

### De ce este mai robust

- Nu se presupune ca un mesaj se transmite atomic.
- `POLLOUT` ramane activ cat timp exista date de trimis.
- Limita `MAX_OUTPUT_BYTES` previne consumul nelimitat de memorie.
- `MSG_NOSIGNAL` evita terminarea procesului la peer inchis.
- Un client care nu citeste poate fi deconectat controlat, in loc sa
  afecteze intregul server.

In `demo_sources`, aceasta idee exista partial, dar trebuie adaugata limita
de backpressure. In `another_demo`, dispatcherul nu trebuie sa scrie direct
cu `send()`; el trebuie sa apeleze `queue()`.

---

## 7. Ajustarea 4: tratarea corecta a rezultatelor POSIX

### Problema

`EAGAIN`, `EWOULDBLOCK`, `EINTR`, EOF si erorile permanente au semnificatii
distincte. In special, `recv() == -1` cu `EAGAIN` nu inseamna disconnect.

### Exemplu de tratare

```cpp
ssize_t count = ::recv(fd, buffer, sizeof(buffer), 0);

if (count > 0)
    appendInput(buffer, static_cast<std::size_t>(count));
else if (count == 0)
    closeClient("peer performed orderly shutdown");
else if (errno == EINTR)
    return; // retry la urmatoarea iteratie
else if (errno == EAGAIN || errno == EWOULDBLOCK)
    return; // nu mai exista date acum
else
    closeClient(std::strerror(errno));
```

### Acceptare robusta

```cpp
for (;;)
{
    int clientFd = ::accept(listenerFd, 0, 0);
    if (clientFd >= 0)
    {
        registerClient(clientFd);
        continue;
    }
    if (errno == EINTR)
        continue;
    if (errno == EAGAIN || errno == EWOULDBLOCK)
        break;
    logSystemError("accept");
    break;
}
```

Aceasta elimina comportamentul din `demo_sources` unde erorile `accept()`
diferite de `EAGAIN` sunt ignorate la
[Server.cpp:152](/home/alexkatz/Documents/GitHub/PENDING/ft_irc/demo_sources/srcs/Server.cpp:152).

---

## 8. Ajustarea 5: event loop fara invalidarea containerelor

### Problema

In `another_demo`, `_pollFds` poate fi modificat in timp ce este parcurs.
`push_back()` poate realoca vectorul, iar stergerea elementului curent poate
deplasa urmatorii indici.

### Varianta cu snapshot de descriptori

```cpp
std::vector<struct pollfd> ready = _pollFds;
for (std::size_t i = 0; i < ready.size(); ++i)
{
    const struct pollfd& event = ready[i];
    if (!isStillConnected(event.fd))
        continue;

    if (event.revents & (POLLERR | POLLHUP | POLLNVAL))
        markForRemoval(event.fd);
    else if (event.revents & POLLIN)
        readClient(event.fd);
}

removeMarkedClients();
addPendingClients();
```

### De ce este mai robust

- Lista procesata nu se schimba in timpul iteratiei.
- Clientii noi asteapta urmatoarea iteratie.
- Stergerile sunt amanate pana cand nu mai exista indici activi.
- `POLLERR`, `POLLHUP` si `POLLNVAL` devin cai explicite de inchidere.

O alternativa este folosirea unui map `fd -> Connection` si reconstruirea
listei `pollfd` la fiecare iteratie; pentru dimensiunea unui proiect
educational, claritatea este adesea mai importanta decat micro-optimizarea.

---

## 9. Ajustarea 6: indexuri consistente pentru nickname

### Problema

In `demo_sources`, `NICK` poate fi schimbat dupa inregistrare. Socketul,
obiectul `User` si mapa `users` pot ajunge sa contina nume diferite.

### Regula de baza

Schimbarea nick-ului trebuie sa fie o operatie atomica la nivelul modelului:

```cpp
bool UserRegistry::rename(User& user, const std::string& requested)
{
    std::string normalized = normalizeIrcNick(requested);
    if (!validNick(requested) || contains(normalized))
        return false;

    std::string oldName = normalizeIrcNick(user.nick());
    _byNick.erase(oldName);
    user.setNick(requested);
    _byNick[normalized] = &user;
    return true;
}
```

### Invarianta

La orice moment trebuie sa fie adevarat:

```text
users_by_nick[normalize(user.nick)] == &user
```

Handlerul `NICK` nu trebuie sa modifice direct campuri din `Socket` si `User`
in locuri diferite. El trebuie sa apeleze un singur serviciu de registru care
valideaza, verifica unicitatea si actualizeaza toate indexurile.

---

## 10. Ajustarea 7: canale si cleanup determinist

### Problema

La deconectare, `demo_sources` elimina membrul, dar pastreaza canalul gol.
Dupa suficiente canale temporare, memoria creste chiar daca nu mai exista
membri.

### Exemplu de cleanup

```cpp
void Server::removeMember(Channel& channel, User& user)
{
    channel.remove(user);
    user.leave(channel);

    if (channel.memberCount() == 0)
    {
        std::string key = normalizeChannel(channel.name());
        _channels.erase(key);
        delete &channel; // doar daca Server este proprietarul explicit
    }
}
```

In codul real, `delete &channel` trebuie inlocuit cu mecanismul de ownership
folosit efectiv. Ideea importanta este ca eliminarea din index si eliberarea
obiectului sa aiba acelasi proprietar si sa se intample intr-un singur loc.

De asemenea, relatiile `User <-> Channel` ar trebui sa foloseasca metode
private/publice controlate, nu membri publici precum `members` si `channels`.
Astfel se poate pastra invarianta bidirectionala:

```text
user.channels contine channel  <=>  channel.members contine user
```

---

## 11. Ajustarea 8: signal handling sigur

### Problema

Handlerul din `another_demo/main.cpp` apeleaza `stop()`, scrie in `std::cout`
si modifica obiecte complexe. Aceste operatii nu sunt async-signal-safe.

### Exemplu minimal

```cpp
volatile std::sig_atomic_t g_stop = 0;

extern "C" void handleSignal(int)
{
    g_stop = 1;
}
```

Bucla principala verifica flag-ul:

```cpp
while (!g_stop && server.isRunning())
{
    server.pollOnce();
}

server.stop(); // executat in context normal, nu in handler
```

### De ce este mai robust

- Handlerul face o singura operatie sigura.
- Cleanup-ul STL, `close()`, logging-ul si eliberarea memoriei raman in firul
  normal de executie.
- Nu exista reentranta in timp ce un container este modificat.

Pentru o implementare mai avansata se poate folosi `sigaction()` si un
self-pipe, dar principiul ramane acelasi: handlerul semnalului doar notifica,
nu executa shutdown-ul.

---

## 12. Ajustarea 9: parsing si validare explicita

### Problema

Ambele variante accepta doar o parte din regulile IRC. Un parser robust nu
trebuie sa produca o comanda aparent valida din input incomplet sau ambiguu.

### Exemplu de rezultat de parse explicit

```cpp
struct ParseResult
{
    bool ok;
    std::string command;
    std::vector<std::string> parameters;
    std::string error;

    ParseResult() : ok(false) {}
};
```

Parserul poate returna eroarea, iar dispatcherul decide raspunsul IRC:

```cpp
ParseResult parsed = parseLine(line);
if (!parsed.ok)
{
    connection.queue(makeNumericError(ERR_UNKNOWNCOMMAND,
                                      parsed.error));
    return;
}

dispatch(parsed.command, parsed.parameters, connection);
```

### Validare de port, fara `atoi()`

```cpp
char* end = 0;
long value = std::strtol(text, &end, 10);
if (*text == '\0' || *end != '\0' || value < 1 || value > 65535)
    throw InvalidArgument("invalid port");
```

Diferenta importanta fata de `atoi()` este ca se verifica intregul input:
`"123abc"` nu mai este acceptat ca portul `123`.

### Normalizare IRC

`std::tolower()` generic nu este suficient pentru case-folding IRC. Este
preferabil un helper dedicat care opereaza pe setul de caractere permis de
protocol, cu reguli documentate si teste pentru `[ ] \ { } | ^`.

---

## 13. Ajustarea 10: responsabilitati mai clare

`demo_sources` concentreaza networking, framing, parsing, state machine,
channels si raspunsuri in `Server.cpp`. O impartire mai robusta ar fi:

```text
Listener
  -> Connection / Fd
      -> LineFramer
          -> IrcParser
              -> CommandDispatcher
                  -> UserRegistry / ChannelRegistry
                      -> OutputQueue
```

Fiecare componenta are un contract verificabil:

- `Listener`: accepta si configureaza fd-uri;
- `Connection`: detine starea unei conexiuni si fd-ul;
- `LineFramer`: produce doar linii complete;
- `IrcParser`: valideaza sintaxa;
- `CommandDispatcher`: aplica regulile comenzii;
- registry-urile: mentin unicitatea si indexurile;
- `OutputQueue`: gestioneaza partial writes si limitele.

Separarea nu este un scop in sine. Ea este utila deoarece fiecare problema
critica poate fi testata independent, fara pornirea unui server complet.

---

## 14. Exemple de teste de robustete necesare

Fragmentele de cod de mai sus trebuie sustinute de teste pentru:

1. `PI` + `NG :abc\r\n` produce o singura comanda `PING`.
2. `PING :a\r\nPONG :b\r\n` produce doua comenzi.
3. O linie fara `\r\n` ramane in buffer, nu este dispatchuita.
4. Un `send()` partial pastreaza restul in coada.
5. `EAGAIN` nu inchide clientul.
6. EOF inchide clientul si elimina toate indexurile.
7. Nickname duplicat este respins fara modificarea starii existente.
8. Redenumirea actualizeaza simultan `User`, socket si registry.
9. Bufferul de output depasit produce disconnect controlat.
10. Un canal fara membri este eliminat.
11. SIGINT seteaza doar flag-ul, iar shutdown-ul se executa normal.
12. Inputul `123abc` este respins ca port.

---

## 15. Ordinea recomandata de implementare

1. Introducerea ownership-ului explicit pentru fd-uri, useri si canale.
2. Mutarea framing-ului intr-o componenta reutilizabila si testabila.
3. Introducerea `OutputQueue` cu partial writes si limita de memorie.
4. Tratarea completa a `EINTR`, `EAGAIN`, EOF si evenimentelor `poll`.
5. Repararea atomica a schimbarii de nickname.
6. Shutdown sigur prin flag `sig_atomic_t`.
7. Curatarea canalelor goale si a tuturor indexurilor.
8. Separarea parserului de transport si raspunsuri IRC standard.
9. Adaugarea testelor de fragmentare, disconnect, backpressure si validare.

## Concluzie

`demo_sources` are cea mai buna baza pentru robustetea networkingului:
framing, buffering si tratarea partiala a I/O sunt deja prezente. `another_demo`
are o separare arhitecturala mai buna pentru parser si comenzi, dar trebuie
reparat fundamental la nivel de stream TCP, ownership si send.

Solutia recomandata nu este alegerea exclusiva a uneia dintre variante, ci
combinarea lor: **transportul robust din `demo_sources` + modularitatea din
`another_demo` + ownership explicit si teste de protocol**.
