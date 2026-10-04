# Consultanta de design pentru IRC server — patternuri, surse si catalog de algoritmi

> Am dus problema de design a claselor (Server / Connection / User / Channel /
> Dispatcher) la un coleg senior, specializat pe cod robust si algoritmi
> clasici. Mai jos: ce patternuri de design mi-a recomandat pentru acest
> proiect concret, de unde spune el ca se documenteaza de fiecare data cand
> ajunge intr-o astfel de situatie, si doua capitole separate cu catalogul
> brut de algoritmi/idiomuri — unul pentru C++98, unul pentru C++ modern —
> doar denumiri si contextul de aplicare, fara descrieri lungi, asa cum ai
> cerut.

---

## 1. Recomandari de design de clase, aplicate direct pe acest proiect

Colegul a insistat pe un lucru inainte de orice: **nu alegi un pattern
pentru ca "suna bine", il alegi pentru problema exacta de mai sus din
audit.** Fiecare recomandare de mai jos e legata de o problema concreta din
`AUDIT_ROBUSTETE_SI_AJUSTARI.md` sau din `AUDIT_CAZURI_LIMITA_SI_IMPLEMENTARE.md`.

| Pattern / idiom | Unde se aplica in proiect | Problema pe care o rezolva |
|---|---|---|
| **RAII** | `Fd`, `Connection` | ownership de fd, cleanup garantat (Ajustarea 1) |
| **Non-copyable idiom** | `Fd`, `Connection`, `Socket` | previne dublu-close pe acelasi fd |
| **Rule of Three** | orice clasa cu resurse (fd, pointeri) | evita copiere implicita periculoasa |
| **Facade** | `Server` | ascunde `Listener + Registry + Dispatcher` in spatele unei singure interfete simple pentru `main()` |
| **Mediator** | `Server` intre `User` si `Channel` | `User` si `Channel` nu se cunosc direct unul pe celalalt decat prin `Server`, deci nu pot crea dependinte circulare greu de testat |
| **Registry (index pattern)** | `UserRegistry`, `ChannelRegistry` | un singur loc care garanteaza unicitatea nickname-urilor/canalelor (Ajustarea 6) |
| **State (masina de stari)** | ciclul de viata al `Connection`/`User`: `CONNECTED -> PASS_OK -> NICK_SET -> USER_SET -> REGISTERED` | previne comenzi acceptate intr-o ordine gresita (ex: `JOIN` inainte de `NICK`) |
| **Command** | fiecare comanda IRC (`NickCommand`, `JoinCommand`, `KickCommand`...) ca obiect cu `execute()` | dispatcher-ul devine o simpla harta `nume -> Command*`, usor de extins fara `if/else` gigantic |
| **Strategy** | validarea parametrilor per comanda | fiecare comanda isi valideaza singura parametrii, fara logica dispersata in `Server` |
| **Factory Method** | generarea raspunsurilor numerice IRC (`makeNumericError`, `makeWelcome`...) | centralizeaza formatul exact al mesajelor, testabil izolat |
| **Observer** | `Channel` notifica membrii la `broadcast()` | separa "ce s-a intamplat" de "cine trebuie anuntat" |
| **Adapter** | interfata `ISocket` peste `::socket`/`::recv`/`::send` (idee buna din `another_demo`) | permite mock-uri in teste, fara conexiuni TCP reale |
| **Dependency Injection (manuala)** | `Server` primeste `ISocket*`/`Listener*` prin constructor, nu le creeaza intern | testabilitate; poti injecta un socket fals in teste unitare |
| **Non-Virtual Interface (NVI)** | clase de baza cu o metoda publica non-virtuala care cheama una privata virtuala | control central asupra a ce se intampla inainte/dupa logica specifica derivatei (ex: logare, validare comuna) |
| **Erase-remove idiom** | curatarea `vector`-elor de membri/canale | evita exact bug-urile de iterator invalidation din C2/C13 |
| **Named Constructor idiom** | `Connection::fromAcceptedSocket(int fd)` in loc de constructor public cu logica ambigua | previne constructia unei conexiuni intr-o stare incompleta/invalida |
| **Sentinel / Null Object** | un `Channel::none()` sau verificare explicita `findChannel() == 0` | evita ramificatii `if (ptr)` uitate, standardizeaza raspunsul "nu exista" |

**Sfatul lui de baza, repetat de mai multe ori:** intr-un proiect mic de
scoala nu trebuie folosite toate aceste patternuri simultan — riscul e
supra-inginerie. Cele pe care le considera **obligatorii** pentru acest
proiect, in ordinea introducerii: RAII + non-copyable (ai nevoie de ele
imediat), Registry (rezolva bug-ul de nickname), State machine pentru
inregistrare, Command pentru dispatcher. Restul (Observer, Adapter, DI,
NVI) le-a numit "imbunatatiri de faza a doua", utile mai ales daca vrei sa
scrii teste unitare fara sa deschizi conexiuni TCP reale.

---

## 2. Surse de inspiratie — la ce apeleaza el de fiecare data

I-am cerut explicit lista lui personala, cea la care revine indiferent de
proiect. Cam asa arata:

**Carti de referinta pentru design C++**
- *Effective C++* si *More Effective C++* — Scott Meyers
- *Effective STL* — Scott Meyers
- *Exceptional C++* si *Exceptional C++ Style* — Herb Sutter
- *C++ Coding Standards: 101 Rules, Guidelines, and Best Practices* — Herb Sutter & Andrei Alexandrescu
- *Design Patterns: Elements of Reusable Object-Oriented Software* — Gamma, Helm, Johnson, Vlissides (GoF)
- *Modern C++ Design* — Andrei Alexandrescu (chiar daca proiectul e C++98, ideile de policy-based design ii clarifica de ce anumite idiomuri C++98 exista)
- *The C++ Programming Language* — Bjarne Stroustrup

**Carti de referinta pentru networking/sisteme (partea "grea" a acestui proiect)**
- *Unix Network Programming, Volume 1* — W. Richard Stevens — sursa lui principala pentru `socket`, `poll`, `select`, `SIGPIPE`, framing TCP
- *Advanced Programming in the UNIX Environment* — Stevens & Rago — pentru semnale, fd-uri, signal-safety
- *TCP/IP Illustrated* — W. Richard Stevens — pentru intelegerea garantiilor reale ale TCP (de ce framing-ul e obligatoriu)
- *Beej's Guide to Network Programming* — ghid liber online, primul loc unde trimite pe cineva nou la sockets

**Standarde si documentatie normativa**
- *C++ Core Guidelines* (Stroustrup & Sutter, isocpp.github.io) — regulile lui de baza pentru ownership, RAII, exception safety
- man pages POSIX: `man 2 recv`, `man 2 send`, `man 2 poll`, `man 2 accept`, `man 7 signal` — le citeste mereu din nou pentru codurile de eroare exacte, niciodata din memorie
- RFC 1459 si RFC 2812 — specificatia protocolului IRC, pentru raspunsurile numerice si sintaxa exacta a comenzilor

**Resurse online recurente**
- cppreference.com — pentru semnatura exacta si garantiile fiecarei functii din STL/libc pe care le foloseste
- *Guru of the Week* (GotW) — Herb Sutter — seria de "puzzle-uri" clasice despre exception safety, ownership, const-correctness
- isocpp.org — FAQ-uri oficiale ale comitetului de standardizare

**Unelte pe care le ruleaza sistematic, nu doar "daca are timp"**
- `-Wall -Wextra -Werror -std=c++98` la fiecare compilare
- Valgrind (`--leak-check=full`) pentru leak-uri de memorie si fd
- AddressSanitizer / UndefinedBehaviorSanitizer, cand toolchain-ul permite, pentru exact genul de use-after-free din C3/C10/C11
- `cppcheck` sau `clang-tidy` ca a doua opinie statica, separat de compilator

**Codebase-uri reale pe care le studiaza pentru arhitectura (nu pentru copiat cod)**
- servere IRC open-source consacrate — ngIRCd, InspIRCd, UnrealIRCd — studiate strict pentru cum isi separa transportul de protocol, niciodata pentru a copia cod (licente diferite, plus scopul e sa intelegi decizia, nu sa o clonezi)

---

## 3. Capitolul I — Algoritmi si idiomuri pentru C++98

Doar denumiri si contextul de aplicare, fara descrieri, asa cum ai cerut.

### Managementul resurselor si al obiectelor
- **RAII (Resource Acquisition Is Initialization)** — fd-uri de socket, fisiere, mutex-uri (daca ar exista threading)
- **Rule of Three** — orice clasa care detine un pointer/resursa bruta
- **Non-copyable idiom** (copy ctor + `operator=` private, nedefinite) — `Fd`, `Connection`, `TCPSocket`
- **Pimpl idiom (pointer to implementation)** — ascunderea detaliilor de implementare din header, izolare la recompilare
- **`std::auto_ptr`** — transfer de ownership cu semantica exception-safe, inainte de `unique_ptr`
- **Copy-and-swap idiom** — implementarea sigura a `operator=` pentru clase cu resurse
- **Named Constructor idiom** — constructie controlata, validata, cu nume explicit al intentiei
- **Construct-on-first-use idiom** — evitarea "static initialization order fiasco" la obiecte globale/statice
- **Meyers' Singleton (functie locala cu `static`)** — singleton fara riscul ordinii de initializare globale (folosit cu grija, nu implicit recomandat)

### Design orientat pe obiecte si polimorfism
- **Non-Virtual Interface (NVI) idiom** — control centralizat in jurul unei metode virtuale private
- **Template Method pattern** — algoritm fix, pasi inlocuibili in clase derivate
- **Strategy pattern** — comportament interschimbabil (validare, parsare) fara `switch` gigantic
- **Command pattern** — comenzi ca obiecte, utile pentru dispatcher-ul IRC
- **State pattern (masina de stari explicita)** — ciclul de inregistrare al unui `User`/`Connection`
- **Observer pattern** — notificare la evenimente (broadcast catre canal)
- **Visitor pattern / double dispatch** — parcurgerea unui AST (relevant pentru `another_demo`)
- **Adapter pattern** — `ISocket` peste apeluri POSIX brute, pentru testabilitate
- **Facade pattern** — `Server` ca punct unic de intrare peste subsistemele interne
- **CRTP (Curiously Recurring Template Pattern)** — polimorfism static, fara cost de vtable

### Containere si algoritmi STL clasici (C++98)
- **Erase-remove idiom** (`std::remove_if` + `container.erase`) — curatarea unei liste de membri/canale fara iterator invalidation
- **`std::find` / `std::find_if`** — cautare liniara intr-un `vector` (ex: cautarea unui user intr-o lista mica de membri)
- **`std::sort` (implementare tipica: introsort)** — ordonare, daca e nevoie de afisare sortata (ex: `NAMES`)
- **`std::binary_search` / `std::lower_bound`** — cautare rapida intr-un container deja sortat
- **`std::transform`** — normalizare in masa (ex: case-folding pe o lista de nickname-uri)
- **`std::for_each`** — aplicarea unei operatii pe fiecare element (alternativa la bucla explicita)
- **Functori (`std::unary_function`, `std::binary_function`, `std::mem_fun`, `std::bind1st`/`std::bind2nd`)** — predicate reutilizabile pentru algoritmii STL de mai sus
- **Arbore binar echilibrat (implementarea interna a `std::map`/`std::set`)** — indexare O(log n) pentru `UserRegistry`/`ChannelRegistry`
- **Coada dubla / `std::deque`** — buffer de iesire cu extragere eficienta din fata (`OutputQueue`)

### Retea, I/O si concurenta la nivel de sistem
- **I/O multiplexing cu `select()`** — varianta veche, cu limita `FD_SETSIZE` (vezi C1 din auditul de cazuri limita)
- **I/O multiplexing cu `poll()`** — varianta recomandata in acest proiect, fara limita fixa de fd
- **I/O multiplexing cu `epoll()` (Linux) / `kqueue()` (BSD)** — varianta scalabila pentru multe conexiuni, dincolo de scopul unui proiect educational
- **Bucla non-blocking cu retry pe `EAGAIN`/`EWOULDBLOCK`/`EINTR`** — pattern standard pentru `recv()`/`send()`/`accept()` pe socket non-blocant
- **Partial read/write loop** — acumularea/trimiterea corecta cand `recv()`/`send()` returneaza mai putin decat cerut
- **Sliding window / streaming parser pentru framing TCP** — extragerea liniilor `\r\n` dintr-un flux continuu de octeti
- **Producer-consumer queue (fara thread-uri, doar structural)** — `OutputQueue` per conexiune
- **Token bucket / leaky bucket** — limitarea ratei de comenzi per client (protectie flood, extensie utila peste `MAX_OUTPUT_BYTES`)
- **Exponential backoff** — reincercare controlata la reconectare/retry de sistem, daca serverul initiaza vreodata conexiuni catre alt server IRC
- **Semnal async-signal-safe cu `volatile std::sig_atomic_t`** — shutdown controlat la `SIGINT`/`SIGTERM`

### Parsare si validare
- **Finite State Machine (FSM) explicit, cu enum + switch** — parsarea liniei IRC caracter cu caracter, alternativa la un lexer generic
- **Two-pointer / index scanning** — separarea prefixului, comenzii si parametrilor dintr-o linie IRC
- **`strtol`-based strict parsing** — validare numerica stricta (port, limite de canal), fara ambiguitatile lui `atoi`

---

## 4. Capitolul II — Algoritmi si tehnici din C++ modern (C++11 -> C++23)

Din nou, doar denumiri si context — util de stiut pentru ziua in care
proiectul (sau altul viitor) nu mai are constrangerea C++98.

### C++11 / C++14
- **Move semantics si rvalue references (`&&`)** — transfer de resurse fara copiere (inlocuieste multe motive pentru `Fd`/`auto_ptr` scrise manual)
- **`std::unique_ptr`** — ownership unic, move-only, inlocuieste direct wrapper-ul RAII manual (`Fd`)
- **`std::shared_ptr` / `std::weak_ptr`** — ownership partajat cu numarare de referinte; `weak_ptr` rezolva elegant "verifica daca obiectul mai exista" din C3/C13
- **Lambda expressions si closures** — predicate inline pentru algoritmii STL, fara functori separati
- **`std::function`** — type erasure pentru callback-uri (ex: handler de comanda ca `std::function<void(...)>`, inlocuieste `Command` pattern clasic in multe cazuri)
- **`std::bind`** — legarea partiala de argumente (inlocuit ulterior, in mare parte, de lambda-uri)
- **Variadic templates** — functii/clase cu numar variabil de argumente tip-sigure
- **`auto` si `decltype`** — deducerea tipului, reduce zgomotul sintactic la iteratori/tipuri complexe
- **Range-based for loop** — iterare fara iteratori expliciti
- **`std::thread`, `std::mutex`, `std::condition_variable`** — concurenta reala, daca serverul ar deveni multi-threaded
- **`std::atomic`** — operatii fara lacate pentru flag-uri simple (inlocuieste `volatile sig_atomic_t` in cazuri mai generale)
- **`nullptr`** — inlocuieste `0`/`NULL` cu tip dedicat, elimina ambiguitati de overload
- **`enum class`** — enumerari scoped si type-safe (inlocuieste "type-safe enum idiom" din C++98)
- **`constexpr`** — calcul la compilare pentru constante/functii simple
- **Delegating constructors** — un constructor cheama alt constructor al aceleiasi clase
- **`override` si `final`** — verificare explicita la compilare a intentiei de suprascriere

### C++17
- **`std::optional`** — valoare opționala fara sentinel/pointer null (inlocuieste "gaseste sau nu gaseste user" fara pointeri)
- **`std::variant` + `std::visit`** — uniune tip-sigura, utila pentru rezultate de parsare cu mai multe forme posibile
- **`std::string_view`** — vedere non-owning peste un sir, parsare fara copii (util direct in framing/tokenizare)
- **`std::any`** — type erasure generic, alternativa la `void*`
- **Structured bindings** — descompunerea unui `pair`/`tuple` direct in variabile numite
- **`if constexpr`** — ramificare la compilare in cod template, alternativa la SFINAE explicit
- **`std::filesystem`** — operatii pe fisiere/cai portabile (util daca serverul are configurare din fisier)
- **Fold expressions** — reducerea pachetelor de parametri variadici intr-o singura expresie
- **Class template argument deduction (CTAD)** — omiterea parametrilor de template la constructie

### C++20
- **Concepts** — constrangeri explicite pe parametri de template, erori de compilare mult mai clare decat SFINAE
- **Ranges library** — compunere de algoritmi STL in stil "pipeline" (`| filter | transform`)
- **Coroutines (`co_await`, `co_yield`, `co_return`)** — I/O asincron fara callback-uri imbricate; alternativa moderna directa la bucla de `poll()` scrisa manual
- **`std::span`** — vedere non-owning peste un bloc contiguu de memorie (similar cu `string_view`, dar generic)
- **`std::jthread`** — thread cu RAII si `stop_token` incorporat, oprire cooperativa fara flag-uri manuale
- **Modules** — alternativa la `#include`/header guards, compilare mai rapida si izolare mai buna
- **Three-way comparison (`operator<=>`)** — genereaza automat toate operatiile de comparare

### C++23 (si tendinte curente)
- **`std::expected`** — reprezentarea unui rezultat "valoare sau eroare" fara exceptii, util direct pentru `ParseResult`-ul din auditul de baza
- **`std::mdspan`** — vedere multi-dimensionala peste memorie (relevant mai ales in calcul numeric, mai putin in acest proiect)
- **Deducing this** — simplificare pentru implementarea NVI/CRTP fara duplicare cod const/non-const

### Design de nivel inalt pentru servere de retea, in ecosistemul modern
- **Reactor pattern** — event loop bazat pe notificare de "gata pentru citire/scriere" (echivalentul modern conceptual al buclei `poll()` din acest proiect)
- **Proactor pattern** — I/O asincron unde sistemul de operare face operatia si notifica la finalizare (ex: `io_uring` pe Linux, IOCP pe Windows)
- **Policy-based design** — comportament configurabil prin parametri de template (Alexandrescu), alternativa la mostenire pentru variatii de comportament
- **Type erasure idiom** — expunerea unei interfete uniforme peste tipuri diferite, fara mostenire clasica (`std::function`, `std::any` sunt exemple deja standard)

---

## 5. Concluzia consultarii

Recomandarea lui finala, pastrata ca atare: pentru proiectul actual (C++98,
obligatoriu), seteaza intai fundatia obligatorie din sectiunea 1 — RAII,
non-copyable, Registry, State machine, Command — pentru ca ele rezolva
direct bug-urile deja gasite in audit. Catalogul din capitolele I si II nu
e o lista de "de facut acum", ci un index de recunoastere: cand intalnesti
o problema noua, cauti in lista denumirea care descrie exact forma
problemei (ownership -> RAII/unique_ptr, "verifica daca mai exista" ->
weak_ptr/Registry, "prea multe cai de eroare" -> `std::expected`/`ParseResult`),
si abia apoi cauti implementarea potrivita standardului de limbaj pe care
il ai voie sa il folosesti.
