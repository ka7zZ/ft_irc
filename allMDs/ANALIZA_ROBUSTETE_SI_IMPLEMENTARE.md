# Audit de robustețe și propunere de implementare pentru serverul IRC

## 1. Scop și delimitare

Acest fișier este livrabilul auditului. Nu aplică modificări în sursele
serverului. El conține:

1. cazurile-limită care pot produce crash, pierderi de date sau blocarea
   întregului server;
2. cauza tehnică a fiecărei probleme;
3. soluția recomandată;
4. implementarea propusă, sub formă de design și fragmente C++98;
5. teste care trebuie scrise înainte de integrarea soluțiilor.

Proiectul trebuie compilat cu C++98, deci nu se presupun `nullptr`,
`std::unique_ptr`, lambda-uri sau move semantics.

## 2. Verdict

Prototipul din `skettch` are o separare bună între socket, utilizator, lexer,
AST și dispatcher, dar transportul TCP este încă fragil. Problemele prioritare
sunt:

- un `recv()` este tratat ca o comandă IRC completă;
- un `send()` este considerat implicit complet;
- `SIGPIPE` poate termina procesul;
- `User` nu eliberează socketul primit;
- `poll()` și lista de clienți pot fi modificate în timpul procesării;
- `accept()` și operațiile POSIX nu diferențiază toate cazurile de eroare;
- handlerul de semnal execută operații nesigure;
- nickname-urile nu sunt validate și nu sunt unice;
- nu există limite pentru input și output;
- parserul nu raportează explicit inputul invalid.

Obiectivul realist este ca un client defect sau ostil să poată fi deconectat
controlat, fără să afecteze ceilalți clienți și fără să provoace crash.

## 3. Model mental pentru un student

Un file descriptor este un număr pe care kernelul îl folosește pentru a
identifica un socket. Fluxul normal este:

```text
socket -> setsockopt -> bind -> listen -> accept
recv/send -> close
```

Socketul de listening nu transportă mesaje IRC. El anunță că există o
conexiune nouă pentru `accept()`. Fiecare client primește un descriptor separat.

`poll()` nu citește și nu scrie date. El răspunde la întrebarea: „pe ce
descriptor merită să încerc acum o operație fără să blochez?”. `POLLIN`
înseamnă că se poate încerca `recv()`, iar `POLLOUT` că se poate încerca
`send()`.

TCP este un flux de bytes, nu un protocol cu pachete de comenzi. Dacă clientul
trimite:

```text
PI
NG :abc\r\nPING :xyz\r\n
```

serverul poate primi exact aceste două blocuri, un singur bloc sau alte
fragmentări. Doar `\r\n` marchează sfârșitul unei comenzi IRC.

## 4. Cazuri-limită și soluții propuse

### 4.1 Framing TCP

**Problemă:** `handleClientData()` trimite blocul primit direct lexerului.

**Consecințe:**

- comenzi fragmentate sunt respinse sau interpretate greșit;
- două comenzi în același `recv()` pot deveni una singură;
- inputul fără `\r\n` nu are un comportament clar.

**Implementare propusă:** adăugarea unui buffer per conexiune:

```cpp
class LineFramer
{
private:
    std::string _input;

public:
    bool append(const char *data, std::size_t size)
    {
        if (_input.size() > 4096 || size > 4096 - _input.size())
            return false;
        _input.append(data, size);
        return true;
    }

    bool popLine(std::string& line)
    {
        std::string::size_type end = _input.find("\r\n");
        if (end == std::string::npos)
            return false;
        if (end + 2 > 512)
            return false;
        line.assign(_input, 0, end);
        _input.erase(0, end + 2);
        return true;
    }
};
```

`recv()` adaugă bytes în framer, iar parserul primește doar linii complete.
Depășirea limitelor înseamnă disconnect controlat, nu alocare nelimitată.

### 4.2 `send()` parțial și client lent

**Problemă:** `send()` poate transmite doar o parte din date sau poate întoarce
`EAGAIN`. Dacă rezultatul este ignorat, mesajele se pierd.

**Implementare propusă:** coadă de output per client:

```cpp
bool queueOutput(const std::string& message)
{
    if (_output.size() > 65536
        || message.size() > 65536 - _output.size())
        return false;
    _output += message;
    return true;
}

bool flushOutput(int fd)
{
    while (!_output.empty())
    {
        ssize_t count = ::send(fd, _output.data(), _output.size(),
                               MSG_NOSIGNAL);
        if (count > 0)
        {
            _output.erase(0, static_cast<std::size_t>(count));
            continue;
        }
        if (count < 0 && errno == EINTR)
            continue;
        if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
            return true;
        return false;
    }
    return true;
}
```

`POLLOUT` trebuie activat numai cât timp coada nu este goală. Limita previne
consumul infinit de memorie de către un client care nu citește.

### 4.3 `SIGPIPE`

Un peer poate închide conexiunea exact înaintea unui `send()`. `MSG_NOSIGNAL`
împiedică terminarea procesului și permite tratarea erorii prin return value și
`errno`. Alternativa globală este ignorarea `SIGPIPE`, dar flagul local este
mai precis pentru această operație.

### 4.4 Ownership pentru socketuri

În forma actuală, `User` primește un `ISocket*`, dar destructorul nu îl șterge.
Rezultatul este leak de memorie și descriptor la fiecare client.

Designul recomandat este:

```text
Server deține User
User deține ISocket
TCPSocket deține fd-ul
```

Destructorul `User` șterge socketul, iar destructorul `TCPSocket` face
`close(_fd)` dacă descriptorul este valid. Copierea claselor owning trebuie
dezactivată în C++98 prin declararea constructorului de copiere și a
operatorului de atribuire în `private`.

Trebuie ales un singur proprietar. Nu este permis ca și `Server`, și `User` să
închidă același descriptor.

### 4.5 Rezultatele POSIX

Tratamentul obligatoriu este:

```text
recv > 0                    date disponibile
recv == 0                   peer-ul a închis ordonat
recv < 0, EINTR             repetă operația
recv < 0, EAGAIN/EWOULDBLOCK revino la poll
recv < 0, altă eroare       elimină clientul
```

Aceleași reguli se aplică la `send()`. Pentru `accept()`:

- `EINTR`: repetă;
- `EAGAIN`/`EWOULDBLOCK`: nu mai sunt clienți în coadă;
- altă eroare: loghează și nu crea un `User` invalid.

### 4.6 Invalidarea listei `pollfd`

Adăugarea și ștergerea elementelor din `_pollFds` în timp ce vectorul este
parcurs poate deplasa indicii sau invalida iteratorii.

Implementarea propusă este procesarea unui snapshot:

```cpp
std::vector<struct pollfd> ready = _pollFds;
for (std::size_t i = 0; i < ready.size(); ++i)
{
    int fd = ready[i].fd;
    if (ready[i].revents & (POLLERR | POLLHUP | POLLNVAL))
        markForRemoval(fd);
    else if (ready[i].revents & POLLIN)
        readClient(fd);
    if (ready[i].revents & POLLOUT)
        writeClient(fd);
}
removeMarkedClients();
```

Înaintea fiecărei operații trebuie verificat că descriptorul încă există.

### 4.7 `SIGINT`

Handlerul actual scrie în `std::cout` și apelează `stop()`. STL-ul și
destructorii nu sunt async-signal-safe.

Implementarea propusă:

```cpp
volatile std::sig_atomic_t g_stop = 0;

extern "C" void onSignal(int)
{
    g_stop = 1;
}
```

Bucla verifică `g_stop`, apoi execută `stop()` în context normal. Handlerul nu
face logging, nu modifică map-uri și nu închide obiecte complexe.

### 4.8 Nickname-uri

`NICK` trebuie să verifice:

- nickname nenul;
- lungime maximă;
- primul caracter permis;
- caracterele rămase permise;
- unicitate case-insensitive.

Schimbarea trebuie făcută atomically la nivelul registry-ului. Dacă verificarea
eșuează, nickname-ul anterior rămâne neschimbat.

### 4.9 Parser și AST

`ASTParser::peekToken()` trebuie apelat numai când `_index < _tokens.size()`.
Parserul ar trebui să aibă un rezultat explicit:

```cpp
struct ParseResult
{
    bool ok;
    std::string error;
    MessageAST *message;

    ParseResult() : ok(false), message(NULL) {}
};
```

Linia goală, comanda lipsă și parametrii invalizi nu trebuie să producă un AST
care pare valid.

### 4.10 Canale și disconnect

Când un client pleacă, trebuie eliminat din toate canalele. Canalele fără membri
trebuie șterse din registry. Invariantele necesare sunt:

```text
user.channels conține channel
    <=> channel.members conține user
```

Eliminarea trebuie făcută într-un singur serviciu, nu în mai multe handlers.

## 5. Ordinea recomandată de implementare

1. Definirea ownership-ului pentru `User`, `TCPSocket` și descriptor.
2. Introducerea bufferului persistent de input.
3. Introducerea cozii de output și a limitei de backpressure.
4. Tratarea explicită a `EINTR`, `EAGAIN`, EOF și `POLL*`.
5. Stabilizarea event loop-ului cu snapshot sau liste de removals.
6. Corectarea handlerului de semnal.
7. Validarea nickname-urilor și introducerea registry-ului.
8. Curățarea canalelor la disconnect.
9. Testarea parserului și a tuturor limitelor.

## 6. Teste obligatorii înainte de integrare

1. `PI` + `NG :abc\r\n` produce un singur `PING`.
2. Două comenzi într-un singur bloc produc două AST-uri.
3. O linie incompletă rămâne în buffer.
4. O linie de 513 bytes este respinsă.
5. `send()` parțial păstrează restul mesajului.
6. `EAGAIN` nu închide clientul.
7. EOF elimină clientul și socketul.
8. `POLLHUP` nu provoacă acces la un pointer șters.
9. Nickname-urile duplicate sunt respinse.
10. `SIGINT` nu modifică STL-ul din handler.
11. Bufferul de output depășit produce disconnect controlat.
12. Un canal gol este eliminat.

## 7. Rezultat așteptat după aplicarea propunerii

Serverul nu trebuie să promită că orice input este acceptat. Trebuie să
promită că inputul invalid este respins controlat și că un client defect nu
poate:

- să corupă memoria;
- să blocheze ceilalți clienți;
- să consume memorie nelimitat;
- să piardă tăcut output valid;
- să termine procesul prin `SIGPIPE`;
- să lase descriptor-i sau obiecte în urmă.

## 8. Fișierele care ar trebui modificate ulterior

Implementarea efectivă ar trebui să fie împărțită în:

- [User.hpp](/home/alexkatz/Documents/GitHub/PENDING/ft_irc/skettch/headers/User.hpp)
  și [User.cpp](/home/alexkatz/Documents/GitHub/PENDING/ft_irc/skettch/srcs/User.cpp):
  stare per conexiune, framer și coadă;
- [TCPSocket.hpp](/home/alexkatz/Documents/GitHub/PENDING/ft_irc/skettch/headers/TCPSocket.hpp)
  și [TCPSocket.cpp](/home/alexkatz/Documents/GitHub/PENDING/ft_irc/skettch/srcs/TCPSocket.cpp):
  ownership și I/O;
- [Server.hpp](/home/alexkatz/Documents/GitHub/PENDING/ft_irc/skettch/headers/Server.hpp)
  și [Server.cpp](/home/alexkatz/Documents/GitHub/PENDING/ft_irc/skettch/srcs/Server.cpp):
  event loop și lifecycle;
- [CommandDispatcher.cpp](/home/alexkatz/Documents/GitHub/PENDING/ft_irc/skettch/srcs/CommandDispatcher.cpp):
  validare și enqueue, nu `send()` direct;
- [main.cpp](/home/alexkatz/Documents/GitHub/PENDING/ft_irc/skettch/srcs/main.cpp):
  signal flag.

În starea actuală, aceste fișiere nu sunt modificate de acest livrabil.
