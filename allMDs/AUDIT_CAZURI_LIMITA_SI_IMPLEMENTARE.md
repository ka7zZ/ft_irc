# Audit de robustete IRC server — cazuri limita de crash si implementare C++98

> Acest document continua auditul `AUDIT_ROBUSTETE_SI_AJUSTARI.md`. Acolo au
> fost identificate problemele mari de arhitectura (framing, ownership,
> partial write, SIGPIPE etc.). Aici cobor un nivel mai jos: caut punctual
> **situatiile concrete in care serverul ar crapa** (segfault, exceptie
> nepusa, `abort()`, blocare) si dau pentru fiecare o solutie C++98 completa,
> explicata pas cu pas ca pentru cineva care inca nu are reflexele de
> networking/IPC formate.

---

## 0. Cateva notiuni, pe scurt, inainte sa intram in cod

Daca termenii de mai jos iti sunt deja clari, sari direct la sectiunea 1.

- **Socket** = un "capat de fir telefonic" pe care procesul tau il tine
  deschis. Il identifici printr-un numar intreg, `fd` (*file descriptor*).
  Orice `fd` deschis (socket, fisier, pipe) trebuie inchis exact o data,
  altfel ramane "agatat" (leak) sau, si mai rau, poate fi inchis de doua ori
  (crash).
- **`recv()` / `send()`** = citesc, respectiv scriu, octeti pe un socket.
  Amandoua sunt operatii *partiale*: `recv()` poate intoarce mai putini
  octeti decat exista in flux, iar `send()` poate scrie mai putini octeti
  decat i-ai cerut. Niciuna din ele nu stie nimic despre "comenzi IRC";
  ele stiu doar de octeti.
- **IPC** (*inter-process communication*) = mecanismul prin care doua procese
  separate (aici: server si client, procese diferite, eventual pe masini
  diferite) isi trimit date. Un socket TCP este un canal IPC.
- **IRC** = protocolul text de deasupra TCP-ului. O "comanda" IRC e o linie
  de text terminata cu `\r\n` (ex: `NICK ana\r\n`). TCP-ul **nu garanteaza**
  ca o comanda IRC ajunge intr-un singur `recv()`. Poate ajunge taiata in
  doua, sau pot ajunge trei comenzi lipite.
- **`poll()`** = functia care ii spune serverului "pe ce socket-uri ai date
  de citit sau poti scrie fara sa blochezi". E folosita ca sa nu stea
  serverul blocat intr-un singur `recv()` cat timp alti 50 de clienti
  asteapta.
- **Iterator invalidation** = atunci cand modifici un `vector`/`map` (adaugi
  sau stergi elemente) in timp ce il parcurgi cu un iterator sau cu indici
  retinuti dinainte, iar acele pozitii/adrese devin invalide. Continuarea
  folosirii lor e comportament nedefinit (de multe ori: crash imediat, alte
  ori: crash mult mai tarziu si greu de reprodus).
- **Use-after-free / dangling pointer** = folosesti un pointer catre un
  obiect care a fost deja `delete`-uit. Obiectul "nu mai exista", dar
  pointerul inca "arata" spre acea zona de memorie.
- **Async-signal-safe** = o functie care poate fi apelata in siguranta din
  interiorul unui *signal handler* (ex: la `Ctrl+C`). `malloc`, `new`,
  `std::cout`, orice metoda STL **nu** sunt async-signal-safe.

Cu asta lamurit, trecem la cazurile limita.

---

## 1. Cum am cautat cazurile limita

Metoda e simpla si merita retinuta pentru orice server de retea: pentru
fiecare functie care primeste date de la exterior (client, argumente de
linie de comanda, alt server), imi pun sistematic aceleasi intrebari:

1. Ce se intampla daca **lipsesc date** (parametru absent, buffer gol)?
2. Ce se intampla daca sunt **prea multe date** (linie gigantica, mii de
   conexiuni, mesaj repetat la infinit)?
3. Ce se intampla daca datele sunt **malformate** (octeti invalizi, numere
   in afara plajei, text unde ma astept la cifre)?
4. Ce se intampla daca operatia esueaza **partial** (`send()`/`recv()` care
   nu termina tot)?
5. Ce se intampla daca doua evenimente **se suprapun in timp** (clientul
   se deconecteaza chiar in mijlocul procesarii altei comenzi de-a lui,
   sau primeste un semnal de sistem chiar cand serverul modifica o lista)?
6. Cine este proprietarul acestei resurse si **cine are voie sa o stearga**?

Fiecare caz de mai jos e o gasire concreta pornind de la una din aceste
intrebari.

---

## 2. Taxonomie rapida

| Categorie | Simptom tipic | Cazuri in acest document |
|---|---|---|
| Memorie (use-after-free, double free) | `SIGSEGV`, `SIGABRT` | C3, C10, C11 |
| Containere (iterator invalidation, `.at()`) | `SIGSEGV`, `std::out_of_range` neprins | C2, C4, C5, C13 |
| Sistem de operare (semnale, limite de fd) | `SIGPIPE`, `SIGSEGV`, blocare | C1, C7, C8, C15 |
| Protocol / parsing | consum infinit de memorie, comportament nedefinit | C6, C9, C12 |
| Gestionarea exceptiilor | `terminate()` -> tot procesul moare | C14 |

---

## 3. Cazurile limita, pe rand

### C1. `select()` cu `fd >= FD_SETSIZE` — corupere de stiva, nu doar bug

**Scenariu.** Daca undeva in cod (chiar si intr-o versiune veche/alternativa
a serverului) se foloseste `select()` in loc de `poll()`, `select()`
lucreaza cu un `fd_set`, care este intern un bitmap de dimensiune fixa
(`FD_SETSIZE`, de obicei 1024). `FD_SET(fd, &set)` **nu verifica limita**.
Daca serverul ruleaza suficient timp si acumuleaza destule conexiuni (sau
pur si simplu procesul are deja alte fd-uri deschise), un `fd` poate depasi
1024. `FD_SET` scrie atunci in afara bitmap-ului -> stiva sau memoria
adiacenta se corupe. Nu e o exceptie frumoasa, e corupere de memorie care
poate crapa cu totul altceva, mult mai tarziu.

**De ce e periculos special intr-un context "de aparare"/productie:** e
genul de bug care trece testele functionale (functioneaza perfect cu 5, 50,
chiar 500 de clienti) si pica exact sub sarcina reala, quasi-imposibil de
reprodus determinist.

**Solutie.** In acest proiect folosim exclusiv `poll()` (sau `epoll`), care
nu au aceasta limita fixa — lista de `pollfd` e un `std::vector`, creste cat
e nevoie. Adaugam insa si o garda explicita, ca sa nu "alunece" cineva
inapoi spre `select()` fara sa observe:

```cpp
// Undeva central, langa initializarea listener-ului:
#include <climits>

void Server::assertNoSelectAssumptions(int fd)
{
    // Aceasta functie nu foloseste select(); e doar o plasa de siguranta
    // documentata, pentru cazul in care cineva adauga ulterior cod care
    // amesteca select() cu poll() pe acelasi fd.
    (void)fd; // pastram semnatura pentru viitor, fara sa impunem o limita falsa
}
```

Punctul important de retinut (mai valoros decat codul de mai sus): **alegeti
`poll()`/`epoll` si documentati explicit in `Server.hpp` ca `select()` este
interzis in acest proiect**, exact ca sa preveniti reintroducerea lui de
catre cineva care nu stie de limita `FD_SETSIZE`.

---

### C2. Stergerea unui client din `map`/`vector` in timp ce il parcurgi

**Scenariu.** Bucla de `poll()` gaseste ca doi clienti trebuie deconectati
in aceeasi iteratie (unul a trimis `QUIT`, altul a inchis conexiunea).
Codul naiv:

```cpp
// GRESIT
for (std::map<int, User*>::iterator it = _users.begin();
     it != _users.end(); ++it)
{
    if (shouldDisconnect(it->second))
        _users.erase(it); // it a devenit invalid; ++it de la for e UB
}
```

`erase(it)` invalideaza iteratorul `it`. Bucla `for` incearca apoi `++it`
pe un iterator mort. Cu `std::map` (bazat pe arbore) efectul e adesea
imediat: `SIGSEGV`. Cu `std::vector`, `erase()` in plus **deplaseaza** toate
elementele de dupa pozitia stearsa, deci indici retinuti separat (de
exemplu intr-un `pollfd` snapshot) ajung sa se refere la alt client decat
cel intentionat.

**Solutie elegantă (C++98): colectezi mai intai, stergi dupa.**

```cpp
void Server::reapDisconnectedUsers()
{
    // 1) Prima trecere: doar citim, nu modificam containerul.
    std::vector<int> toRemove;
    for (std::map<int, User*>::const_iterator it = _users.begin();
         it != _users.end(); ++it)
    {
        if (shouldDisconnect(it->second))
            toRemove.push_back(it->first);
    }

    // 2) A doua trecere: stergem, pe o lista separata, deci fara sa
    //    afectam iteratia de mai sus (care oricum s-a incheiat deja).
    for (std::vector<int>::const_iterator id = toRemove.begin();
         id != toRemove.end(); ++id)
    {
        removeClient(*id); // un singur punct de inchidere + stergere, vezi C10
    }
}
```

De ce functioneaza: separam citirea de scriere in doi pasi distincti.
Prima bucla nu modifica niciodata `_users`, deci iteratorii ei raman valizi
pana la capat. A doua bucla lucreaza pe `toRemove`, un container complet
separat, si apeleaza o singura functie de cleanup (`removeClient`) care
face toata treaba de stergere intr-un singur loc — asa evitam si duplicarea
logicii de cleanup in mai multe locuri din cod (sursa clasica de leak-uri).

Aceeasi tehnica de "marcheaza, apoi aplica dupa" e folosita si in auditul
de baza pentru `_pollFds` (`markForRemoval` + `removeMarkedClients`); e
acelasi principiu, aplicat aici direct pe `map`-ul de useri.

---

### C3. Use-after-free la broadcast intr-un canal in timp ce se sterge un membru

**Scenariu.** Un handler de `PART` sau `KICK` sterge un user dintr-un canal
**in timp ce** o alta parte a codului (de exemplu un mesaj de bun venit
broadcastat catre toti membrii) inca tine un pointer/referinta catre acel
`User`. Sau, mai insidios: procesarea unui singur pachet TCP contine
`KICK cineva\r\nPRIVMSG #canal :salut\r\n` — dupa ce `KICK` sterge userul
si (daca `Server` e proprietar) face `delete`, linia urmatoare din acelasi
pachet incearca sa foloseasca un pointer catre un obiect deja sters.

```cpp
// GRESIT: broadcast cu pointeri care pot deveni invalizi in timpul iteratiei
void Channel::broadcast(const std::string& msg, User* except)
{
    for (std::size_t i = 0; i < members.size(); ++i)
    {
        if (members[i] != except)
            members[i]->connection().queue(msg); // ce faci daca members[i]
                                                   // tocmai a fost sters de
                                                   // un handler anterior din
                                                   // aceeasi bucla de linii?
    }
}
```

**Solutie: iteram pe o copie a listei de membri, nu pe lista live, si
verificam explicit ca fiecare membru e inca valid.**

```cpp
void Channel::broadcast(const std::string& msg, const User* except)
{
    // Copiem lista de pointeri (nu obiectele!) inainte de a incepe sa
    // trimitem. Daca cineva modifica lista "vie" de membri in timpul
    // trimiterii (ex: un KICK provocat indirect), acea modificare loveste
    // copia _originalului_, nu snapshot-ul pe care il parcurgem acum.
    std::vector<User*> snapshot = members;

    for (std::size_t i = 0; i < snapshot.size(); ++i)
    {
        User* member = snapshot[i];

        // isStillMember() interogheaza registry-ul curent (sursa unica de
        // adevar), nu presupune ca pointerul e valid doar pentru ca a fost
        // valid in momentul copierii listei.
        if (member == except || !server.isStillMember(member))
            continue;

        member->connection().queue(msg);
    }
}
```

Ideea centrala, valabila peste tot unde iterezi si "lucrurile din lista"
pot disparea in timpul iteratiei: **nu itera direct structura pe care o
poate modifica altcineva in acelasi timp; itereaza o copie, si verifica
inainte de fiecare folosire ca obiectul tinta mai exista** intr-un registru
central (vezi C10 pentru cum construim acel registru).

---

### C4. Acces la parametri de comanda fara sa verifici cati au venit

**Scenariu.** Un handler scris rapid:

```cpp
// GRESIT
void handleJoin(const std::vector<std::string>& params, Connection& conn)
{
    std::string channel = params[0]; // daca JOIN a venit fara parametri...
    joinChannel(conn, channel);
}
```

Un client rau-intentionat (sau pur si simplu un client cu un bug) trimite
`JOIN\r\n`, fara canal. `params` e un `vector` gol. `params[0]`
(`operator[]`) **nu verifica limitele** — e comportament nedefinit, de
multe ori crash imediat, alteori citirea unei zone de memorie arbitrare.

**Solutie: acces "safe" cu verificare explicita, plus raspuns IRC corect
in loc de crash.**

```cpp
// Un mic helper reutilizabil in tot dispatcher-ul:
static bool paramAt(const std::vector<std::string>& params,
                     std::size_t index, std::string& out)
{
    if (index >= params.size())
        return false;
    out = params[index];
    return true;
}

void handleJoin(const std::vector<std::string>& params, Connection& conn)
{
    std::string channel;
    if (!paramAt(params, 0, channel))
    {
        // ERR_NEEDMOREPARAMS (461), raspunsul standard IRC, nu un crash.
        conn.queue(makeNumericError(461, "JOIN :Not enough parameters"));
        return;
    }
    joinChannel(conn, channel);
}
```

De ce e "elegant" si nu doar "safe": nu doar ca previne crash-ul, ci
transforma un input invalid intr-un **raspuns de protocol corect**
(codul numeric IRC standard pentru "parametri insuficienti"). Clientul afla
exact ce a gresit, in loc ca serverul sa cada pentru toata lumea din cauza
unui singur client neatent. Aceeasi regula (`paramAt` peste tot, niciodata
`params[i]` direct in handlere) elimina dintr-o singura miscare o intreaga
clasa de bug-uri.

---

### C5. Exceptii STL neprinse (`std::out_of_range`, `std::length_error`) care opresc tot serverul

**Scenariu.** `std::string::at()` si `std::string::substr()` arunca
`std::out_of_range` daca pozitia depaseste dimensiunea sirului. Alte
operatii (concatenare foarte lunga) pot arunca `std::length_error` sau
`std::bad_alloc`. Daca aceste exceptii nu sunt prinse **nicaieri intre
punctul unde apar si `main()`**, apelul `std::terminate()` se declanseaza
automat si **intreg procesul moare**, luand cu el toti clientii conectati,
nu doar pe cel care a provocat problema.

```cpp
// GRESIT: o singura linie proasta din partea unui singur client
// poate opri serverul pentru toata lumea.
std::string prefix = line.substr(0, 500); // arunca daca line.size() < 500? nu,
                                            // substr(pos) arunca doar daca pos
                                            // > size(); dar pos calculat gresit
                                            // in alta parte poate depasi usor
```

**Solutie in doua straturi.**

Stratul 1 — evitam sa aruncam acolo unde putem verifica dinainte in loc sa
ne bazam pe exceptie:

```cpp
std::string safeSubstr(const std::string& s, std::string::size_type pos,
                        std::string::size_type len = std::string::npos)
{
    if (pos > s.size())
        return std::string(); // input invalid -> rezultat gol, nu exceptie
    return s.substr(pos, len);
}
```

Stratul 2 — o plasa de siguranta la nivelul buclei principale, pentru orice
am uitat sa prindem punctual. Acesta este stratul care conteaza cel mai
mult intr-un server care trebuie sa ramana in picioare:

```cpp
void Server::run()
{
    while (!g_stop)
    {
        try
        {
            pollOnce(); // aici se intampla tot: accept, recv, parse, dispatch
        }
        catch (const ProtocolError& e)
        {
            // Eroare legata de un singur client -> il deconectam pe el,
            // serverul continua neafectat pentru restul clientilor.
            logProtocolError(e.what());
        }
        catch (const std::exception& e)
        {
            // Plasa de siguranta finala: nu lasam o exceptie scapata sa
            // opreasca tot procesul. O logam si continuam bucla.
            logSystemError(e.what());
        }
    }
}
```

De ce ambele straturi, si nu doar unul: stratul 1 (verificare explicita) e
mai rapid si iti da un mesaj de eroare IRC prietenos exact la locul
problemei (ca la C4). Stratul 2 (try/catch global) e plasa care garanteaza
ca, indiferent cate cazuri limita am ratat in cod, **un singur client
defect nu poate opri niciodata tot serverul**. Intr-un proiect de tip
"trebuie sa reziste la orice input", stratul 2 e obligatoriu chiar daca
stratul 1 e foarte bine facut.

---

### C6. Linie IRC fara `\r\n` niciodata — epuizare de memorie printr-un singur socket

**Scenariu.** Auditul de baza a introdus deja `MAX_PENDING_INPUT` la
`appendInput()`. Cazul limita suplimentar: ce se intampla **exact** cand
limita e depasita? Daca reactia e doar `throw` si undeva mai sus exceptia
scapa neprinsa (vezi C5), un singur client rau-intentionat care trimite
octeti la infinit, fara `\r\n`, poate opri tot serverul, nu doar conexiunea
lui.

**Solutie: exceptia dedicata trebuie prinsa exact la nivelul conexiunii,
niciodata mai sus.**

```cpp
class ProtocolError : public std::runtime_error
{
public:
    explicit ProtocolError(const std::string& msg)
        : std::runtime_error(msg) {}
};

void Server::readClient(int fd)
{
    Connection& conn = connectionFor(fd);
    char buffer[4096];
    ssize_t count = ::recv(fd, buffer, sizeof(buffer), 0);

    if (count > 0)
    {
        try
        {
            conn.appendInput(buffer, static_cast<std::size_t>(count));

            std::string line;
            while (conn.popLine(line))
                dispatch(parseLine(line), conn);
        }
        catch (const ProtocolError& e)
        {
            // Doar acest client plateste pentru comportamentul lui.
            conn.queue(makeNumericError(417, "Input too long"));
            markForRemoval(fd);
        }
        return;
    }
    // ... restul cazurilor de recv() ca in sectiunea 7 a auditului de baza
}
```

Regula generala aici: **o eroare provocata de un singur client trebuie
tratata la granita acelui client** (in jurul lui `readClient`), niciodata
lasata sa se propage pana la bucla principala a serverului. Bucla principala
(vezi C5, stratul 2) ramane doar plasa finala pentru bug-uri neprevazute,
nu mecanismul normal de tratare a input-ului rau de la clienti.

---

### C7. Epuizarea file descriptorilor (`EMFILE`/`ENFILE`) tratata gresit la `accept()`

**Scenariu.** Fiecare proces are o limita de file descriptori deschisi
simultan (`ulimit -n`, de multe ori 1024 implicit). Daca serverul accepta
clienti fara nicio limita proprie, la un moment dat `accept()` incepe sa
esueze cu `EMFILE` (procesul si-a atins limita) sau `ENFILE` (limita e la
nivel de sistem). Codul din auditul de baza deja opreste bucla de `accept`
corect la orice eroare diferita de `EAGAIN`/`EINTR` — dar cazul limita
suplimentar e ce se intampla **dupa**: daca serverul nu are propria limita
de clienti, va incerca sa accepte iar si iar la fiecare iteratie de
`poll()`, generand un flux continuu de erori `EMFILE` si consumand CPU
degeaba (un fel de busy-loop), fara sa refuze politicos clientii in plus.

**Solutie: o limita explicita de clienti, verificata *inainte* de
`accept()`, plus un mesaj de refuz curat cand limita e atinsa.**

```cpp
static const std::size_t MAX_CLIENTS = 1024;

void Server::acceptPendingConnections()
{
    for (;;)
    {
        if (_users.size() >= MAX_CLIENTS)
        {
            // Nu mai incercam accept() deloc; lasam conexiunile in coada
            // TCP de backlog pana cand un client existent se deconecteaza
            // si elibereaza un loc. Sistemul de operare le va tine acolo.
            break;
        }

        int clientFd = ::accept(_listenFd, 0, 0);
        if (clientFd >= 0)
        {
            registerClient(clientFd);
            continue;
        }
        if (errno == EINTR)
            continue;
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            break;

        // EMFILE / ENFILE / alte erori de sistem: logam si ne oprim din
        // acceptat pentru iteratia curenta, fara sa opreste serverul.
        logSystemError("accept");
        break;
    }
}
```

De ce conteaza verificarea *inainte* de `accept()`, nu doar reactia la
eroare: previne busy-loop-ul (CPU la 100% incercand `accept()` la infinit)
si face limita explicita si testabila (`MAX_CLIENTS`), in loc sa depinda
de limita implicita, ascunsa, a sistemului de operare.

---

### C8. `SIGPIPE`/`EPIPE` la broadcast, nu doar la scriere directa

**Scenariu.** Auditul de baza a rezolvat deja `SIGPIPE` la `send()`
individual, cu `MSG_NOSIGNAL`. Cazul limita ramas: intr-un `broadcast()`
catre 50 de membri ai unui canal, daca unul dintre ei tocmai a inchis
conexiunea (dar serverul inca nu a procesat evenimentul `POLLHUP` pentru
el), `send()` catre acel client intoarce eroare. Daca bucla de broadcast
trateaza orice eroare de `send()` ca fatala pentru tot broadcast-ul
(`throw` care opreste bucla `for`), restul de 49 de clienti nu mai primesc
deloc mesajul.

**Solutie: eroarea unui singur destinatar nu trebuie sa opreasca livrarea
catre ceilalti.**

```cpp
void Channel::broadcast(const std::string& msg, const User* except)
{
    std::vector<User*> snapshot = members; // ca la C3

    for (std::size_t i = 0; i < snapshot.size(); ++i)
    {
        User* member = snapshot[i];
        if (member == except || !server.isStillMember(member))
            continue;

        // queue() nu arunca niciodata din cauza starii altui client; daca
        // acest client a atins limita lui de output (backpressure), doar
        // el va fi marcat pentru deconectare (vezi ajustarea 3 din auditul
        // de baza), fara sa afecteze livrarea catre restul canalului.
        try
        {
            member->connection().queue(msg);
        }
        catch (const SlowClient&)
        {
            markForRemoval(member->connection().fd());
        }
    }
}
```

Regula: **o operatie "unu-la-multi" (broadcast) trebuie sa izoleze eroarea
fiecarui destinatar in parte**; un client lent sau mort nu are voie sa
opreasca livrarea catre restul.

---

### C9. Amplificare de memorie prin fan-out la broadcast catre canale mari

**Scenariu.** `MAX_OUTPUT_BYTES` per conexiune (din auditul de baza)
protejeaza corect un singur client care nu citeste. Dar exista un caz mai
subtil: daca un canal are 500 de membri si cineva trimite un mesaj de 400
de octeti, `broadcast()` pune acel mesaj in coada **fiecaruia** dintre cei
500 — 200.000 de octeti alocati dintr-o singura comanda de client. Daca
100 de clienti fac asta simultan catre 500 de canale mari, memoria creste
foarte rapid, desi fiecare limita individuala (`MAX_OUTPUT_BYTES`) e
respectata.

**Solutie: partajarea continutului mesajului intre destinatari, in loc de
copiere per client, plus o limita globala de siguranta.**

```cpp
class SharedMessage
{
public:
    explicit SharedMessage(const std::string& text)
        : _text(new std::string(text)), _refCount(new long(0)) {}

    SharedMessage(const SharedMessage& other)
        : _text(other._text), _refCount(other._refCount)
    {
        ++(*_refCount);
    }

    ~SharedMessage()
    {
        if (--(*_refCount) < 0)
        {
            delete _text;
            delete _refCount;
        }
    }

    const std::string& text() const { return *_text; }

private:
    std::string* _text;
    long* _refCount;

    SharedMessage& operator=(const SharedMessage&); // neimplementat, C++98
};
```

Nota importanta pentru un proiect educational: aceasta e o mica implementare
manuala de "shared ownership", pentru ca C++98 nu are `std::shared_ptr`
(acela a venit odata cu `boost`/C++11). **Nu este obligatoriu** sa
implementati asta de la inceput — pentru un proiect de scoala,
`MAX_OUTPUT_BYTES` per client, plus o limita simpla si documentata de
"numar maxim de membri pe canal" (`MAX_CHANNEL_MEMBERS`), e o solutie mult
mai simpla si suficient de robusta:

```cpp
static const std::size_t MAX_CHANNEL_MEMBERS = 200;

bool Channel::canAcceptNewMember() const
{
    return members.size() < MAX_CHANNEL_MEMBERS;
}
```

Alegeti solutia in functie de cerintele reale ale proiectului: `SharedMessage`
e "corect" pentru productie la scara mare, dar `MAX_CHANNEL_MEMBERS` e
suficient si mult mai usor de explicat/testat pentru un proiect de tip
`ft_irc`.

---

### C10. Un `User` traieste in 3 structuri diferite; stergerea din una singura lasa dangling pointers in celelalte

**Scenariu.** Tipic intr-un IRC server, un `User*` apare in:

- `_users` (map de la `fd` la `User*`) — pentru bucla de `poll`;
- `UserRegistry::_byNick` (map de la nickname normalizat la `User*`) —
  pentru comenzi ca `PRIVMSG catre_nick`;
- lista de membri a fiecarui `Channel` la care userul a facut `JOIN`.

Daca `removeClient()` sterge userul doar din `_users` si face `delete`,
celelalte doua structuri raman cu pointeri catre memorie eliberata.
Urmatorul `PRIVMSG catre_nick_vechi` sau urmatorul `broadcast()` pe un
canal la care userul era membru va folosi un pointer mort -> use-after-free.

**Solutie: un singur punct de "moarte" a unui user, care curata toate cele
trei structuri, in ordine, inainte de `delete`.**

```cpp
void Server::removeClient(int fd)
{
    std::map<int, User*>::iterator it = _users.find(fd);
    if (it == _users.end())
        return; // deja curatat; apel dublu accidental nu face nimic rau

    User* user = it->second;

    // 1) Scoatem userul din toate canalele la care era membru. copiem
    //    lista canalelor lui, exact ca la C3, ca sa nu iteram o structura
    //    pe care removeMember() o modifica pe masura ce inainteaza.
    std::vector<Channel*> channels = user->channels();
    for (std::size_t i = 0; i < channels.size(); ++i)
        removeMember(*channels[i], *user); // vede si cleanup canal gol, sectiunea 10 din auditul de baza

    // 2) Scoatem userul din registry-ul de nickname-uri.
    _userRegistry.forget(*user);

    // 3) Scoatem userul din harta de fd-uri.
    _users.erase(it);

    // 4) Inchidem socketul (prin Fd, vezi ajustarea 1 din auditul de baza)
    //    si abia acum eliberam memoria obiectului.
    delete user;
}
```

De ce ordinea conteaza: pasul 1 trebuie sa se intample **inainte** de
`delete user`, pentru ca `removeMember` inca are nevoie sa citeasca starea
userului (de exemplu nickname-ul lui, pentru mesajul de `PART` trimis
celorlalti membri). Daca ai sterge userul intai si ai incerca sa-l scoti
din canale dupa, ai avea deja un use-after-free chiar in interiorul functiei
de cleanup.

Regula generala, si cea mai importanta din acest document: **daca un obiect
traieste in mai multe colectii simultan, trebuie sa existe exact o singura
functie care il scoate din toate**, apelata din exact un singur loc
(niciodata `delete` imprastiat prin cod). Aceasta e exact recomandarea din
sectiunea 4 a auditului de baza ("un singur punct de inchidere si
stergere"), dusa pana la ultimul detaliu de ordine.

---

### C11. Comenzi multiple in acelasi pachet TCP, dintre care una deconecteaza userul

**Scenariu.** Cazul cel mai subtil din acest document. Un client trimite
intr-un singur pachet TCP:

```text
QUIT :la revedere\r\nPRIVMSG #canal :mai vorbesc?\r\n
```

Ambele linii ajung in acelasi `recv()`. Bucla din sectiunea 5 a auditului
de baza le extrage pe rand cu `popLine()` si le proceseaza una cate una,
**in aceeasi iteratie**:

```cpp
// GRESIT daca dispatch(QUIT) sterge imediat `conn`
std::string line;
while (connection.popLine(line))
    dispatch(parse(line), connection); // connection poate fi deja "moarta"
                                        // dupa primul dispatch, daca a fost QUIT
```

Daca handler-ul de `QUIT` apeleaza direct `removeClient()` (care sterge si
face `delete` pe `Connection`/`User`), a doua iteratie a buclei `while`
foloseste `connection` — un obiect deja sters. Use-after-free clasic,
declansat exclusiv de ordinea octetilor intr-un singur pachet TCP trimis
de client, complet in afara controlului serverului.

**Solutie: deconectarea nu se intampla niciodata "pe loc"; se marcheaza, iar
stergerea efectiva se face dupa ce bucla curenta s-a terminat complet.**

```cpp
void Server::readClient(int fd)
{
    Connection& conn = connectionFor(fd);
    char buffer[4096];
    ssize_t count = ::recv(fd, buffer, sizeof(buffer), 0);
    if (count <= 0)
    {
        handleRecvEdgeCase(fd, count); // vezi sectiunea 7 din auditul de baza
        return;
    }

    conn.appendInput(buffer, static_cast<std::size_t>(count));

    std::string line;
    while (conn.popLine(line))
    {
        // isMarkedForRemoval: verificam INAINTE de fiecare linie, nu doar
        // o singura data la inceput, tocmai pentru cazul QUIT + alta comanda
        // in acelasi pachet.
        if (isMarkedForRemoval(fd))
            break; // restul liniilor din acest pachet sunt ignorate; userul
                    // deja a cerut sa plece, nu mai proceseaza comenzi noi

        dispatch(parseLine(line), conn);
    }
    // Stergerea fizica se intampla o singura data, la finalul iteratiei
    // curente de poll(), in reapDisconnectedUsers() (vezi C2). Niciun
    // handler individual nu apeleaza direct delete/removeClient().
}

void Server::handleQuitCommand(Connection& conn, const std::string& reason)
{
    conn.queue(makeQuitNotice(reason));
    markForRemoval(conn.fd()); // doar marcheaza; nu sterge nimic acum
}
```

Aceasta e, de fapt, aceeasi tehnica de la C2 ("marcheaza, apoi aplica dupa"),
dar aplicata acum **si in interiorul buclei de procesare a liniilor**, nu
doar in bucla de `poll()`. E genul de caz limita care nu apare niciodata in
teste manuale (unde tastezi comenzile una cate una, in pachete TCP
separate), dar apare garantat cand un client automat sau un script trimite
mai multe comenzi deodata — exact genul de test pe care un audit din zona
de aparare trebuie sa il ia in calcul explicit.

---

### C12. `strtol`/`atoi` pe parametri numerici din comenzi IRC (ex: `MODE #canal +l 100`)

**Scenariu.** Extensie directa a ajustarii 9 din auditul de baza (acolo era
despre portul din linia de comanda). Acelasi risc exista pentru orice
parametru numeric venit **de la un client**, nu doar din `argv`. Exemplu:
comanda `MODE #canal +l <limita>` seteaza limita de membri ai canalului.
Cu `atoi("abc")` obtii `0` in tacere; cu `atoi("999999999999999")` obtii
comportament nedefinit prin overflow. Un `0` acceptat fara verificare poate
insemna "canal cu limita zero membri" — posibil sa blocheze permanent
canalul, sau sa produca valori negative interpretate gresit in alta parte a
codului (de exemplu ca "fara limita").

**Solutie: acelasi wrapper `strtol` ca in auditul de baza, reutilizat aici
si validat pe o plaja specifica domeniului (limita de canal, nu port).**

```cpp
bool parsePositiveLimit(const std::string& text, long& out)
{
    if (text.empty())
        return false;

    char* end = 0;
    errno = 0;
    long value = std::strtol(text.c_str(), &end, 10);

    // Toate conditiile trebuie verificate, nu doar una:
    if (*end != '\0')       return false; // au ramas caractere neconsumate ("abc" dupa numar)
    if (errno == ERANGE)    return false; // overflow/underflow la strtol insusi
    if (value <= 0)         return false; // limita de canal trebuie sa fie strict pozitiva
    if (value > 100000)     return false; // limita superioara de bun-simt pentru domeniul aplicatiei

    out = value;
    return true;
}

void handleChannelLimitMode(Channel& channel, const std::string& text,
                             Connection& conn)
{
    long limit = 0;
    if (!parsePositiveLimit(text, limit))
    {
        conn.queue(makeNumericError(696, "MODE :Invalid limit parameter"));
        return;
    }
    channel.setMemberLimit(static_cast<std::size_t>(limit));
}
```

De ce fiecare linie din verificare e necesara si niciuna nu poate lipsi:
`*end != '\0'` prinde `"100abc"`; `errno == ERANGE` prinde numerele atat de
mari incat `strtol` insusi a dat overflow intern (situatie in care valoarea
returnata de `strtol` e `LONG_MAX`/`LONG_MIN`, nu eroare vizibila altfel);
`value <= 0` si limita superioara sunt regulile specifice acestui domeniu
(o limita de canal negativa sau absurda nu are sens, chiar daca `strtol` a
"reusit" tehnic sa o parseze).

---

### C13. Modificarea listei de membri a unui canal chiar in timpul unui `KICK` in masa

**Scenariu.** O comanda gen `KICK #canal nick1,nick2,nick3` (kick multiplu,
o extensie comuna a protocolului) itereaza lista de nickname-uri primite si
pentru fiecare apeleaza `removeMember()`. Daca `removeMember()` sterge si
canalul insusi cand ramane fara membri (sectiunea 10 din auditul de baza),
iar bucla de `KICK` mai are inca elemente de procesat din lista de
nickname-uri, urmatoarea iteratie foloseste `channel` — obiect deja sters.

```cpp
// GRESIT
void handleKick(Channel& channel, const std::vector<std::string>& nicks)
{
    for (std::size_t i = 0; i < nicks.size(); ++i)
    {
        User* target = findUser(nicks[i]);
        if (target)
            server.removeMember(channel, *target); // poate sterge `channel`
                                                      // la ultimul membru!
    }
    // daca lista initiala avea exact toti membrii canalului, la a doua
    // iteratie `channel` e deja dangling
}
```

**Solutie: verificam explicit, dupa fiecare pas, daca obiectul pe care
urmeaza sa-l mai folosim inca exista.**

```cpp
void handleKick(const std::string& channelName,
                const std::vector<std::string>& nicks)
{
    for (std::size_t i = 0; i < nicks.size(); ++i)
    {
        // Cautam canalul DIN NOU la fiecare pas, prin numele lui, in loc
        // sa pastram o referinta/pointer catre el peste mai multe iteratii.
        // Daca a fost sters intre timp (canal ramas gol), findChannel()
        // intoarce pur si simplu 0, iar bucla se opreste curat.
        Channel* channel = server.findChannel(channelName);
        if (!channel)
            break; // canalul nu mai exista; restul nick-urilor nu mai au unde fi date afara

        User* target = server.findUser(nicks[i]);
        if (!target || !channel->hasMember(*target))
            continue;

        server.removeMember(*channel, *target);
    }
}
```

Principiul, valabil de fiecare data cand o operatie poate sterge un obiect
pe care bucla il mai foloseste ulterior: **nu tine peste mai multe iteratii
un pointer/referinta catre un obiect care poate fi sters de o iteratie
anterioara; re-cauta-l prin identificatorul lui stabil (nume, id) de
fiecare data, si trateaza "nu mai exista" ca un caz normal, nu ca o
eroare.**

---

### C14. Argumente de linie de comanda invalide la pornirea serverului

**Scenariu.** Extensie a ajustarii 9 din auditul de baza, dar la pornire,
nu in timpul rularii. `./ircserv abc parola` (port ne-numeric),
`./ircserv` (fara argumente), sau `./ircserv 99999 parola` (port in afara
plajei valide 1-65535). Daca `main()` foloseste `atoi(argv[1])` fara
verificare, un port invalid devine tacit `0`, iar `bind()` fie esueaza cu
un mesaj neclar, fie (mai rau, pe unele sisteme) porneste pe un port
alocat automat de sistem, complet neasteptat.

**Solutie: validare explicita in `main()`, cu mesaje clare, inainte sa se
incerce vreo operatie de retea.**

```cpp
int main(int argc, char** argv)
{
    if (argc != 3)
    {
        std::cerr << "Utilizare: " << argv[0] << " <port> <parola>\n";
        return 1;
    }

    long port = 0;
    {
        char* end = 0;
        errno = 0;
        port = std::strtol(argv[1], &end, 10);
        if (*argv[1] == '\0' || *end != '\0' || errno == ERANGE
            || port < 1 || port > 65535)
        {
            std::cerr << "Port invalid: " << argv[1] << "\n";
            return 1;
        }
    }

    const std::string password = argv[2];
    if (password.empty())
    {
        std::cerr << "Parola nu poate fi goala\n";
        return 1;
    }

    try
    {
        Server server(static_cast<int>(port), password);
        server.run();
    }
    catch (const std::exception& e)
    {
        std::cerr << "Eroare fatala la pornire: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
```

De ce validarea la pornire e la fel de importanta ca cea din timpul
rularii: un server "de aparare" trebuie sa esueze **devreme si clar**
(`return 1` cu mesaj explicit) in loc sa porneasca intr-o stare ambigua pe
care abia un client o descopera mai tarziu, intr-un mod greu de
diagnosticat.

---

### C15. Semnal de sistem primit exact in timpul unei modificari de container

**Scenariu.** Auditul de baza rezolva deja partea principala (handler-ul de
semnal doar seteaza un `volatile sig_atomic_t`). Cazul limita ramas: chiar
daca handler-ul e minimal, **bucla principala** trebuie sa verifice flag-ul
`g_stop` doar **intre** iteratiile complete de `poll()`, niciodata la
mijlocul modificarii unui container. Daca verificarea ar fi presarata prin
mijlocul functiilor de cleanup, un `Ctrl+C` primit exact atunci ar putea
opri executia cu un container pe jumatate modificat (de exemplu, un user
scos din `_users` dar inca prezent intr-un canal).

**Solutie: un singur punct de verificare, la nivelul cel mai exterior al
buclei, niciodata in interiorul unei operatii care trebuie sa fie
atomica din punct de vedere logic.**

```cpp
volatile std::sig_atomic_t g_stop = 0;

extern "C" void handleSignal(int)
{
    g_stop = 1;
}

void Server::run()
{
    installSignalHandlers(); // sigaction(SIGINT, ...), sigaction(SIGTERM, ...)

    while (!g_stop) // singurul loc unde se verifica flag-ul
    {
        try
        {
            pollOnce(); // aceasta functie ruleaza integral, nu e intrerupta
                        // "la mijloc" de verificarea flag-ului
        }
        catch (const std::exception& e)
        {
            logSystemError(e.what());
        }
    }

    shutdown(); // cleanup normal, in afara oricarui handler de semnal
}
```

Regula: **`g_stop` se citeste o singura data pe iteratie, la inceputul
buclei `while`, niciodata in interiorul unei functii care presupune ca
poate termina ce a inceput** (cum ar fi `removeClient()` de la C10, care
trebuie sa termine toti cei 4 pasi ai ei inainte ca serverul sa se poata
opri in siguranta).

---

## 4. Implementare consolidata

Fisierele de mai jos aduna solutiile de mai sus intr-o forma directa de
folosit, compatibila C++98. Ca si auditul de baza, acestea sunt fragmente
de referinta, gandite sa fie integrate in structura reala a proiectului
(clasele `Connection`, `Server`, `Channel`, `UserRegistry` presupun deja
existenta framing-ului si a `OutputQueue` din auditul de baza).

### 4.1 `Fd.hpp` — proprietar unic al unui descriptor (reluat, pentru completitudine)

```cpp
#ifndef FD_HPP
#define FD_HPP

#include <unistd.h>

class Fd
{
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

    bool valid() const { return _fd >= 0; }

private:
    int _fd;

    Fd(const Fd&);
    Fd& operator=(const Fd&);
};

#endif
```

### 4.2 `SafeAccess.hpp` — helpere folosite in toate handlerele (C4, C5, C12)

```cpp
#ifndef SAFE_ACCESS_HPP
#define SAFE_ACCESS_HPP

#include <string>
#include <vector>
#include <cstdlib>
#include <cerrno>

// C4: acces la parametri de comanda fara operator[] direct.
inline bool paramAt(const std::vector<std::string>& params,
                     std::size_t index, std::string& out)
{
    if (index >= params.size())
        return false;
    out = params[index];
    return true;
}

// C5: substr fara risc de std::out_of_range.
inline std::string safeSubstr(const std::string& s,
                               std::string::size_type pos,
                               std::string::size_type len = std::string::npos)
{
    if (pos > s.size())
        return std::string();
    return s.substr(pos, len);
}

// C12: parsare numerica stricta, reutilizabila pentru port, limite de
// canal, sau orice alt parametru numeric venit din exterior.
inline bool parseStrictLong(const std::string& text, long minValue,
                             long maxValue, long& out)
{
    if (text.empty())
        return false;

    char* end = 0;
    errno = 0;
    long value = std::strtol(text.c_str(), &end, 10);

    if (*end != '\0')
        return false;
    if (errno == ERANGE)
        return false;
    if (value < minValue || value > maxValue)
        return false;

    out = value;
    return true;
}

#endif
```

### 4.3 `Server.cpp` (fragmente) — accept robust, bucla principala, cleanup amanat

```cpp
static const std::size_t MAX_CLIENTS = 1024;

void Server::acceptPendingConnections() // C7
{
    for (;;)
    {
        if (_users.size() >= MAX_CLIENTS)
            break;

        int clientFd = ::accept(_listenFd, 0, 0);
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
}

void Server::readClient(int fd) // C6, C11
{
    Connection& conn = connectionFor(fd);
    char buffer[4096];
    ssize_t count = ::recv(fd, buffer, sizeof(buffer), 0);

    if (count == 0)
    {
        markForRemoval(fd);
        return;
    }
    if (count < 0)
    {
        if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
            return;
        markForRemoval(fd);
        return;
    }

    try
    {
        conn.appendInput(buffer, static_cast<std::size_t>(count));
    }
    catch (const ProtocolError&)
    {
        conn.queue(makeNumericError(417, "Input too long"));
        markForRemoval(fd);
        return;
    }

    std::string line;
    while (conn.popLine(line))
    {
        if (isMarkedForRemoval(fd))
            break;
        dispatch(parseLine(line), conn);
    }
}

void Server::reapDisconnectedUsers() // C2
{
    std::vector<int> toRemove(_pendingRemoval.begin(), _pendingRemoval.end());
    _pendingRemoval.clear();

    for (std::vector<int>::const_iterator id = toRemove.begin();
         id != toRemove.end(); ++id)
    {
        removeClient(*id);
    }
}

void Server::removeClient(int fd) // C10
{
    std::map<int, User*>::iterator it = _users.find(fd);
    if (it == _users.end())
        return;

    User* user = it->second;

    std::vector<Channel*> channels = user->channels();
    for (std::size_t i = 0; i < channels.size(); ++i)
        removeMember(*channels[i], *user);

    _userRegistry.forget(*user);
    _users.erase(it);
    delete user; // inchide si socketul, prin Fd detinut de Connection/User
}

void Server::run() // C5 (strat 2), C15
{
    installSignalHandlers();

    while (!g_stop)
    {
        try
        {
            pollOnce();               // accept + recv + dispatch pentru fd-urile gata
            reapDisconnectedUsers();  // cleanup amanat, o singura data pe iteratie
        }
        catch (const std::exception& e)
        {
            logSystemError(e.what());
        }
    }

    shutdown();
}
```

### 4.4 `Channel.cpp` (fragmente) — broadcast sigur, kick in masa sigur

```cpp
void Channel::broadcast(const std::string& msg, const User* except) // C3, C8
{
    std::vector<User*> snapshot = _members;

    for (std::size_t i = 0; i < snapshot.size(); ++i)
    {
        User* member = snapshot[i];
        if (member == except || !_server.isStillMember(member))
            continue;

        try
        {
            member->connection().queue(msg);
        }
        catch (const SlowClient&)
        {
            _server.markForRemoval(member->connection().fd());
        }
    }
}

void Server::handleKick(const std::string& channelName, // C13
                        const std::vector<std::string>& nicks)
{
    for (std::size_t i = 0; i < nicks.size(); ++i)
    {
        Channel* channel = findChannel(channelName);
        if (!channel)
            break;

        User* target = findUser(nicks[i]);
        if (!target || !channel->hasMember(*target))
            continue;

        removeMember(*channel, *target);
    }
}
```

### 4.5 `main.cpp` — validare stricta la pornire (C14)

```cpp
#include <iostream>
#include <cerrno>
#include <cstdlib>
#include "Server.hpp"

int main(int argc, char** argv)
{
    if (argc != 3)
    {
        std::cerr << "Utilizare: " << argv[0] << " <port> <parola>\n";
        return 1;
    }

    char* end = 0;
    errno = 0;
    long port = std::strtol(argv[1], &end, 10);
    if (*argv[1] == '\0' || *end != '\0' || errno == ERANGE
        || port < 1 || port > 65535)
    {
        std::cerr << "Port invalid: " << argv[1] << "\n";
        return 1;
    }

    std::string password(argv[2]);
    if (password.empty())
    {
        std::cerr << "Parola nu poate fi goala\n";
        return 1;
    }

    try
    {
        Server server(static_cast<int>(port), password);
        server.run();
    }
    catch (const std::exception& e)
    {
        std::cerr << "Eroare fatala la pornire: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
```

---

## 5. Checklist de teste suplimentare, specifice cazurilor de mai sus

Se adauga la lista din sectiunea 14 a auditului de baza:

```text
[ ] Se trimit simultan QUIT pentru 2 clienti in aceeasi iteratie de poll();
    serverul ramane in picioare, ambii sunt eliminati corect (C2).
[ ] Un canal cu exact 1 membru primeste PART de la acel membru chiar in
    timp ce alt cod ii face broadcast; nu apare use-after-free (C3).
[ ] JOIN, PART, KICK, MODE trimise fara parametri nu crapa serverul; se
    primeste ERR_NEEDMOREPARAMS (C4).
[ ] O linie IRC care ar provoca std::out_of_range la parsare este respinsa
    controlat, nu scapa pana la terminate() (C5).
[ ] Un singur client trimite octeti la infinit fara \r\n; doar acel client
    este deconectat, restul serverului functioneaza normal (C6).
[ ] Serverul primeste conexiuni peste MAX_CLIENTS; conexiunile in plus
    raman in coada TCP, fara busy-loop pe accept() (C7).
[ ] Broadcast catre un canal cu un membru deja deconectat nu opreste
    livrarea catre ceilalti membri (C8).
[ ] KICK cu o lista de nick-uri care goleste complet canalul nu foloseste
    un pointer catre canal dupa ce acesta a fost sters (C13).
[ ] QUIT urmat, in acelasi pachet TCP, de o alta comanda a aceluiasi
    client nu proceseaza acea comanda si nu crapa (C11).
[ ] "MODE #canal +l abc" si "MODE #canal +l -5" sunt respinse cu eroare
    IRC explicita, nu acceptate ca 0 sau ca numar negativ (C12).
[ ] Pornirea serverului cu port non-numeric, port 0, port 70000 sau fara
    parola se opreste cu mesaj clar, fara sa deschida vreun socket (C14).
```

---

## 6. Concluzie

Auditul de baza a stabilit directia arhitecturala corecta (framing,
ownership, partial write, semnale). Cazurile din acest document sunt, in
mare parte, **consecinte directe** ale acelei arhitecturi puse in miscare
reala: momentul in care doua evenimente se suprapun (doi clienti care pleaca
deodata, un `KICK` care goleste canalul la mijlocul procesarii, o comanda
`QUIT` urmata de alta comanda in acelasi pachet TCP). Firul comun al
tuturor solutiilor de mai sus e simplu si merita retinut ca principiu, nu
doar ca lista de cod:

1. **Nu modifica o colectie in timp ce o parcurgi** — marcheaza, apoi
   aplica dupa (C2, C11, C13).
2. **Un obiect care traieste in mai multe locuri are un singur punct din
   care e sters, in ordinea corecta** (C10).
3. **Nu presupune ca un pointer/referinta ramane valid peste mai multe
   pasi** — re-verifica sau re-cauta prin identificator stabil (C3, C13).
4. **Orice input extern (client, argumente de pornire) e validat explicit,
   niciodata presupus corect** (C4, C12, C14).
5. **O eroare provocata de un participant nu are voie sa doboare restul
   sistemului** — nici alt client (C6, C8), nici tot procesul (C5, C15).
