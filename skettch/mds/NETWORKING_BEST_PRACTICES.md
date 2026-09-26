# Bune practici pentru server TCP cu `poll()` în C++98

Am inspectat [Server.cpp](./srcs/Server.cpp), [TCPSocket.cpp](./srcs/TCPSocket.cpp), [User.hpp](./headers/User.hpp) și componentele de parsing/dispatch.

## Concluzie

Structura este un început bun pentru un server single-threaded cu `poll()`:

- un socket de listening;
- socket-uri client non-blocking;
- un singur event loop;
- separare între transport, user și command dispatcher.

Totuși, în forma actuală nu este încă robustă pentru TCP real. Cele mai importante probleme sunt framing-ul mesajelor, gestionarea `send()`, ownership-ul descriptorilor și modificarea vectorului de `pollfd` în timpul iterației.

## Probleme importante în implementarea actuală

### 1. Socket-urile client nu sunt închise corect

În [User.cpp](./srcs/User.cpp), destructorul lui `User` este gol, deși `User` deține logic pointerul către `ISocket`.

În [Server.cpp](./srcs/Server.cpp), se creează un `TCPSocket` și apoi un `User`, dar la ștergerea userului se șterge doar `User`, nu și `TCPSocket`.

Rezultatul:

- leak de memorie pentru fiecare client;
- descriptorul clientului rămâne deschis;
- eventual epuizare de file descriptors.

Trebuie stabilită explicit o singură politică de ownership:

```cpp
User::~User()
{
    delete _socket;
}
```

sau, preferabil, `Server` să dețină socket-ul separat și `User` să aibă doar o referință/pointer non-owning. În C++98, este esențial să documentați clar cine închide fiecare fd.

### 2. `recv()` nu respectă framing-ul TCP

În [Server.cpp](./srcs/Server.cpp), fiecare rezultat al unui `recv()` este tratat drept o comandă completă.

TCP nu păstrează granițele mesajelor. Pot apărea:

```text
recv #1: "PRI"
recv #2: "VMSG ..."
```

sau:

```text
recv #1: "PING 1\r\nNICK bob\r\n"
```

Serverul trebuie să aibă un input buffer per client:

```cpp
std::string inputBuffer;
```

La fiecare `recv()`:

1. se adaugă datele în buffer;
2. se caută delimiterul `\r\n`;
3. se procesează toate comenzile complete;
4. se păstrează fragmentul incomplet pentru următorul `recv()`.

Exemplu conceptual:

```cpp
client.input.append(buffer, bytesRead);

while (true)
{
    std::string::size_type end = client.input.find("\r\n");
    if (end == std::string::npos)
        break;

    std::string line = client.input.substr(0, end);
    client.input.erase(0, end + 2);

    processCommand(client, line);
}
```

Trebuie impuse și limite:

- maximum input buffer per client;
- maximum lungime comandă;
- maximum număr de comenzi procesate per iterație, pentru a evita starvation.

Pentru IRC, limita uzuală a unei linii este aproximativ 512 bytes, dacă aceasta este cerința protocolului vostru.

### 3. `send()` poate trimite doar o parte din date

În [TCPSocket.cpp](./srcs/TCPSocket.cpp), `send()` este apelat o singură dată:

```cpp
return ::send(_fd, data, size, 0);
```

TCP poate accepta doar o parte din buffer. De exemplu, pot fi ceruți 1000 bytes, dar trimiși doar 350.

Fiecare client trebuie să aibă:

```cpp
std::string outputBuffer;
```

La trimitere:

- se adaugă mesajul în `outputBuffer`;
- se activează `POLLOUT`;
- la eveniment `POLLOUT`, se trimite cât se poate;
- se elimină doar bytes trimiși;
- se dezactivează `POLLOUT` când bufferul devine gol.

Nu efectuați `send()` blocant sau bucle lungi de retry în handlerul unui client. Un client lent nu trebuie să blocheze întregul server.

### 4. Risc de `SIGPIPE`

În [TCPSocket.cpp](./srcs/TCPSocket.cpp), `send()` folosește flag-ul `0`.

Pe Linux, trimiterea către un socket închis poate genera `SIGPIPE` și poate termina procesul.

Opțiuni:

```cpp
::send(fd, data, size, MSG_NOSIGNAL);
```

sau:

```cpp
signal(SIGPIPE, SIG_IGN);
```

Prima variantă este mai locală și mai sigură. Dacă portabilitatea este importantă, tratați această diferență la nivelul clasei `TCPSocket`.

### 5. `accept()` acceptă un singur client

La un singur eveniment `POLLIN` se face un singur `accept()`. Dacă există mai multe conexiuni în queue, serverul procesează doar una și revine în `poll()`.

Este mai robust să goliți queue-ul până la `EAGAIN`:

```cpp
while (true)
{
    int clientFd = accept(_serverFd, ...);

    if (clientFd < 0)
    {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            break;

        if (errno == EINTR)
            continue;

        logAcceptError();
        break;
    }

    configureClient(clientFd);
}
```

Pe sisteme Linux moderne se poate folosi `accept4(..., SOCK_NONBLOCK | SOCK_CLOEXEC)`, dar dacă ținta este strict POSIX/C++98, păstrați `accept()` plus configurare separată.

### 6. `fcntl(F_SETFL, O_NONBLOCK)` poate șterge alte flag-uri

Codul de forma:

```cpp
fcntl(_serverFd, F_SETFL, O_NONBLOCK);
```

suprascrie toate file status flags existente. Practica corectă este:

```cpp
int flags = fcntl(fd, F_GETFL, 0);
if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0)
{
    // tratați eroarea
}
```

Verificați și valoarea returnată pentru toate apelurile `socket`, `setsockopt`, `fcntl`, `bind`, `listen`, `accept`, `recv`, `send` și `close`.

### 7. Tratarea incompletă a `pollfd.revents`

Trebuie tratate explicit:

```cpp
POLLERR
POLLHUP
POLLNVAL
POLLIN
POLLOUT
```

Ordinea recomandată:

```cpp
short events = pfd.revents;

if (events & (POLLERR | POLLHUP | POLLNVAL))
{
    disconnectClient(fd);
    continue;
}

if (events & POLLIN)
    readFromClient(fd);

if (events & POLLOUT)
    flushClientOutput(fd);
```

`POLLHUP` poate apărea împreună cu `POLLIN`; trebuie întâi citite datele disponibile dacă protocolul vostru cere procesarea lor, apoi închis clientul când `recv()` returnează zero.

### 8. Modificarea `_pollFds` în timpul iterației

`handleClientData()` poate apela `removeClient()`, care face `erase()` din `_pollFds`. Asta invalidează logic indexarea curentă și poate:

- sări peste următorul client;
- procesa un alt fd la index greșit;
- produce comportament dependent de ordinea conexiunilor.

Soluția recomandată în C++98 este să marcați clienții pentru ștergere:

```cpp
std::vector<int> toRemove;
```

După terminarea buclei:

```cpp
for (...)
    removeClient(toRemove[i]);
```

Astfel, modificările asupra colecției de conexiuni se aplică după dispatch.

### 9. `poll()` trebuie tratat cu `EINTR`

Orice rezultat negativ al lui `poll()` nu trebuie considerat automat eroare fatală:

```cpp
int result = poll(&_pollFds[0], _pollFds.size(), -1);

if (result < 0)
{
    if (errno == EINTR)
        continue;

    throw PollError();
}
```

În implementarea actuală, `SIGINT` întrerupe `poll()`, apoi bucla iese prin eroare înainte ca shutdown-ul să fie gestionat curat.

### 10. Handler-ul de semnal nu este async-signal-safe

În [main.cpp](./srcs/main.cpp), handlerul:

- folosește `std::cout`;
- apelează `globalServer->stop()`;
- eliberează memorie;
- închide fd-uri;
- modifică structuri STL.

Aceste operații nu sunt sigure într-un signal handler.

Minimul acceptabil:

```cpp
volatile sig_atomic_t g_stop = 0;

void signalHandler(int)
{
    g_stop = 1;
}
```

Event loop-ul verifică `g_stop` și execută `stop()` în context normal.

Pentru shutdown imediat fără timeout, o soluție robustă este self-pipe:

- handler-ul scrie un byte într-un pipe;
- read end-ul pipe-ului intră în `_pollFds`;
- bucla primește `POLLIN` și face shutdown normal.

### 11. Închiderea listenerului pe erori este incompletă

Dacă `bind()` sau `listen()` eșuează, `_serverFd` trebuie închis înainte de return.

Folosiți cleanup centralizat:

```cpp
bool Server::initServer()
{
    _serverFd = socket(...);
    if (_serverFd < 0)
        return false;

    if (bind(...) < 0)
    {
        close(_serverFd);
        _serverFd = -1;
        return false;
    }

    // ...
}
```

Ideal, folosiți un wrapper RAII pentru fd:

```cpp
class FileDescriptor
{
private:
    int _fd;

public:
    explicit FileDescriptor(int fd = -1);
    ~FileDescriptor();

    int get() const;
    int release();
    void reset(int fd);
};
```

În C++98, RAII este înlocuitorul practic pentru `unique_ptr` și `shared_ptr`.

### 12. Ownership inconsistent pentru `User`

Operații precum `addUser()` și `removeUser()` trebuie să respecte aceeași politică de ownership. Altfel pot apărea:

- leak de `User`;
- pointer dangling;
- dublă deținere neclară;
- user prezent în map fără fd în `_pollFds`.

Recomandare: `Server` să fie owner unic al clienților și să aibă operații interne precum:

```cpp
User* createClient(int fd);
void destroyClient(int fd);
```

`addUser()` și `removeUser()` ar trebui fie eliminate, fie să gestioneze explicit și consistent memoria.

## Arhitectură recomandată pentru C++98

### Structură per client

În loc de `map<int, User*>` plus socket separat, puteți avea o structură internă:

```cpp
struct Client
{
    int         fd;
    User*       user;
    std::string input;
    std::string output;
    bool        closing;

    Client() : fd(-1), user(NULL), closing(false) {}
};
```

Serverul menține:

```cpp
std::map<int, Client*> _clients;
std::vector<struct pollfd> _pollFds;
```

Fiecare client are:

- input buffer;
- output buffer;
- stare de autentificare;
- eventual timestamp pentru timeout;
- pointer către `User`, dacă păstrați separarea actuală.

### Event loop recomandat

```cpp
while (_running)
{
    int ready = poll(&_pollFds[0], _pollFds.size(), 1000);

    if (ready < 0)
    {
        if (errno == EINTR)
            continue;

        handleFatalPollError();
        break;
    }

    std::vector<int> clientsToRemove;

    for (size_t i = 0; i < _pollFds.size(); ++i)
    {
        int fd = _pollFds[i].fd;
        short revents = _pollFds[i].revents;

        if (revents & (POLLERR | POLLHUP | POLLNVAL))
        {
            clientsToRemove.push_back(fd);
            continue;
        }

        if (fd == _serverFd)
        {
            acceptAllPendingClients();
            continue;
        }

        if (revents & POLLIN)
            readClient(fd, clientsToRemove);

        if (revents & POLLOUT)
            writeClient(fd, clientsToRemove);
    }

    removeMarkedClients(clientsToRemove);
}
```

Important: nu presupuneți că fd-urile din vector rămân stabile după fiecare callback.

## Validare și tratarea erorilor

### `recv()`

- `> 0`: date primite;
- `== 0`: peer shutdown ordonat;
- `< 0` + `EAGAIN/EWOULDBLOCK`: nimic momentan;
- `< 0` + `EINTR`: retry;
- altă eroare: disconnect.

### `send()`

- `> 0`: eliminați exact bytes trimiși;
- `== 0`: tratați ca situație anormală, fără buclă infinită;
- `< 0` + `EAGAIN/EWOULDBLOCK`: păstrați bufferul și așteptați `POLLOUT`;
- `< 0` + `EINTR`: retry limitat sau reveniți în event loop;
- `EPIPE/ECONNRESET`: disconnect.

### `accept()`

- `EAGAIN/EWOULDBLOCK`: ați golit queue-ul;
- `EMFILE/ENFILE`: limita de fd-uri a fost atinsă;
- `EINTR`: retry;
- alte erori: log și continuați, fără să omorâți automat serverul.

## Alte bune practici

1. **Folosiți `SO_REUSEADDR` și verificați rezultatul.** `SO_REUSEPORT` nu trebuie adăugat automat; are semantică diferită.

2. **Folosiți `FD_CLOEXEC`.** Evitați ca descriptorii să fie moșteniți accidental de procese lansate ulterior.

3. **Nu expuneți direct syscall-urile în tot proiectul.** Centralizați operațiile în `TCPSocket` sau într-un wrapper `Socket`.

4. **Nu folosiți `std::map::operator[]` pentru lookup-uri care pot lipsi.** Folosiți `find()` și verificați iteratorul.

5. **Normalizați comenzile.** Lexer-ul actual parsează datele primite ca un singur mesaj și nu face framing robust pentru CRLF/LF, linii goale sau comenzi multiple.

6. **Limitați resursele per client:**
   - input buffer;
   - output buffer;
   - număr de comenzi procesate într-un tick;
   - număr maxim de clienți;
   - eventual idle timeout.

7. **Nu logați date arbitrare fără limite.** Input-ul clientului poate conține caractere de control sau payload extrem de mare.

8. **Verificați corect portul.** `atoi()` nu detectează input invalid precum `"123abc"` sau overflow. În C++98 puteți folosi `strtol()` și verifica `errno`, `endptr` și intervalul.

9. **Folosiți teste de stres:**
   - client care trimite câte un byte;
   - două comenzi într-un singur `send`;
   - 100+ conexiuni simultane;
   - client care nu citește răspunsurile;
   - client care închide imediat după conectare;
   - `SIGINT` în timpul unui `poll`;
   - mesaje mai mari decât bufferul local.

10. **Compilați cu warning-uri stricte**, de exemplu:

```text
-Wall -Wextra -Werror -pedantic
```

și, când este posibil, rulați:

```text
-fsanitize=address,undefined
```

chiar dacă producția trebuie să rămână C++98.

## Prioritatea recomandată

1. ownership și închiderea corectă a socket-urilor;
2. input buffering și procesarea comenzilor delimitate de `\r\n`;
3. output queue cu `POLLOUT`;
4. tratarea `POLLHUP`, `POLLERR` și `POLLNVAL`;
5. eliminarea modificărilor directe ale `_pollFds` în timpul iterării;
6. signal handling sigur;
7. acceptarea tuturor conexiunilor pending;
8. validarea și logging-ul tuturor syscall-urilor;
9. limitele de memorie și protecția împotriva clientului lent;
10. teste de fragmentare, partial writes și shutdown.

În esență, modelul corect este:

> **non-blocking sockets + input/output buffers per client + `poll()` ca mecanism de readiness + modificări de conexiuni aplicate controlat după dispatch**

Parserul și dispatcher-ul nu ar trebui să presupună niciodată că un `recv()` corespunde exact unei comenzi.
