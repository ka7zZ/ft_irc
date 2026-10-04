# Consultare de design, algoritmi și surse de inspirație

## 1. Scop

Acest document reuneste consultarea unui coleg-agent senior specializat în
C++98, networking POSIX și servere IRC. Nu modifică sursele proiectului.
Conține recomandări de design și inventare de algoritmi/pattern-uri; nu este
încă o implementare aplicată.

Concluzia colegului este că structura actuală separă conceptual socketul,
utilizatorul, lexerul, AST-ul și dispatcherul, dar are nevoie de contracte
explicite pentru ownership, framing TCP, lifecycle și cozi de output înainte
de extinderea funcționalităților IRC.

## 2. Design recomandat al claselor

### `Server`

Responsabilități:

- configurarea listenerului;
- coordonarea event loop-ului;
- acceptarea conexiunilor;
- coordonarea registry-urilor;
- dispatch-ul evenimentelor;
- shutdown ordonat.

Nu trebuie să parseze direct linii, să apeleze `send()` direct sau să conțină
toată logica de validare a nickname-urilor.

Ownership recomandat:

```text
Server
 ├── ListenerSocket
 ├── ConnectionRegistry
 ├── NicknameRegistry
 ├── ChannelRegistry
 └── CommandDispatcher
```

### `ClientConnection` / `User`

Ideal, transportul și identitatea IRC sunt separate:

```text
ClientConnection
 ├── socket
 ├── input buffer
 ├── output queue
 ├── transport state
 └── disconnect/deadline state

UserSession
 ├── nickname
 ├── username
 ├── realname
 ├── hostname
 ├── authentication state
 └── channel membership
```

Dacă se păstrează clasa `User` existentă, ea trebuie să declare explicit:

- ownership-ul socketului;
- buffer de input;
- coada de output;
- starea de înregistrare;
- membership-ul în canale;
- starea de închidere.

### `ISocket` și `TCPSocket`

`TCPSocket` trebuie să fie singurul proprietar al descriptorului:

```text
fd >= 0  => TCPSocket închide fd-ul
fd < 0   => fd-ul este deja închis sau invalid
```

În C++98, copierea claselor owning trebuie dezactivată prin constructor de
copiere și operator `=` private. Nu se combină ownership-ul din `Server` cu
ownership-ul din `User`.

### `Listener`

Responsabilități:

- `socket()`;
- `setsockopt(SO_REUSEADDR)`;
- `bind()`;
- `listen()`;
- configurarea non-blocking;
- `accept()` repetat până la `EAGAIN`.

Listenerul nu trebuie să creeze logică IRC.

### `EventLoop`

Responsabilități:

- menținerea setului de `pollfd`;
- activarea/dezactivarea `POLLIN` și `POLLOUT`;
- procesarea evenimentelor;
- snapshot sau removals amânate;
- verificarea faptului că fd-ul există înainte de fiecare operație.

Invariantă:

```text
fiecare fd activ apare o singură dată în poll set
```

### `LineFramer`

Responsabilități:

- acumularea octeților TCP;
- detectarea delimitatorului `\r\n`;
- extragerea liniilor complete;
- păstrarea fragmentului incomplet;
- limitarea liniei și a bufferului.

Nu tokenizează și nu execută comenzi.

### `IRCParser`

Responsabilități:

- transformarea unei linii complete într-un rezultat sintactic;
- validarea prefixului, comenzii și parametrilor;
- raportarea explicită a erorii;
- garantarea că AST-ul valid reprezintă o linie validă.

### `CommandDispatcher`

Responsabilități:

- normalizarea comenzii;
- verificarea handlerului;
- verificarea stării de înregistrare;
- delegarea către servicii;
- generarea răspunsurilor prin coada de output.

Nu trebuie să apeleze `send()` direct.

### `NicknameRegistry`

Responsabilități:

- `nickname normalizat -> user`;
- unicitate;
- rezervare atomică;
- redenumire fără stare intermediară invalidă;
- eliminare la disconnect.

Forma originală se păstrează pentru afișare, iar forma normalizată pentru
lookup.

### `Channel` și `ChannelRegistry`

`ChannelRegistry` mapează nume normalizate la canale. Invariantele sunt:

```text
user.channels conține channel
    <=> channel.members conține user
```

La disconnect, userul este scos din toate canalele, canalele devenite goale
sunt eliminate conform politicii proiectului, apoi sunt eliminate nickname-ul
și conexiunea.

## 3. Algoritmi de implementare recomandați

### Framing TCP

```text
recv într-o buclă
  -> append exact bytes primiți
  -> verifică limitele
  -> caută repetat \r\n
  -> extrage fiecare linie completă
  -> păstrează fragmentul rămas
```

Reguli:

- maximum 512 octeți pentru linia IRC, inclusiv `\r\n`;
- buffer per conexiune, nu global;
- limită totală de input, de exemplu 4–16 KiB;
- disconnect dacă limita este depășită fără delimitator;
- `recv() == 0` înseamnă EOF și disconnect ordonat;
- `EINTR` se repetă;
- `EAGAIN`/`EWOULDBLOCK` revine la event loop.

Nu se folosește `std::string(buffer)` după `recv()`, deoarece lungimea
validă este rezultatul lui `recv()`, iar datele pot conține NUL.

### Event loop cu `poll()`

```text
pregătește pollfd
poll()
copiează evenimentele ready
procesează snapshot-ul
aplică eliminările și interesele noi
```

Ordinea recomandată:

1. `POLLNVAL` -> eliminare;
2. `POLLERR` -> diagnostic și eliminare;
3. `POLLHUP` -> eventual drenare, apoi disconnect;
4. `POLLIN` -> citire;
5. dacă userul există încă, `POLLOUT` -> flush output.

Listenerul este golit prin `accept()` repetat până la `EAGAIN`.

### Input buffering

Fluxul per conexiune:

```text
recv -> append -> limite -> extract lines -> parse -> dispatch
```

Pentru echitate, se recomandă un buget maxim de comenzi procesate per
conexiune într-o singură iterație; altfel un client poate produce starvation
trimițând mii de comenzi într-un singur pachet.

### Output buffering și backpressure

```text
queue message
  -> activează POLLOUT
  -> send când socketul este pregătit
  -> elimină numai bytes trimiși
  -> păstrează restul
  -> dezactivează POLLOUT când coada devine goală
```

Rezultate:

```text
count > 0              elimină bytes transmiși
EINTR                   repetă
EAGAIN/EWOULDBLOCK      păstrează restul
EPIPE/ECONNRESET       disconnect
altă eroare            disconnect
```

Coada are watermark maxim. Pentru proiectul educațional, disconnect-ul
controlat al clientului lent este mai predictibil decât pierderea tăcută a
mesajelor.

### Lifecycle

```text
Accepted
 -> Connected
 -> Negotiating/Unregistered
 -> Registered
 -> Closing
 -> Closed
```

Disconnect-ul trebuie să fie idempotent:

1. marchează conexiunea ca închisă;
2. o scoate din canale;
3. eliberează nickname-ul;
4. o scoate din registry;
5. o scoate din poll set;
6. distruge socketul;
7. eliberează sesiunea.

Două evenimente de închidere pentru același fd nu trebuie să producă
double-delete sau use-after-free.

### Registry nickname

```text
validate format
normalize
check duplicate
reserve new key
remove old key
update user
```

La eșec, nickname-ul vechi rămâne intact. Lookup-ul într-un `map<int, User*>`
este acceptabil pentru prototip, dar un registry separat oferă lookup O(log n).

### Registry canale și broadcast

La broadcast se construiește lista destinatarilor înainte de transmitere,
pentru a evita invalidarea iteratorilor dacă un destinatar este eliminat ca
efect al unei erori. Fiecare destinație primește mesajul în propria coadă,
niciodată prin `send()` sincron către toți membrii.

### Parsing

Validarea se face în etape:

1. eliminare exactă `\r\n`;
2. prefix opțional;
3. comandă obligatorie;
4. parametri intermediari;
5. trailing parameter;
6. număr și tip de parametri;
7. normalizare comandă;
8. rezultat explicit `ok/error`.

`peekToken()` trebuie să verifice indexul, iar AST-ul nu trebuie să fie
considerat valid dacă nu are comandă sau dacă mai rămân tokeni invalizi.

### Shutdown

Handlerul `SIGINT` setează doar un `volatile sig_atomic_t`. Event loop-ul
observă flagul și execută shutdown-ul normal. Dacă `poll(..., -1)` poate rămâne
blocat, se folosește timeout periodic, self-pipe sau `eventfd` pe Linux.

Ordinea shutdown-ului:

1. oprește acceptarea;
2. opțional trimite mesaj de shutdown;
3. închide clienții;
4. distruge registry-urile;
5. închide listenerul;
6. lasă toate obiectele într-o stare destructibilă și idempotentă.

## 4. Sfaturi și capcane

### Evoluție recomandată

1. ownership și reguli de copiere;
2. abstracție de conexiune;
3. framing și limite input;
4. output queue și backpressure;
5. erori POSIX;
6. event loop stabil;
7. lifecycle explicit;
8. signal handling;
9. parser cu rezultat explicit;
10. nickname registry;
11. channel registry și cleanup;
12. numerice IRC;
13. teste unitare, integrare și stres.

### Capcane

- double close;
- use-after-free după procesarea `POLLIN`;
- iterator invalidation la broadcast;
- starvation;
- unbounded memory;
- silent data loss;
- `SIGPIPE`;
- EOF tratat ca eroare generică;
- case folding inconsistent;
- stare parțială la `NICK`;
- parser prea permisiv;
- STL folosit în handler de semnal;
- logging excesiv în event loop;
- folosirea neizolată a API-urilor non-portabile;
- sintaxă IRC proprie în locul răspunsurilor standard.

## 5. Surse de inspirație și ajutor

### RFC-uri și standarde

- **RFC 1459 — Internet Relay Chat Protocol** — comenzi și model IRC;
- **RFC 2811 — Internet Relay Chat: Channel Management** — canale;
- **RFC 2812 — Internet Relay Chat: Client Protocol** — mesaje, limita 512,
  numerice;
- **RFC 7194 — Default Port for IRC via TLS/SSL** — transport IRC securizat;
- **POSIX.1-2008 / The Open Group Base Specifications** — API POSIX;
- **ISO/IEC 14882:1998** — C++98;
- **ISO/IEC 14882:2011, 2014, 2017, 2020, 2023** — evoluția C++.

### Manual pages

`socket(2)`, `setsockopt(2)`, `bind(2)`, `listen(2)`, `accept(2)`,
`connect(2)`, `recv(2)`, `send(2)`, `poll(2)`, `fcntl(2)`, `close(2)`,
`sigaction(2)`, `signal(7)`, `signal-safety(7)`, `errno(3)`,
`getaddrinfo(3)`, `tcp(7)`, `ip(7)`.

### Instrumente

- **GCC / Clang** — build strict cu `-std=c++98 -Wall -Wextra -Werror`;
- **GDB** — debugging și examinarea descriptorilor;
- **Valgrind Memcheck** — leak-uri și accesări invalide;
- **AddressSanitizer** — erori de memorie;
- **UndefinedBehaviorSanitizer** — comportament nedefinit;
- **strace** — `poll`, `accept`, `recv`, `send`, `close`;
- **lsof** — descriptor-i rămași deschiși;
- **netcat (`nc`)** — testare TCP și fragmentare;
- **socat** — conexiuni și închideri controlate;
- **telnet** — testare interactivă de bază;
- **clang-tidy** — analiză statică;
- **cppcheck** — analiză statică suplimentară;
- **Wireshark / tcpdump** — traficul TCP;
- **GoogleTest / Catch2** — teste unitare, dacă dependențele sunt permise.

## 6. Inventar C++98: algoritmi, pattern-uri și context

Conform cerinței, această secțiune oferă denumiri și contexte scurte, nu
tutoriale.

- **RAII** — ownership pentru socketuri, fd-uri și resurse;
- **Rule of Three** — clase care dețin descriptor sau memorie;
- **Pimpl** — reducerea dependențelor din header;
- **Facade** — API simplificat peste POSIX;
- **Adapter** — adaptarea `ISocket` la TCP;
- **Strategy** — politici de framing, backpressure și normalizare;
- **State Pattern** — stările conexiunii IRC;
- **Command Pattern** — maparea comenzilor la handlers;
- **Registry Pattern** — nickname-uri și canale;
- **Observer Pattern** — notificarea membrilor unui canal;
- **Mediator Pattern** — comunicarea între useri și canale;
- **Object Pool** — reutilizarea obiectelor de conexiune;
- **Iterator Pattern** — parcurgerea membrilor și descriptorilor;
- **Snapshot Iteration** — evenimente `poll` fără invalidare;
- **Two-Phase Removal** — marcare și eliminare ulterioară;
- **Producer–Consumer Queue** — coada de output;
- **Finite State Machine** — lifecycle și registration;
- **Linear Scan** — lookup în prototipuri;
- **Ordered Map Lookup** — lookup O(log n) cu `std::map`;
- **Case Folding / Canonicalization** — lookup case-insensitive;
- **Delimiter-Based Framing** — separare prin `\r\n`;
- **Watermark / Backpressure** — limite pentru output;
- **Retry on EINTR** — reluarea syscall-urilor;
- **Edge-safe Drain Loop** — golirea accept/read queue până la `EAGAIN`;
- **Fail-Fast Validation** — respingerea inputului invalid;
- **Null Object** — absența canalului sau prefixului;
- **Dependency Inversion** — dependență de `ISocket`;
- **Single Responsibility Principle** — separarea transport/parsing/domeniu;
- **Ownership Graph** — documentarea responsabilităților;
- **Idempotent Cleanup** — disconnect și shutdown repetabile.

## 7. Inventar C++ modern: C++11–C++23

### C++11

- **Move Semantics** — transfer eficient al bufferelor;
- **`std::unique_ptr`** — ownership exclusiv;
- **`std::shared_ptr` / `std::weak_ptr`** — ownership partajat;
- **`enum class`** — stări și tokeni;
- **Lambda Expressions** — callback-uri;
- **`std::unordered_map`** — registry cu lookup mediu O(1);
- **`std::array`** — buffer fix;
- **`std::chrono`** — timeout-uri;
- **`std::condition_variable`** — coordonare multi-thread;
- **`static_assert`** — verificări compile-time;
- **Variadic Templates** — logging și dispatch generic;
- **`std::thread`** — execuție concurentă.

### C++14

- **Generic Lambdas** — callback-uri generice;
- **Return Type Deduction** — utilitare interne;
- **`std::make_unique`** — ownership exclusiv sigur;
- **Binary Literals / Digit Separators** — constante;
- **Relaxed `constexpr`** — logică compile-time.

### C++17

- **`std::optional`** — rezultate opționale;
- **`std::variant`** — rezultate alternative de parsing;
- **`std::string_view`** — procesare fără copiere;
- **`std::filesystem`** — configurare și logging;
- **Structured Bindings** — parcurgere registry;
- **`if constexpr`** — infrastructură generică;
- **`std::scoped_lock`** — locking multiplu;
- **`std::pmr`** — alocare specializată;
- **`std::invoke`** — dispatch generic;
- **`std::shared_mutex`** — registry-uri concurente.

### C++20

- **Concepts** — interfețe generice restricționate;
- **Coroutines** — modele asincrone;
- **Ranges** — pipeline-uri de procesare;
- **`std::span`** — view sigur peste buffer;
- **`std::jthread`** — thread cu stop cooperativ;
- **`std::stop_token`** — anulare controlată;
- **`std::latch` / `std::barrier`** — coordonare;
- **Three-Way Comparison** — ordonarea tipurilor;
- **Calendar and Time-zone Facilities** — deadline-uri.

### C++23

- **`std::expected`** — valoare sau eroare explicită;
- **`std::print` / `std::format`** — logging formatat;
- **`std::mdspan`** — view multidimensional;
- **`std::generator`** — generare lazy;
- **`std::ranges` extins** — procesare declarativă;
- **`std::stacktrace`** — diagnostic;
- **`std::flat_map`** — registry mic și predominant citit;
- **`std::byteswap`** — conversii de reprezentare;
- **Deduced `this`** — API-uri generice;
- **Monadic Operations** — compunerea rezultatelor.

Pentru proiectul actual, implementarea trebuie să rămână C++98. Facilitățile
moderne sunt direcții de evoluție și nu trebuie introduse ca dependențe în
soluția imediată.
