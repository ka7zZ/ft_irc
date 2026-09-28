# API-ul de socket POSIX în C++98: rol, argumente, forme de utilizare și când se folosește fiecare

> **Ghid de referință pentru serverul IRC** (continuare a ghidului de
> robustețe și design). Explică, apel cu apel, tot ce folosim din
> biblioteca de socket-uri: `socket`, `setsockopt`, `fcntl`, `sockaddr_in`,
> `bind`, `listen`, `accept`, `connect`, `recv`, `send`, `poll`, `shutdown`,
> `close`, `getsockname`/`getpeername`, `getaddrinfo` și funcțiile ajutătoare
> (`htons`, `inet_pton`, …).
>
> **Pentru cine este:** dezvoltatori juniori. Fiecare apel are aceeași
> structură: **Rol → Argumente → Valoare întoarsă și erori → Forme de
> utilizare (minimă / robustă / variante) → Când și când nu → Capcane.**
>
> **Verificat:** programul complet `socket_api_demo.cpp` (livrat împreună cu
> acest ghid) folosește fiecare apel în forma robustă, compilează cu
> `-std=c++98 -Wall -Wextra -Werror` și a trecut testele de fragmentare,
> comenzi lipite, linie prea lungă, octeți NUL, client cu RST, depășirea
> limitei de clienți, client lent (backpressure) și `SIGINT` (exit 0).
> Afirmațiile despre comportamentul kernelului marcate „(Linux)” au fost
> verificate pe Linux 6.x; pentru macOS/BSD sunt semnalate diferențele
> cunoscute, dar **verificați-le pe mașina voastră**.

---

## Cuprins

1. Harta apelurilor: cine cheamă ce și în ce ordine
2. Headere, tipuri și convenții
3. Ordinea octeților și conversia adreselor
4. Structurile de adresă (`sockaddr` și familia ei)
5. `socket()`
6. `setsockopt()` / `getsockopt()`
7. `fcntl()`: modul non-blocking
8. `bind()`
9. `listen()`
10. `accept()`
11. `connect()` (clienți și teste)
12. `recv()`
13. `send()`
14. `poll()`
15. `shutdown()` și `close()`
16. `getsockname()` / `getpeername()`
17. `getaddrinfo()` și familia lui
18. Semnale legate de socket-uri
19. Matricea de erori: ce faci la fiecare `errno`
20. Tabele de decizie: ce variantă alegi și când
21. Capcane frecvente
22. Exerciții pentru juniori (cu `strace`)
23. Anexă: programul `socket_api_demo.cpp` explicat

---

## 1. Harta apelurilor

```text
SERVER (TCP)                                   CLIENT (TCP)
─────────────                                  ─────────────
socket()      ← creează un capăt (fd)          socket()
setsockopt()  ← SO_REUSEADDR (înainte de bind)
bind()        ← "ocup adresa:portul"
listen()      ← "acest fd primește conexiuni"
fcntl()       ← O_NONBLOCK
   │
   ▼  ┌───────────────── bucla principală ─────────────────┐
poll()│ „pe ce fd pot face acum o operație fără să blochez?"│
   │  └──────────────────────────────────────────────────────┘
   ├─ listener POLLIN → accept() (în buclă, până la EAGAIN)
   │                        └─ fcntl(O_NONBLOCK) pe fd-ul nou
   ├─ client POLLIN   → recv()  ◄──────────────────────────  send()
   ├─ client POLLOUT  → send()  ──────────────────────────►  recv()
   └─ EOF / eroare    → shutdown()? → close()                 close()
                                                             connect() ← doar clientul
```

Trei idei de reținut înainte de detalii:

1. **Socket-ul de listening și socket-urile de client sunt fd-uri diferite.**
   `accept()` produce un fd **nou** pentru fiecare client; cel de listening
   nu transportă date.
2. **`poll()` nu citește și nu scrie.** Doar spune că *merită încercat*.
   Operația efectivă (`accept`/`recv`/`send`) trebuie totuși tratată la
   fel de defensiv (poate eșua cu `EAGAIN`, `EINTR` etc.).
3. **Fiecare apel poate eșua.** Convenția POSIX: valoarea `-1` + `errno`.
   Un cod care nu verifică rezultatul este un cod care crapă „doar uneori”.

---

## 2. Headere, tipuri și convenții

| Header | Ce aduce |
|---|---|
| `<sys/types.h>` | `ssize_t`, `size_t` (istoric necesar pe unele sisteme înaintea `<sys/socket.h>`) |
| `<sys/socket.h>` | `socket`, `bind`, `listen`, `accept`, `connect`, `send`, `recv`, `shutdown`, `setsockopt`, `getsockopt`, `getsockname`, `getpeername`, `struct sockaddr`, `sockaddr_storage`, `socklen_t`, constantele `AF_*`, `SOCK_*`, `SOL_SOCKET`, `SO_*`, `MSG_*`, `SHUT_*`, `SOMAXCONN` |
| `<netinet/in.h>` | `sockaddr_in`, `sockaddr_in6`, `in_addr`, `INADDR_ANY`, `INADDR_LOOPBACK`, `htons/htonl/ntohs/ntohl`, `IPPROTO_*` |
| `<netinet/tcp.h>` | `TCP_NODELAY`, `TCP_KEEPIDLE`, … (opțiuni de nivel TCP) |
| `<arpa/inet.h>` | `inet_pton`, `inet_ntop`, `INET_ADDRSTRLEN`, `INET6_ADDRSTRLEN` |
| `<netdb.h>` | `getaddrinfo`, `freeaddrinfo`, `gai_strerror`, `getnameinfo`, `struct addrinfo` |
| `<poll.h>` | `poll`, `struct pollfd`, `nfds_t`, `POLLIN/POLLOUT/POLLERR/POLLHUP/POLLNVAL` |
| `<fcntl.h>` | `fcntl`, `F_GETFL/F_SETFL`, `O_NONBLOCK`, `F_GETFD/F_SETFD`, `FD_CLOEXEC` |
| `<unistd.h>` | `close`, `read`, `write` |
| `<cerrno>` | `errno` și constantele `EINTR`, `EAGAIN`, … |
| `<csignal>` | `sigaction`, `SIGPIPE`, `SIGINT`, `sig_atomic_t` |

**Tipuri care se confundă des**

| Tip | Ce este | Regulă |
|---|---|---|
| `socklen_t` | întreg fără semn pentru **lungimi de adrese/opțiuni** | *nu* `int`, *nu* `size_t` |
| `ssize_t` | întreg **cu semn**: număr de octeți sau `-1` | rezultatul lui `recv`/`send`; nu-l pune într-un `size_t` înainte să verifici `< 0` |
| `nfds_t` | număr de intrări în `poll` (întreg fără semn) | cast explicit de la `vector::size()` |
| `in_port_t` | port (16 biți), în **ordine de rețea** în structuri | se scrie cu `htons` |

**Convenția „value-result”** (apare la `accept`, `getsockname`, `getpeername`,
`getsockopt`): argumentul de lungime este **și intrare, și ieșire**. Îl
inițializezi cu dimensiunea bufferului tău; kernelul îl suprascrie cu
dimensiunea reală. Dacă îl lași neinițializat, kernelul poate citi gunoi ca
„dimensiune disponibilă”.

```cpp
struct sockaddr_in peer;
socklen_t len = sizeof(peer);     // OBLIGATORIU înainte de apel
::accept(fd, reinterpret_cast<struct sockaddr*>(&peer), &len);
// după apel: len = dimensiunea adresei scrise (poate fi > sizeof(peer): trunchiere)
```

---

## 3. Ordinea octeților și conversia adreselor

Procesorul tău (x86, ARM) stochează numerele *little-endian*; rețeaua
folosește *big-endian* („network byte order”). Tot ce intră în `sockaddr_*`
(port, adresă) trebuie convertit.

| Funcție | Sens | Folosire |
|---|---|---|
| `htons(x)` | host → network, 16 biți | **portul** înainte de `bind`/`connect` |
| `htonl(x)` | host → network, 32 biți | `INADDR_ANY`, `INADDR_LOOPBACK` |
| `ntohs(x)` | network → host, 16 biți | afișarea portului unui peer / după `getsockname` |
| `ntohl(x)` | network → host, 32 biți | rar (adresa ca număr) |

**Capcană clasică:** `addr.sin_port = 6667;` fără `htons` „merge” pe
un procesor big-endian și **ascultă pe alt port** pe x86 (6667 = `0x1A0B` →
citit ca `0x0B1A` = 2842). Nicio eroare, doar un port greșit.

### 3.1 Text ↔ adresă binară: `inet_pton` / `inet_ntop`

```cpp
int inet_pton(int af, const char* src, void* dst);
const char* inet_ntop(int af, const void* src, char* dst, socklen_t size);
```

| Argument | Semnificație |
|---|---|
| `af` | `AF_INET` (IPv4) sau `AF_INET6` |
| `src`/`dst` | text ↔ structură binară (`in_addr` / `in6_addr`) |
| `size` | dimensiunea bufferului: `INET_ADDRSTRLEN` (16) / `INET6_ADDRSTRLEN` (46) |

`inet_pton` întoarce **`1`** succes, **`0`** text invalid, **`-1`** familie
nesuportată (cu `errno`). Verifică explicit `== 1`; „`> 0`” ascunde nimic dar
„`!= -1`” acceptă textul invalid.

```cpp
// Minimă
struct sockaddr_in a;  std::memset(&a, 0, sizeof(a));
inet_pton(AF_INET, "127.0.0.1", &a.sin_addr);

// Robustă
if (::inet_pton(AF_INET, ip, &a.sin_addr) != 1)
    throw std::runtime_error(std::string("adresa IP invalida: ") + ip);

// Afișarea unui peer (log, prefix IRC nick!user@host)
char buf[INET_ADDRSTRLEN];
if (::inet_ntop(AF_INET, &peer.sin_addr, buf, sizeof(buf)) == 0) std::strcpy(buf, "?");
```

**De evitat:** `inet_addr()` (întoarce `-1` = `255.255.255.255` *și* eroare,
ambiguu, doar IPv4) și `inet_ntoa()` (buffer static, nu e thread-safe).

---

## 4. Structurile de adresă

API-ul are o singură semnătură pentru toate familiile de adrese
(`const struct sockaddr*`), deci lucrezi cu structura **concretă** și o
„castezi” la cea generică.

```text
struct sockaddr          ← „interfața”: doar familia + 14 octeți opaci
   ├─ struct sockaddr_in     IPv4  (16 octeți)
   ├─ struct sockaddr_in6    IPv6  (28 octeți)
   ├─ struct sockaddr_un     socket Unix (cale de fișier)
   └─ struct sockaddr_storage  ← destul de mare pentru ORICARE (128 octeți)
```

### 4.1 `struct sockaddr_in` (IPv4), câmp cu câmp

```cpp
struct sockaddr_in {
    sa_family_t    sin_family;   // AF_INET (mereu)
    in_port_t      sin_port;     // port, ORDINE DE RETEA -> htons()
    struct in_addr sin_addr;     // adresa IPv4; .s_addr, ORDINE DE RETEA
    unsigned char  sin_zero[8];  // umplutură; trebuie zero
};
```

| Câmp | Valoare uzuală | Note |
|---|---|---|
| `sin_family` | `AF_INET` | trebuie să coincidă cu `domain` din `socket()` |
| `sin_port` | `htons(6667)` | `0` la `bind` = „alege tu un port liber” (află-l cu `getsockname`) |
| `sin_addr.s_addr` | `htonl(INADDR_ANY)` / `htonl(INADDR_LOOPBACK)` / rezultat `inet_pton` | vezi tabelul de mai jos |
| `sin_zero` | zerouri | de aceea `memset` pe toată structura |
| *(BSD/macOS)* `sin_len` | `sizeof(sockaddr_in)` | există doar pe BSD; `memset` îl lasă 0, iar kernelul BSD îl tolerează la `bind`/`connect` |

| Valoare pentru `sin_addr` | Înseamnă | Când |
|---|---|---|
| `INADDR_ANY` (`0.0.0.0`) | „ascult pe **toate** interfețele” | server accesibil din rețea |
| `INADDR_LOOPBACK` (`127.0.0.1`) | doar local | dezvoltare/teste; **implicit mai sigur** |
| o adresă anume | doar acea interfață | server pe o singură placă |

`INADDR_ANY` este `0`, deci `htonl` nu schimbă nimic, dar **scrie-l oricum**:
`INADDR_LOOPBACK` (`0x7F000001`) *are* nevoie de `htonl`, iar consecvența
previne greșeala.

```cpp
// Minimă (didactică)
struct sockaddr_in addr;
addr.sin_family = AF_INET;
addr.sin_port = htons(6667);
addr.sin_addr.s_addr = htonl(INADDR_ANY);
// GREȘIT: sin_zero necunoscut (gunoi din stivă)

// Robustă
struct sockaddr_in addr;
std::memset(&addr, 0, sizeof(addr));               // zeroizează tot, inclusiv sin_zero
addr.sin_family      = AF_INET;
addr.sin_port        = htons(static_cast<unsigned short>(port));
addr.sin_addr.s_addr = htonl(INADDR_ANY);          // sau inet_pton(...)
```

### 4.2 `struct sockaddr_in6` și `sockaddr_storage`

```cpp
struct sockaddr_in6 {
    sa_family_t     sin6_family;   // AF_INET6
    in_port_t       sin6_port;     // htons()
    uint32_t        sin6_flowinfo; // 0
    struct in6_addr sin6_addr;     // in6addr_any / in6addr_loopback
    uint32_t        sin6_scope_id; // pentru adrese link-local (fe80::), altfel 0
};
```

`sockaddr_storage` este folosit când **nu știi dinainte familia** peer-ului
(ex. `accept` pe un socket dual-stack): citești `ss_family` și apoi
reinterpretezi ca `sockaddr_in` sau `sockaddr_in6`.

```cpp
struct sockaddr_storage ss;
socklen_t len = sizeof(ss);
int c = ::accept(l, reinterpret_cast<struct sockaddr*>(&ss), &len);
if (ss.ss_family == AF_INET)
    /* reinterpret_cast<sockaddr_in*>(&ss) */;
else if (ss.ss_family == AF_INET6)
    /* reinterpret_cast<sockaddr_in6*>(&ss) */;
```

Pentru un proiect IRC școlar, **IPv4 (`sockaddr_in`) este suficient**; treceți
la `getaddrinfo` (§17) dacă trebuie să suportați IPv6.

### 4.3 De ce `reinterpret_cast`?

C++ nu are conversie implicită `sockaddr_in*` → `sockaddr*`, iar C-style
cast ascunde intenția. Folosiți `reinterpret_cast<struct sockaddr*>(&addr)`
**doar la granița apelului de sistem**, nu în restul codului.

---

## 5. `socket()`

**Rol.** Cere kernelului un „capăt de comunicație” și întoarce un **file
descriptor**. Încă nu e legat de nicio adresă și nu face nimic în rețea.

```cpp
int socket(int domain, int type, int protocol);
```

| Argument | Valori | Semnificație |
|---|---|---|
| `domain` | `AF_INET` | IPv4 (proiectul IRC) |
| | `AF_INET6` | IPv6 (poate primi și IPv4 dacă `IPV6_V6ONLY=0`) |
| | `AF_UNIX` | comunicare locală prin fișier (nu pentru IRC) |
| | *(`AF_UNSPEC` **nu** e valid aici; doar la `getaddrinfo`)* | |
| `type` | `SOCK_STREAM` | flux de octeți fiabil, ordonat = **TCP** |
| | `SOCK_DGRAM` | datagrame = UDP (mesaje separate, nefiabile) |
| | `SOCK_RAW` | pachete brute (necesită privilegii) |
| | `\| SOCK_NONBLOCK`, `\| SOCK_CLOEXEC` | (Linux/BSD modern) setează flag-urile **atomic** la creare |
| `protocol` | `0` | protocolul implicit pentru perechea (domain, type): TCP pentru `AF_INET`+`SOCK_STREAM` |
| | `IPPROTO_TCP`, `IPPROTO_UDP` | explicit; necesar doar când există mai multe variante |

**Returnează:** fd `>= 0`, sau `-1` cu `errno`:

| `errno` | Cauză | Acțiune |
|---|---|---|
| `EMFILE` | procesul a atins limita de fd (`ulimit -n`) | fatal la pornire; la rulare, vezi §10 |
| `ENFILE` | limita la nivel de sistem | idem |
| `EACCES` | permisiuni (mai ales `SOCK_RAW`) | fatal |
| `EAFNOSUPPORT`, `EPROTONOSUPPORT`, `EPROTOTYPE` | combinație domain/type/protocol invalidă | bug de cod |
| `ENOBUFS`, `ENOMEM` | memorie kernel insuficientă | fatal / reîncearcă |

### Forme de utilizare

```cpp
// 1. Minimă (didactică): scurgere de fd dacă bind() eșuează mai jos
int fd = ::socket(AF_INET, SOCK_STREAM, 0);
if (fd < 0) { perror("socket"); return 1; }

// 2. Robustă: RAII, nimic nu se scurge pe nicio cale de eroare
int raw = ::socket(AF_INET, SOCK_STREAM, 0);
if (raw < 0)
    throw std::runtime_error(std::string("socket: ") + std::strerror(errno));
Fd guard(raw);                       // Fd::~Fd() face close(); vezi ghidul de robustețe, L1
/* ... setsockopt / bind / listen pot arunca: guard închide fd-ul ... */
return guard.release();              // transfer explicit al ownership-ului

// 3. Atomic (Linux): elimină fereastra dintre socket() și fcntl()
int fd = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
```

**Când ce.** Varianta 2 *întotdeauna* într-un proiect real. Varianta 3 doar
dacă ținta e strict Linux; pentru portabilitate (macOS) folosește `socket()`
simplu + `fcntl()` (§7).

**Capcane.** (1) Uitatul `close()` pe o cale de eroare. (2) `type` corect dar
`domain` diferit de `sin_family` din adresa dată la `bind` → `EINVAL`/
`EAFNOSUPPORT`. (3) `SOCK_STREAM` nu păstrează granițele mesajelor: vezi
framing în ghidul de robustețe (`T1`).

---

## 6. `setsockopt()` / `getsockopt()`

**Rol.** Reglează (sau citește) comportamentul unui socket, la diverse
niveluri (socket, TCP, IP).

```cpp
int setsockopt(int fd, int level, int optname, const void* optval, socklen_t optlen);
int getsockopt(int fd, int level, int optname, void* optval, socklen_t* optlen); // value-result
```

| Argument | Semnificație |
|---|---|
| `fd` | socket-ul |
| `level` | **cine interpretează opțiunea**: `SOL_SOCKET` (generic), `IPPROTO_TCP`, `IPPROTO_IPV6`, … |
| `optname` | opțiunea (tabel mai jos) |
| `optval` | **pointer** la valoare (aproape mereu `int`; `struct linger`, `struct timeval` la unele) |
| `optlen` | dimensiunea valorii: `sizeof(int)` (la `getsockopt`: pointer, value-result) |

Întoarce `0` / `-1`. Erori: `ENOPROTOOPT` (opțiune necunoscută la acel
`level`), `EINVAL`, `EBADF`, `ENOTSOCK`.

### 6.1 Opțiunile care contează în acest proiect

| Opțiune (`level`) | Rol | Recomandare |
|---|---|---|
| `SO_REUSEADDR` (`SOL_SOCKET`) | Permite `bind()` pe un port aflat în `TIME_WAIT` (conexiuni închise recent). Fără ea, după o repornire rapidă: `bind: Address already in use`. | **Da, la listener, înainte de `bind()`.** |
| `SO_REUSEPORT` | Mai multe socket-uri pot asculta pe același port (load-balancing). | **Nu** în acest proiect: o a doua instanță ar porni „cu succes”, ascunzând greșeala. |
| `TCP_NODELAY` (`IPPROTO_TCP`) | Dezactivează algoritmul Nagle (nu mai așteaptă să grupeze pachete mici). | Opțional; util pentru latență la mesaje mici. Nu e necesar pentru corectitudine. |
| `SO_KEEPALIVE` | Sondează conexiunile inactive; implicit prima sondă după ~2 h (Linux: `tcp_keepalive_time=7200`). | În IRC se folosește **`PING`/`PONG` la nivel de aplicație** (răspunde în secunde, nu ore). |
| `SO_SNDBUF` / `SO_RCVBUF` | Dimensiunea bufferelor kernel. (Linux) valoarea e dublată și există un minim: la cerere 1024 am obținut **4608**. | Implicit e bine. **Truc de test:** micșorat pentru a *forța* `send()` parțial / `EAGAIN` (vezi anexa). |
| `SO_LINGER` (`struct linger`) | Comportamentul lui `close()` cu date netrimise. `{1, 0}` = închidere abruptă cu **RST**. | Doar pentru teste (simulezi `ECONNRESET`). |
| `SO_ERROR` (`getsockopt`) | Citește și șterge eroarea în așteptare a socket-ului. | După un `connect()` non-blocking (§11). |
| `SO_NOSIGPIPE` (macOS/BSD) | Echivalentul per-socket al lui `MSG_NOSIGNAL`. | Doar pe BSD, ca alternativă (§18). |
| `IPV6_V6ONLY` (`IPPROTO_IPV6`) | `0` = un socket IPv6 acceptă și IPv4 (dual-stack). | Doar cu IPv6. |

### 6.2 Forme de utilizare

```cpp
// Minimă
int one = 1;
setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));   // rezultat ignorat

// Robustă
int one = 1;
if (::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one)) < 0)
    throw std::runtime_error(std::string("setsockopt: ") + std::strerror(errno));

// Citire: câți octeți are efectiv bufferul de trimitere?
int sz = 0; socklen_t len = sizeof(sz);
if (::getsockopt(fd, SOL_SOCKET, SO_SNDBUF, &sz, &len) == 0) { /* sz valid */ }

// Sondarea erorii după connect() non-blocking
int err = 0; socklen_t elen = sizeof(err);
::getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &elen);   // err == 0 => connect reușit
```

**Capcane.** (1) `SO_REUSEADDR` se setează **înainte de `bind()`**; după, e
inutilă. (2) `optval` este un *pointer*: pasezi `&one`, nu `1`. (3) `optlen`
greșit (ex. `sizeof(long)`) → `EINVAL` sau valoare citită trunchiat.
(4) Opțiunile setate pe **listener** nu se propagă garantat la fd-urile
acceptate (unele — precum `SO_SNDBUF` — da pe Linux, altele nu): pentru
siguranță le setezi pe fd-ul rezultat din `accept()`.

---

## 7. `fcntl()`: modul non-blocking

**Rol.** Manipulează proprietățile unui fd deja creat. Aici îl folosim ca să
punem socket-ul în **mod non-blocking**: `accept/recv/send` nu mai
adorm procesul când nu pot progresa, ci întorc imediat `-1` cu `EAGAIN`.

```cpp
int fcntl(int fd, int cmd, ... /* arg */);
```

| `cmd` | Rol |
|---|---|
| `F_GETFL` / `F_SETFL` | citește / scrie **flag-urile de stare** ale fișierului (`O_NONBLOCK`) |
| `F_GETFD` / `F_SETFD` | citește / scrie **flag-urile fd-ului** (`FD_CLOEXEC`) |

**De ce non-blocking?** Un server cu un singur fir, cu `recv()` blocant pe
un client tăcut, îngheață toți ceilalți clienți. Non-blocking + `poll()` =
un fir servește sute de conexiuni.

```cpp
// Minimă (BUG ascuns: suprascrie orice alt flag existent)
fcntl(fd, F_SETFL, O_NONBLOCK);

// Robustă: read-modify-write
static bool setNonBlocking(int fd)
{
    int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags < 0)
        return false;
    return ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

// Alternative: SOCK_NONBLOCK la socket()/accept4() (Linux), ioctl(fd, FIONBIO, &one)
// Utilitar, dacă serverul face vreodată fork()+exec():
::fcntl(fd, F_SETFD, FD_CLOEXEC);       // fd-ul nu se moștenește în procesul exec-uit
```

> **Verificat pe Linux:** un fd întors de `accept()` pe un listener non-blocking
> **nu** moștenește `O_NONBLOCK` (rezultat: `0`). Pe BSD/macOS moștenește. Concluzie
> portabilă: **setați explicit `O_NONBLOCK` pe fiecare fd rezultat din
> `accept()`** (în demo: `setNonBlocking(cfd)`).

> **Regulament:** unele regulamente școlare restrâng formele permise de
> `fcntl` (ex. doar `F_SETFL, O_NONBLOCK`). Verificați cerințele proiectului
> înainte să folosiți `F_GETFL` sau `FD_CLOEXEC`.

**Capcane.** (1) Setat doar pe listener, nu și pe clienți → un `recv()`
blochează serverul. (2) `F_SETFL` fără `F_GETFL` (suprascrie flag-uri).
(3) Non-blocking **nu** înlocuiește `poll()`; fără `poll()` ajungi la
busy-loop de `EAGAIN` (CPU 100%).

---

## 8. `bind()`

**Rol.** Leagă socket-ul de o **adresă locală** (IP + port), adică „vreau
să primesc trafic destinat acestei adrese:port”.

```cpp
int bind(int fd, const struct sockaddr* addr, socklen_t addrlen);
```

| Argument | Semnificație |
|---|---|
| `fd` | socket-ul creat cu `socket()` |
| `addr` | adresa (de obicei `sockaddr_in*`, castat) |
| `addrlen` | `sizeof(sockaddr_in)`: dimensiunea structurii **concrete**, nu a lui `sockaddr` |

Întoarce `0` / `-1`:

| `errno` | Cauză | Acțiune |
|---|---|---|
| `EADDRINUSE` | portul e ocupat (altă instanță; sau `TIME_WAIT` fără `SO_REUSEADDR`) | mesaj clar + `return 1`; nu reîncerca orbește |
| `EACCES` | port `< 1024` fără privilegii | alege port `>= 1024` |
| `EADDRNOTAVAIL` | IP-ul cerut nu aparține mașinii | verifică adresa |
| `EINVAL` | socket deja legat / `addrlen` greșit | bug |
| `EAFNOSUPPORT` | `sin_family` nepotrivit | bug |

### Forme de utilizare

```cpp
// Minimă
bind(fd, (struct sockaddr*)&addr, sizeof(addr));            // eroarea se pierde

// Robustă
if (::bind(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0)
    throw std::runtime_error(std::string("bind: ") + std::strerror(errno));

// Port 0: „alege tu un port liber” (teste, porturi efemere); află-l cu getsockname (§16)
addr.sin_port = htons(0);
```

**Când ce.**
- Server IRC: `INADDR_ANY` dacă trebuie accesibil din rețea; **`127.0.0.1` în
  dezvoltare** (nu expui accidental un server nefinisat).
- Clientul **nu** face de obicei `bind()`: kernelul alege portul sursă la `connect()`.

**Capcane.** (1) `sizeof(sockaddr)` în loc de `sizeof(sockaddr_in)` merge „din
întâmplare” pe unele sisteme și pică pe altele. (2) `bind()` **nu** pornește
ascultarea: fără `listen()`, clienții primesc `ECONNREFUSED`. (3) Portul
scris fără `htons`, vezi §3.

---

## 9. `listen()`

**Rol.** Transformă socket-ul într-unul **pasiv**, care primește conexiuni,
și cere kernelului să gestioneze handshake-ul TCP în numele lui.

```cpp
int listen(int fd, int backlog);
```

| Argument | Semnificație |
|---|---|
| `fd` | socket legat cu `bind()`, `SOCK_STREAM` |
| `backlog` | dimensiunea cozii de conexiuni **deja stabilite** (handshake complet) și încă neluate cu `accept()` |

Se apelează **o singură dată**, după `bind()`. Întoarce `0` / `-1`
(`EADDRINUSE`, `EOPNOTSUPP` dacă socket-ul e UDP, `EBADF`).

**Ce este, de fapt, `backlog`?** Kernelul ține două cozi: conexiunile
*în curs* de handshake și conexiunile *gata*, care așteaptă `accept()`.
`backlog` limitează a doua. (Linux) valoarea este plafonată de
`net.core.somaxconn` (pe mașina de test: **4096**; pe kernele mai vechi: 128).
Când coada e plină, kernelul ignoră noile SYN/ACK; clientul reîncearcă și
vede întârziere, nu neapărat eroare imediată.

| Valoare | Când |
|---|---|
| `SOMAXCONN` | implicit rezonabil, „cât permite sistemul” (recomandat) |
| `128` | valoare tradițională, suficientă pentru un proiect școlar |
| `1`–`5` | doar demonstrații/teste care vor să umple coada |

**`backlog` NU este numărul maxim de clienți.** Numărul de clienți conectați
simultan îl limitezi tu (`MAX_CLIENTS`) și fd-urile procesului (`ulimit -n`).

```cpp
// Robustă
if (::listen(fd, SOMAXCONN) < 0)
    throw std::runtime_error(std::string("listen: ") + std::strerror(errno));
```

**Capcane.** (1) Uitat complet (frecvent!) → toți clienții primesc
`Connection refused`. (2) Interpretarea backlog-ului ca limită de clienți.
(3) Dacă `accept()` nu e apelat destul de repede (server blocat), coada se
umple și noii clienți se blochează în connect.

---

## 10. `accept()`

**Rol.** Extrage din coada listener-ului **o conexiune deja stabilită** și
întoarce un **fd nou**, dedicat acelui client. Listenerul rămâne deschis.

```cpp
int accept(int fd, struct sockaddr* addr, socklen_t* addrlen);   // addrlen: value-result
```

| Argument | Semnificație |
|---|---|
| `fd` | socket-ul de **listening** |
| `addr` | buffer în care kernelul scrie adresa clientului; poate fi `NULL` (nu te interesează) |
| `addrlen` | pointer, **inițializat cu `sizeof(*addr)`**; la întoarcere = lungimea reală. `NULL` dacă și `addr` e `NULL` |

**Returnează:** fd nou `>= 0`, sau `-1`:

| `errno` | Cauză | Acțiune |
|---|---|---|
| `EAGAIN`/`EWOULDBLOCK` | coada e goală (listener non-blocking) | **normal**: ieși din bucla de accept |
| `EINTR` | întrerupt de semnal | reîncearcă (sau revino la `poll`, verificând `g_stop`) |
| `ECONNABORTED` | clientul a renunțat între handshake și `accept` | ignoră, continuă |
| `EMFILE` | limita de fd a procesului | vezi „Epuizarea fd” mai jos |
| `ENFILE` | limita de sistem | idem |
| `ENOBUFS`, `ENOMEM` | memorie kernel | log, reîncearcă mai târziu |
| `EPROTO`, `ENETDOWN`, `EHOSTUNREACH`, … (Linux) | erori de rețea aferente conexiunii noi | tratează ca `EAGAIN` (reîncearcă) |
| `EBADF`, `EINVAL`, `ENOTSOCK` | bug (fd invalid / fără `listen`) | fatal |

### Forme de utilizare

```cpp
// 1. Minimă, BLOCANTĂ (un singur client la un moment dat; doar exerciții)
int c = ::accept(listenFd, 0, 0);

// 2. Robustă, NON-BLOCKING, cu buclă până la EAGAIN
for (;;)
{
    if (_clients.size() >= MAX_CLIENTS) return;                   // vezi „limita de clienți”
    struct sockaddr_in peer;
    socklen_t plen = sizeof(peer);                                // value-result: inițializat
    int cfd = ::accept(_listen.get(), reinterpret_cast<struct sockaddr*>(&peer), &plen);
    if (cfd < 0)
    {
        if (errno == EINTR || errno == ECONNABORTED) continue;
        if (errno == EAGAIN || errno == EWOULDBLOCK) return;      // coada golită
        std::perror("accept");                                    // EMFILE etc.
        return;
    }
    Fd guard(cfd);                                                // nimic nu se scurge
    if (!setNonBlocking(cfd)) continue;                           // NU se moștenește pe Linux
    /* new Client(cfd) ...; guard.release() abia după ce Client îl deține */
    char ip[INET_ADDRSTRLEN] = "?";
    ::inet_ntop(AF_INET, &peer.sin_addr, ip, sizeof(ip));         // log / hostname IRC
}

// 3. Linux: accept4 setează flag-urile atomic
int cfd = ::accept4(l, 0, 0, SOCK_NONBLOCK | SOCK_CLOEXEC);
```

**De ce în buclă, până la `EAGAIN`?** `poll()` semnalează `POLLIN` pe listener
**o dată** pentru mai multe conexiuni sosite simultan; un singur `accept()`
ar lăsa restul așteptând până la următorul `poll`. Bucla golește coada.

**Epuizarea fd-urilor (`EMFILE`) și limita de clienți.** `poll()` este
*level-triggered*: dacă nu iei conexiunile din coadă, listenerul rămâne `POLLIN`
și `poll()` revine imediat, la infinit (CPU 100%). Soluții:
(1) `MAX_CLIENTS` **sub** `ulimit -n` (minus stdio, listener, log);
(2) când limita e atinsă, **scoate listenerul din setul `poll`** (așa face
demo-ul: `if (_clients.size() < MAX_CLIENTS)` la construirea vectorului
`pollfd`) și repune-l când pleacă un client;
(3) opțional, tehnica *fd de rezervă*: ține un `open("/dev/null")`; la `EMFILE`
îl închizi, faci `accept()`+`close()` (refuz curat), redeschizi rezerva.

**Capcane.** (1) `plen` neinițializat. (2) `new Client(cfd)` care aruncă
`bad_alloc` **după** accept → `cfd` scurs (de aici `Fd guard`). (3) Uitat
`O_NONBLOCK` pe fd-ul nou. (4) Presupunerea că `peer` e IPv4 pe un listener
dual-stack: verifică `sin_family`/`ss_family`.

---

## 11. `connect()` (clienți și teste)

Serverul IRC **nu** îl folosește; îl veți folosi în clienți de test, boți sau
un viitor server-to-server.

```cpp
int connect(int fd, const struct sockaddr* addr, socklen_t addrlen);
```

Argumentele sunt ca la `bind()`, dar `addr` este adresa **peer-ului**. Kernelul
alege singur portul sursă.

| Formă | Comportament |
|---|---|
| **Blocantă** | Așteaptă handshake-ul (sau timeout-ul TCP, minute). Simplu, dar poate îngheța. |
| **Non-blocking** | Întoarce `-1` cu `errno == EINPROGRESS` imediat. Aștepți cu `poll(POLLOUT)`, apoi verifici `getsockopt(SO_ERROR)`: `0` = conectat, altfel codul erorii (`ECONNREFUSED`, `ETIMEDOUT`, …). |

```cpp
// Non-blocking, robust
::fcntl(fd, F_SETFL, ::fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
int rc = ::connect(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr));
if (rc < 0 && errno != EINPROGRESS) { /* eroare imediată */ }
if (rc < 0)                                        // în curs
{
    struct pollfd p; p.fd = fd; p.events = POLLOUT; p.revents = 0;
    if (::poll(&p, 1, 5000) == 1)
    {
        int err = 0; socklen_t elen = sizeof(err);
        ::getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &elen);
        if (err != 0) { /* eșuat: strerror(err) */ }
    }
    else { /* timeout */ }
}
```

**Capcană.** După `EINTR` la `connect()` **nu** reapela `connect()` (conexiunea
continuă în fundal, iar al doilea apel dă `EALREADY`/`EISCONN`); așteaptă cu
`poll(POLLOUT)` + `SO_ERROR`.

---

## 12. `recv()`

**Rol.** Copiază în buffer-ul tău octeți primiți pe socket. Operația e
**parțială**: primești *cel mult* `len` octeți, uneori mult mai puțini.

```cpp
ssize_t recv(int fd, void* buf, size_t len, int flags);
```

| Argument | Semnificație |
|---|---|
| `fd` | socket-ul de client |
| `buf`, `len` | destinația și capacitatea ei. **Nu** citi mai mult decât spațiul liber din bufferul de input al conexiunii |
| `flags` | `0` (normal) sau: `MSG_PEEK` (citește *fără* să consume), `MSG_DONTWAIT` (non-blocking doar pentru acest apel), `MSG_WAITALL` (blochează până la `len` octeți), `MSG_TRUNC`/`MSG_OOB` (rare) |

**Interpretarea valorii întoarse**: aici se fac cele mai multe greșeli.

| Valoare | Semnificație | Acțiune |
|---|---|---|
| `> 0` | `n` octeți primiți (poate `< len`; **nu** e EOF, **nu** e „mesaj complet”) | append exact `n` octeți în buffer |
| `== 0` | peer-ul a închis ordonat (EOF), **sau** ai dat `len == 0` | disconnect; **garda `len > 0`** |
| `-1`, `EINTR` | întrerupt de semnal | reîncearcă / revino la `poll` |
| `-1`, `EAGAIN`/`EWOULDBLOCK` | nu sunt date acum (socket non-blocking) | **nu e eroare**; revino la `poll` |
| `-1`, `ECONNRESET` | peer-ul a trimis RST (a murit / abort) | disconnect |
| `-1`, `ETIMEDOUT`, `ENOTCONN`, altele | conexiune nefuncțională | disconnect |

### Forme de utilizare

```cpp
// 1. Minimă, GREȘITĂ pentru un server real
char buf[512];
recv(fd, buf, sizeof(buf), 0);
std::string cmd(buf);                     // a) fără n (nu ține de lungime), b) trunchiază la NUL,
                                          // c) presupune "o comandă per recv", d) ignoră erorile

// 2. Robustă (din demo): lungime exactă, verdict pe fiecare caz, framing separat
std::size_t room = MAX_IN - c.in.size();
if (room == 0) { markDead(fd); return; }              // recv(len=0) ar da fals EOF
char buf[1024];
ssize_t n = ::recv(fd, buf, room < sizeof(buf) ? room : sizeof(buf), 0);
if (n == 0) { markDead(fd); return; }                 // EOF
if (n < 0)
{
    if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) return;
    markDead(fd); return;                             // ECONNRESET etc.
}
c.in.append(buf, static_cast<std::size_t>(n));        // exact n octeți; NUL-safe
/* apoi extragi linii complete din c.in (framing) */

// 3. Blocantă cu MSG_WAITALL: pentru protocoale binare cu lungime fixă, NU în event loop
```

**`recv` vs `read`.** Pe un socket, `read(fd, buf, len)` este echivalent cu
`recv(fd, buf, len, 0)`. Alege `recv` când vrei flag-uri; `read` când același
cod trebuie să lucreze și cu pipe-uri/fișiere.

**Când `MSG_PEEK`?** Rar: pentru a inspecta antetul înainte să decizi cum
citești. Într-un server IRC cu buffer per conexiune nu este necesar.

**Capcane.** (1) `std::string(buf)` după `recv` (fără `n`). (2) Presupunerea că
un `recv` = o comandă (vezi `T1` din ghidul de robustețe). (3) Tratarea lui
`-1/EAGAIN` ca disconnect. (4) `len == 0` interpretat ca EOF. (5) Un singur
`recv` per iterație cu date rămase în buffer-ul kernelului: cu `poll`
level-triggered nu se pierd, dar dacă limitezi bugetul de linii ai nevoie de
un flag de „backlog” (vezi `L4`).

---

## 13. `send()`

**Rol.** Copiază octeții tăi în bufferul de trimitere al kernelului, care îi
transmite când poate. Întoarce câți octeți a **acceptat**, nu câți au ajuns la
destinatar.

```cpp
ssize_t send(int fd, const void* buf, size_t len, int flags);
```

| Argument | Semnificație |
|---|---|
| `buf`, `len` | ce trimiți |
| `flags` | `0`; **`MSG_NOSIGNAL`** (nu genera `SIGPIPE`; Linux/FreeBSD; lipsește pe macOS); `MSG_DONTWAIT` (non-blocking doar pentru acest apel); `MSG_MORE` (Linux: „urmează mai multe date, nu trimite încă pachetul”) |

| Valoare | Semnificație | Acțiune |
|---|---|---|
| `> 0` | `n` octeți acceptați (poate `< len`) | șterge din coadă exact `n`, păstrează restul |
| `-1`, `EAGAIN`/`EWOULDBLOCK` | bufferul kernel e plin (client lent) | păstrează tot restul, activează `POLLOUT` |
| `-1`, `EINTR` | semnal | reîncearcă |
| `-1`, `EPIPE` | peer-ul a închis; (fără `MSG_NOSIGNAL`/`SIG_IGN`: **`SIGPIPE`**) | disconnect |
| `-1`, `ECONNRESET` | RST primit | disconnect |
| `-1`, `ENOBUFS`/`ENOMEM` | resurse kernel | tratează ca eroare temporară sau disconnect |

### Forme de utilizare

```cpp
// 1. Minimă, GREȘITĂ: presupune scriere completă + poate ucide procesul (SIGPIPE)
send(fd, msg.data(), msg.size(), 0);

// 2. Blocantă „send all”: acceptabilă DOAR pe socket blocant, în clienți simpli/teste
std::size_t off = 0;
while (off < len)
{
    ssize_t n = ::send(fd, p + off, len - off, MSG_NOSIGNAL);
    if (n < 0) { if (errno == EINTR) continue; return false; }
    off += static_cast<std::size_t>(n);
}

// 3. Robustă, NON-BLOCKING, cu coadă de output (din demo)
while (!c.out.empty())
{
    ssize_t n = ::send(fd, c.out.data(), c.out.size(), MSG_NOSIGNAL);
    if (n > 0) { c.out.erase(0, static_cast<std::size_t>(n)); continue; }
    if (n < 0 && errno == EINTR) continue;
    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return;   // așteaptă POLLOUT
    markDead(fd); return;                                              // EPIPE, ECONNRESET…
}
```

**Regula de aur:** în server, *dispatcher-ul nu apelează `send()`*; pune
răspunsul într-o **coadă** (cu limită de octeți), iar bucla I/O o golește
(prima încercare imediat după procesare, restul la `POLLOUT`). Detalii și
limita `MAX_OUTPUT_BYTES` în ghidul de robustețe (`T3`).

**Cum testezi `send()` parțial?** Micșorează `SO_SNDBUF` (§6) și trimite
multe date către un client care nu citește: vei vedea `EAGAIN` real.

**Capcane.** (1) Ignorarea valorii întoarse. (2) `SIGPIPE` neprotejat.
(3) Coadă nelimitată. (4) Activarea permanentă a `POLLOUT` (busy-loop: un
socket gol e *mereu* „scriabil”): îl ceri doar cât coada nu e goală.
(5) `send()` cu `len == 0`: inutil, evită-l.

---

## 14. `poll()`

**Rol.** Blochează procesul până când **cel puțin un fd** dintr-o listă este
gata pentru operația cerută, sau expiră timeout-ul. Este inima event loop-ului.

```cpp
int poll(struct pollfd* fds, nfds_t nfds, int timeout);

struct pollfd {
    int   fd;        // descriptorul urmărit (< 0 => intrarea e ignorată)
    short events;    // CE cere programul (mască de biți)
    short revents;   // CE a găsit kernelul (completat de poll)
};
```

| Argument | Semnificație |
|---|---|
| `fds` | vectorul de `pollfd` (`&v[0]`; **nu** pe vector gol, UB în C++98 → `poll(0, 0, ms)` pentru simplu „sleep”) |
| `nfds` | numărul de intrări (`static_cast<nfds_t>(v.size())`) |
| `timeout` | milisecunde: **`-1`** infinit · **`0`** doar verifică, nu așteaptă · **`> 0`** așteaptă cel mult atât |

### 14.1 Flag-uri

| Flag | În `events` (cerut)? | În `revents` (primit)? | Semnificație |
|---|---|---|---|
| `POLLIN` | da | da | date de citit **sau EOF**; pe listener: o conexiune așteaptă `accept()` |
| `POLLOUT` | da | da | se poate scrie fără blocare (măcar puțin) |
| `POLLPRI` | da | da | date urgente (OOB); nefolosit în IRC |
| `POLLERR` | *(nu e nevoie)* | **da** | eroare pe fd (ex. RST) |
| `POLLHUP` | *(nu e nevoie)* | **da** | conexiune închisă complet (ambele direcții / RST) |
| `POLLNVAL` | *(nu e nevoie)* | **da** | fd invalid/neinchis-corect (**bug de cod**) |
| `POLLRDHUP` | da | da | (Linux, `_GNU_SOURCE`) peer-ul a închis scrierea; non-portabil, nu îl folosi |

`POLLERR/POLLHUP/POLLNVAL` sunt raportate **chiar dacă `events == 0`**. De
aceea un client în starea „închid după flush” (fără `POLLIN`) este totuși
monitorizat pentru erori.

### 14.2 Valoarea întoarsă

| Valoare | Semnificație |
|---|---|
| `> 0` | numărul de intrări cu `revents != 0` |
| `0` | timeout expirat, nimic de făcut (moment bun pentru sarcini periodice: `PING`, deadline-uri) |
| `-1` | eroare: `EINTR` (**normal**: semnal; verifică `g_stop`), `ENOMEM`, `EINVAL` (`nfds` prea mare), `EFAULT` |

### 14.3 Forme de utilizare

```cpp
// 1. Vector reconstruit la FIECARE iterație (din demo; recomandat pentru proiect)
std::vector<struct pollfd> fds;
if (_clients.size() < MAX_CLIENTS)                    // plin => listenerul iese din set
{ struct pollfd p; p.fd = listenFd; p.events = POLLIN; p.revents = 0; fds.push_back(p); }
for (/* fiecare client */)
{
    struct pollfd p; p.fd = fd; p.events = 0; p.revents = 0;
    if (!closing)         p.events |= POLLIN;         // nu mai citim de la un client „în închidere”
    if (!out.empty())     p.events |= POLLOUT;        // POLLOUT doar cât avem ce trimite
    fds.push_back(p);
}
int rc = ::poll(&fds[0], static_cast<nfds_t>(fds.size()), 1000);
if (rc < 0) { if (errno == EINTR) continue; throw std::runtime_error("poll"); }

// 2. Vector persistent: mai eficient, dar cere disciplină la adăugări/ștergeri
//    (amânate la finalul iterației). Un fd „dezactivat” temporar: p.fd = -1 (ignorat).
```

**Ordinea de tratare a evenimentelor** (motivată în ghidul de robustețe, `T7`):

1. `POLLNVAL | POLLERR` → elimină clientul.
2. `POLLIN` → **citește întâi** (chiar dacă e și `POLLHUP`: pot exista ultimele comenzi).
3. `POLLHUP` fără `POLLIN` → elimină.
4. `POLLOUT` → `send()` din coadă (doar dacă clientul nu a fost eliminat între timp).

### 14.4 Comportamente care surprind

- **Level-triggered:** `poll` raportează cât timp condiția *persistă*. `POLLIN`
  pe un client cu date necitite revine imediat la fiecare apel. Bine dacă
  citești; busy-loop dacă îl ignori.
- **`poll` nu garantează că `recv/send/accept` vor reuși** (ex. `accept` după
  ce clientul a renunțat → `ECONNABORTED`; un alt fir/proces a luat evenimentul).
  De aceea fd-urile sunt non-blocking: o presupunere greșită nu îngheață serverul.
- **`revents` este suprascris la fiecare apel**: nu trebuie curățat, dar
  `events` îl reconstruiești/actualizezi tu.
- **Cost:** kernelul parcurge tot vectorul la fiecare apel: O(n). Excelent
  până la câteva mii de fd-uri; peste, `epoll` (Linux) / `kqueue` (BSD).
- **Semnal între verificarea `g_stop` și `poll(-1)`:** serverul rămâne blocat
  până la un eveniment. Folosește timeout periodic (ex. 1000 ms), `ppoll` sau
  self-pipe.

### 14.5 `poll` vs `select` vs `epoll`

| | `select` | `poll` | `epoll`/`kqueue` |
|---|---|---|---|
| Limită de fd | **`FD_SETSIZE` (~1024)**; `FD_SET` peste limită = corupere de memorie | fără limită fixă | fără limită fixă |
| Interfață | 3 bitmap-uri, refăcute la fiecare apel | vector `pollfd` | obiect persistent în kernel |
| Cost/apel | O(n) | O(n) | O(evenimente) |
| Portabil | da | da | nu (Linux / BSD) |
| Recomandare aici | **interzis** | **da** | dincolo de scopul proiectului |

---

## 15. `shutdown()` și `close()`

**`close(fd)`**: eliberează descriptorul. Când ultimul fd către socket se
închide, kernelul încheie conexiunea. **Un singur proprietar** trebuie să
închidă (RAII, vezi `Fd`).

**`shutdown(fd, how)`**: închide **o direcție** a conexiunii fără să elibereze fd-ul.

```cpp
int shutdown(int fd, int how);
```

| `how` | Efect |
|---|---|
| `SHUT_RD` | nu mai citești (rar util) |
| `SHUT_WR` | trimite **FIN**: „nu mai am nimic de spus”; mai poți citi |
| `SHUT_RDWR` | ambele |

### Forme de închidere

```cpp
// 1. Minimă: close() direct
::close(fd);

// 2. Ordonată (din demo): golește coada, FIN, apoi eliberează
//    (în flushClient, după ce out e gol și clientul e „closing”):
::shutdown(fd, SHUT_WR);          // peer-ul citește tot, apoi vede EOF
_dead.insert(fd);                 // reap() => delete Client => ~Fd => close(fd)

// 3. Cu deadline: un client în „Closing” care nu citește nu trebuie să ocupe
//    resurse la infinit: o marcă de timp + verificare la fiecare timeout de poll.
```

**Capcană importantă:** dacă apelezi `close()` cât în bufferul de recepție al
serverului mai există date *necitite* de la client, TCP poate trimite **RST**
în loc de FIN, iar clientul poate pierde mesajul final (`ERROR :Closing link`
sau răspunsul la `QUIT`). Într-un test pe loopback Linux am observat că
mesajul a ajuns oricum; pe rețele reale sau alte sisteme nu e garantat, deci
**nu vă bazați pe testul local**. Mitigare: golește coada → `shutdown(SHUT_WR)`
→ (opțional) mai citește și aruncă date scurt timp → `close()`.

**`close()` și `EINTR`:** pe Linux fd-ul este oricum eliberat; **nu reapela**
`close()` (ai putea închide un fd realocat între timp de alt fir).

**`SO_LINGER`:** schimbă semnificația lui `close()` (așteaptă transmiterea
sau abandonează cu RST). Neutilizat în producție; util în teste pentru a
simula `ECONNRESET` (`{l_onoff=1, l_linger=0}`).

---

## 16. `getsockname()` / `getpeername()`

```cpp
int getsockname(int fd, struct sockaddr* addr, socklen_t* addrlen); // adresa LOCALĂ a fd
int getpeername(int fd, struct sockaddr* addr, socklen_t* addrlen); // adresa PEER-ului
```

Ambele: value-result (`addrlen` inițializat!). Întorc `0` / `-1`
(`ENOTCONN` la `getpeername` pe un socket neconectat).

| Uz | Apel |
|---|---|
| Aflii portul ales de kernel după `bind(port=0)` | `getsockname` + `ntohs(sin_port)` (demo: `boundPort()`; util în teste automate) |
| Hostname/IP-ul clientului pentru prefixul IRC `nick!user@host` | `accept()` îl dă deja; `getpeername` dacă nu l-ai păstrat |
| Diagnostic: pe ce IP local a ajuns conexiunea | `getsockname` pe fd-ul acceptat |

```cpp
struct sockaddr_in bound; socklen_t len = sizeof(bound);
if (::getsockname(fd, reinterpret_cast<struct sockaddr*>(&bound), &len) == 0)
    std::cout << "LISTENING " << ntohs(bound.sin_port) << std::endl;
```

**Nu face DNS invers în bucla de evenimente.** `getnameinfo`/`gethostbyaddr`
blochează (secunde) și îngheață toți clienții. Folosește adresa **numerică**
(`inet_ntop`, sau `getnameinfo` cu `NI_NUMERICHOST`).

---

## 17. `getaddrinfo()` și familia lui

**Rol.** Convertește un nume/serviciu în liste de adrese gata de folosit,
independent de IPv4/IPv6, în locul completării manuale a `sockaddr_in`.

```cpp
int  getaddrinfo(const char* node, const char* service,
                 const struct addrinfo* hints, struct addrinfo** res);
void freeaddrinfo(struct addrinfo* res);
const char* gai_strerror(int code);      // erorile NU sunt în errno, ci coduri EAI_*
```

| Argument | Semnificație |
|---|---|
| `node` | nume/IP (`"127.0.0.1"`, `"localhost"`); `NULL` + `AI_PASSIVE` = adresa wildcard (pentru server) |
| `service` | port ca text (`"6667"`) sau nume de serviciu |
| `hints` | filtre: `ai_family` (`AF_UNSPEC`, `AF_INET`, `AF_INET6`), `ai_socktype` (`SOCK_STREAM`), `ai_flags`: `AI_PASSIVE` (server), `AI_NUMERICHOST` (fără DNS), `AI_NUMERICSERV`, `AI_ADDRCONFIG` |
| `res` | ieșire: listă înlănțuită (`ai_next`), fiecare element cu `ai_family`, `ai_socktype`, `ai_protocol`, `ai_addr`, `ai_addrlen` |

```cpp
// Listener robust, agnostic IPv4/IPv6
struct addrinfo hints, *res = 0;
std::memset(&hints, 0, sizeof(hints));
hints.ai_family   = AF_UNSPEC;
hints.ai_socktype = SOCK_STREAM;
hints.ai_flags    = AI_PASSIVE | AI_NUMERICSERV;
int rc = ::getaddrinfo(0, "6667", &hints, &res);
if (rc != 0) throw std::runtime_error(std::string("getaddrinfo: ") + ::gai_strerror(rc));
int fd = -1;
for (struct addrinfo* ai = res; ai != 0 && fd < 0; ai = ai->ai_next)
{
    int s = ::socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
    if (s < 0) continue;
    int one = 1; ::setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    if (::bind(s, ai->ai_addr, ai->ai_addrlen) == 0) fd = s; else ::close(s);
}
::freeaddrinfo(res);                          // ALTFEL: leak
```

**Când ce.** Pentru un server IRC școlar cu IPv4, `sockaddr_in` + `inet_pton`
(§4) e mai simplu și mai transparent. `getaddrinfo` devine preferabil când
cerința include IPv6, nume de host sau clienți care se conectează la nume.
Cu un `node` de tip nume, **rezolvarea DNS este blocantă**: acceptabilă doar la
pornire, niciodată în event loop. Verificați dacă regulamentul permite funcția.

---

## 18. Semnale legate de socket-uri

- **`SIGPIPE`**: scrierea pe o conexiune închisă de peer termină procesul
  implicit. Protecție dublă: `MSG_NOSIGNAL` pe fiecare `send()` **și**
  `sigaction(SIGPIPE, SIG_IGN)` la pornire (portabil; pe macOS `MSG_NOSIGNAL`
  lipsește: `#ifndef MSG_NOSIGNAL / # define MSG_NOSIGNAL 0`, iar alternativa
  per-socket e `SO_NOSIGPIPE`).
- **`SIGINT`/`SIGTERM`**: handlerul setează **doar** `volatile sig_atomic_t`;
  fără `SA_RESTART`, ca `poll()` să iasă cu `EINTR`; shutdown-ul are loc în
  contextul normal (vezi `T8`).
- **`EINTR`** apare la `poll`, `accept`, `recv`, `send`, `connect`: nu e o
  eroare, e „revino”.

```cpp
struct sigaction sa;  std::memset(&sa, 0, sizeof(sa));
sa.sa_handler = onSignal;  sigemptyset(&sa.sa_mask);  sa.sa_flags = 0;
sigaction(SIGINT, &sa, 0);  sigaction(SIGTERM, &sa, 0);
struct sigaction ign; std::memset(&ign, 0, sizeof(ign));
ign.sa_handler = SIG_IGN; sigemptyset(&ign.sa_mask);
sigaction(SIGPIPE, &ign, 0);
```

---

## 19. Matricea de erori: ce faci la fiecare `errno`

`✔` = reîncearcă/ignoră (normal) · `✖` = elimină clientul · `⚠` = log + situație de sistem · `FATAL` = oprește pornirea/serverul

| `errno` | `poll` | `accept` | `recv` | `send` | `connect` | `bind`/`listen` |
|---|---|---|---|---|---|---|
| `EINTR` | ✔ verifică `g_stop` | ✔ | ✔ | ✔ | așteaptă `POLLOUT` | – |
| `EAGAIN`/`EWOULDBLOCK` | – | ✔ ieși din buclă | ✔ înapoi la `poll` | ✔ păstrează coada, `POLLOUT` | – | – |
| `ECONNABORTED` | – | ✔ | – | – | – | – |
| `ECONNRESET` | – | – | ✖ | ✖ | ⚠ | – |
| `EPIPE` | – | – | – | ✖ | – | – |
| `ETIMEDOUT` | – | – | ✖ | ✖ | ⚠ | – |
| `ECONNREFUSED` | – | – | – | – | ⚠ (nimeni nu ascultă) | – |
| `EINPROGRESS` | – | – | – | – | ✔ așteaptă | – |
| `EMFILE`/`ENFILE` | – | ⚠ vezi §10 | – | – | ⚠ | FATAL la `socket` inițial |
| `ENOBUFS`/`ENOMEM` | ⚠ | ⚠ | ⚠ | ⚠ | ⚠ | FATAL |
| `EADDRINUSE` | – | – | – | – | – | FATAL, mesaj clar |
| `EACCES` | – | – | – | – | – | FATAL (port < 1024) |
| `EBADF`/`ENOTSOCK`/`EINVAL` | FATAL (bug) | FATAL (bug) | bug | bug | bug | bug |
| `ENOTCONN` | – | – | ✖ | ✖ | – | – |

Regulă: la orice `errno` neprevăzut într-un apel pe un **client**, elimină
clientul (nu oprești serverul); la orice eroare pe **listener** sau la
pornire, decizi între log+continuă și oprire.

---

## 20. Tabele de decizie: ce variantă alegi și când

| Situație | Alege | De ce |
|---|---|---|
| Un singur client, exercițiu | socket-uri blocante, `accept`/`recv`/`send` simple | cel mai simplu; **nu** scalează |
| Server IRC (mulți clienți, un fir) | non-blocking + `poll` + buffere per conexiune | un client lent/tăcut nu blochează pe ceilalți |
| Peste mii de conexiuni | `epoll`/`kqueue` | `poll` e O(n) per apel |
| Adresa de bind în dezvoltare | `127.0.0.1` | nu expui accidental serverul |
| Adresa de bind în producție | `INADDR_ANY` (sau IP anume) | accesibil din rețea |
| Port | `>= 1024`; `0` doar în teste automate | `< 1024` cere privilegii |
| `SO_REUSEADDR` | da, la listener, înainte de `bind` | repornire rapidă |
| `SO_REUSEPORT` | nu | ascunde greșeli (două instanțe) |
| Detectarea peer-ilor morți | `PING`/`PONG` IRC | `SO_KEEPALIVE` e prea lent implicit (~2 h) |
| Trimiterea răspunsurilor | coadă + `send` în I/O loop | `send` parțial, `EAGAIN`, backpressure |
| Protecție `SIGPIPE` | `MSG_NOSIGNAL` **+** `SIG_IGN` | portabil și precis |
| Închiderea unui client | golește coada → `shutdown(SHUT_WR)` → `close` | livrezi mesajul final |
| Eroare pe un client | `markForRemoval`, ștergere la finalul iterației | fără iteratori/pointeri invalizi |
| Adrese IPv4 simple | `sockaddr_in` + `inet_pton` | transparent |
| IPv6 / nume de host | `getaddrinfo` | agnostic de familie |
| Afișare peer | `inet_ntop` (numeric) | fără DNS blocant în bucla de evenimente |

---

## 21. Capcane frecvente (listă de verificare)

- [ ] `htons` pe port; `memset` pe `sockaddr_in`; `sizeof(sockaddr_in)` la `bind`.
- [ ] `SO_REUSEADDR` **înainte** de `bind`.
- [ ] `listen()` chemat (altfel `ECONNREFUSED`).
- [ ] `socklen_t` value-result inițializat la `accept`/`getsockname`.
- [ ] `O_NONBLOCK` pe **fiecare** fd acceptat.
- [ ] `accept` în buclă până la `EAGAIN`; limită de clienți și listener scos din `poll` când e plin.
- [ ] `recv`: `n == 0` = EOF; `-1/EAGAIN` ≠ disconnect; nu `std::string(buf)`; nu `len == 0`.
- [ ] `send`: rezultat verificat; coadă cu limită; `MSG_NOSIGNAL` + `SIGPIPE` ignorat.
- [ ] `POLLOUT` doar cât coada nu e goală; `POLLIN` citit înaintea `POLLHUP`.
- [ ] `poll`: `EINTR` tratat, timeout periodic, nu `&v[0]` pe vector gol.
- [ ] Un singur proprietar pentru fiecare fd (`Fd` RAII), `close` o singură dată.
- [ ] Fără `select()`, fără DNS blocant în bucla principală.
- [ ] Fiecare apel are ramura de eroare **scrisă**, nu „o să meargă”.

---

## 22. Exerciții pentru juniori (cu `strace`)

Rulați serverul din anexă sub `strace` și corelați ce ați citit cu ce vedeți:

```bash
g++ -std=c++98 -Wall -Wextra -Werror socket_api_demo.cpp -o demo
strace -f -e trace=network,poll,fcntl,close ./demo 0 2>&1 | head -60
```

1. **Ordinea de setup.** Găsiți în `strace` secvența `socket → setsockopt(SO_REUSEADDR) → bind → listen → fcntl`. Ce se întâmplă dacă mutați `setsockopt` după `bind`? Repornire rapidă după un test cu clienți conectați.
2. **`htons`.** Scoateți `htons` pe port; ce port apare la `bind` în `strace`? (Calculați-l manual din §3.)
3. **Non-blocking.** Comentați `setNonBlocking(cfd)`: conectați un client tăcut și un al doilea. Ce apel rămâne blocat în `strace` (`recvfrom`) și ce se întâmplă cu al doilea client?
4. **Level-triggered.** Comentați bucla de `accept` (păstrați un singur `accept`) și conectați rapid 5 clienți: număratul apelurilor `poll` în `strace`.
5. **`POLLOUT` permanent.** Setați `events |= POLLOUT` mereu; urmăriți în `strace` apelurile `poll` care revin instant (CPU 100% în `top`).
6. **`send` parțial.** Rulați `./demo 0 127.0.0.1 1024` (SNDBUF mic), conectați un client care trimite multe linii fără să citească; observați `send(...) = -1 EAGAIN` și apoi dropul la `MAX_OUT`.
7. **Client cu RST.** Dintr-un script Python setați `SO_LINGER (1,0)` și închideți: ce `errno` vede serverul la `recv`? Rămâne serverul în picioare?
8. **`SIGINT`.** Apăsați `Ctrl+C` în timp ce nu există clienți: ce apel iese din blocare și cu ce `errno` (`EINTR`)? Verificați `echo $?` = `0`.
9. **Limita de clienți.** Reduceți `MAX_CLIENTS` la 2, conectați 3 clienți; cine așteaptă și unde (în backlog-ul kernelului)? Verificați cu `ss -ltn` (coloana `Recv-Q` pe listener).

Comenzi utile: `ss -ltnp` (ce ascultă), `ss -tan` (stări TCP, `TIME_WAIT`), `lsof -p <pid>`
(fd-uri deschise), `cat /proc/sys/net/core/somaxconn`, `ulimit -n`, `nc -v 127.0.0.1 <port>`.

---

## 23. Anexă: `socket_api_demo.cpp` explicat

Programul livrat lângă acest ghid este un server de „echo pe linii” (fiecare linie primită → `ECHO: <linie>`, `QUIT` → `BYE` + închidere ordonată), scris în forma **robustă** a fiecărui apel.

| Fragment din demo | Apel / concept | Secțiune |
|---|---|---|
| `class Fd` | RAII: un singur proprietar al fd-ului | 5, 15 |
| `createListener`: `socket` → `setsockopt(SO_REUSEADDR)` → `inet_pton` → `bind` → `listen` → `setNonBlocking` | construcția unui listener; `Fd guard` + `release()` | 5–9 |
| `htons`, `memset`, `sockaddr_in` | adresa | 3, 4 |
| `boundPort()` | `getsockname` + `ntohs` (port `0`) | 16 |
| `Server::run`: vector `pollfd` reconstruit, listener scos când e plin, `POLLOUT` doar cu coadă | `poll` | 14 |
| `acceptAll()`: buclă până la `EAGAIN`, `ECONNABORTED`, `setNonBlocking(cfd)`, `inet_ntop` | `accept` | 10 |
| `readClient()`: gardă `room==0`, `n==0`/`n<0`, `append(buf, n)`, linii cu `\n`/`\r\n`, limită 512 | `recv` + framing | 12 |
| `flushClient()`: `MSG_NOSIGNAL`, ștergere exactă a lui `n`, `EAGAIN` → așteaptă `POLLOUT`, `shutdown(SHUT_WR)` la final | `send`, `shutdown` | 13, 15 |
| `_dead` + `reap()` | marcare + ștergere la finalul iterației | ghid robustețe `L2`/`L3` |
| `sigaction` (`SIGINT`, `SIGTERM`, `SIGPIPE`) | semnale | 18 |
| `parseStrictLong` în `main` | validare port fără `atoi` | ghid robustețe `P6` |

**Rulare și verificare rapidă**

```bash
g++ -std=c++98 -Wall -Wextra -Werror socket_api_demo.cpp -o demo
./demo 0                                   # afișează: LISTENING <port>
printf 'hello\r\nQUIT\r\n' | nc 127.0.0.1 <port>
# Fragmentare:  ( printf 'he'; sleep 1; printf 'llo\r\n'; sleep 1 ) | nc 127.0.0.1 <port>
# Linie prea lungă: head -c 1000 /dev/zero | tr '\0' 'A' | nc 127.0.0.1 <port>
```

**Ce NU este demo-ul:** nu implementează IRC (nicio comandă, niciun canal) și
folosește o coadă `std::string` simplă. Este scheletul de transport pe care
se montează `Connection`, `User`, parserul și dispatcher-ul din ghidul de
robustețe. Diferența față de acel ghid: aici `\n` este acceptat ca terminator
alături de `\r\n` (decizia de la `T1`, tolerantă pentru `nc`).
