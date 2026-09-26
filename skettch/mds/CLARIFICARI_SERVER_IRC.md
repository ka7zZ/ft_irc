# Clarificari tehnice: cele doua servere IRC demo

Acest document explica nelamuririle legate de [serverul integrat](demo_sources/srcs/Server.cpp) si [serverul modular](another_demo/srcs/Server.cpp). Exemplele sunt raportate la codul existent, nu la un server IRC complet conform RFC.

> Terminologie: `poll()` este o functie POSIX de multiplexare I/O, nu o “piscina” de thread-uri. Ea asteapta evenimente pe mai multe file descriptor-uri in acelasi timp.

---

# I. Serverul integrat

## 1. De ce este importanta functia `poll()`?

Un server IRC trebuie sa poata deservi simultan:

- socket-ul serverului, pe care apar conexiuni noi;
- clientul A, care trimite `NICK`;
- clientul B, care trimite `PRIVMSG`;
- clientul C, care nu trimite nimic;
- clienti carora trebuie sa le fie livrate date.

O varianta simpla, dar slaba, ar fi:

```cpp
accept(listenerFd, ...); // asteapta aici pana apare un client
recv(clientA, ...);      // poate astepta aici la nesfarsit
send(clientA, ...);      // poate astepta daca bufferul TCP este plin
```

Daca `recv(clientA, ...)` asteapta, serverul nu mai ajunge la clientii B si C. Un singur client lent poate bloca tot serverul.

In serverul integrat, bucla principala foloseste:

```cpp
if (poll(&descriptors[0], descriptors.size(), -1) < 0)
{
    if (errno == EINTR)
        continue;
    throw std::runtime_error("poll failed");
}
```

`poll()` asteapta pana cand cel putin un descriptor este pregatit. Argumentul `-1` inseamna “asteapta fara timeout”. Functia nu primeste datele in locul serverului; doar spune **pe ce descriptor merita incercata operatia**.

Beneficiul este multiplexarea intr-un singur thread:

1. serverul asteapta evenimente;
2. `poll()` se trezeste cand exista activitate;
3. serverul citeste doar socket-urile pregatite;
4. revine in `poll()`.

Complexitatea este aproximativ proportionala cu numarul de descriptor-i scanati la fiecare trezire. Pentru un demo este potrivit. La mii sau zeci de mii de conexiuni, un programator ar evalua `epoll` pe Linux, `kqueue` pe BSD/macOS sau o biblioteca portabila.

---

## 2. Cum pune serverul socket-ul de ascultare in lista monitorizata?

Mai intai, constructorul creeaza socket-ul, il leaga la port si il pune in stare de ascultare:

```cpp
listenerFd = socket(AF_INET, SOCK_STREAM, 0);

sockaddr_in address;
std::memset(&address, 0, sizeof(address));
address.sin_family = AF_INET;
address.sin_addr.s_addr = htonl(INADDR_ANY);
address.sin_port = htons(port);

bind(listenerFd, reinterpret_cast<sockaddr *>(&address),
     sizeof(address));
listen(listenerFd, 16);
```

`listenerFd` nu reprezinta un client. Este un socket special care primeste notificari ca exista conexiuni in asteptare.

La fiecare iteratie, el este primul element din vector:

```cpp
std::vector<pollfd> descriptors;
pollfd listener;
listener.fd = listenerFd;
listener.events = POLLIN;
listener.revents = 0;
descriptors.push_back(listener);
```

Structura `pollfd` are, conceptual:

```cpp
struct pollfd {
    int   fd;       // descriptorul monitorizat
    short events;   // ce evenimente cerem
    short revents;  // ce evenimente s-au produs
};
```

Pentru socket-ul de ascultare, `POLLIN` nu inseamna “se poate citi un mesaj IRC”. Inseamna “exista cel putin o conexiune care poate fi acceptata fara blocare”.

Dupa `poll()`:

```cpp
if (descriptors[0].revents & POLLIN)
    acceptClients();
```

Iar `acceptClients()` face efectiv:

```cpp
int fd = accept(listenerFd, NULL, NULL);
sockets[fd] = new Socket(fd);
```

Asadar, lista monitorizata contine:

```text
listenerFd  -> conexiuni noi
clientFd A  -> date de la clientul A
clientFd B  -> date de la clientul B
```

---

## 3. Rolul `POLLIN` si `POLLOUT`

### `POLLIN`

`POLLIN` cere notificare cand descriptorul poate fi citit fara blocare.

Pentru listener:

```cpp
listener.events = POLLIN;
```

inseamna “exista o conexiune noua pentru `accept()`”.

Pentru un client:

```cpp
descriptor.events = POLLIN;
```

inseamna “exista octeti disponibili pentru `recv()`” sau clientul a inchis conexiunea, situatie care trebuie verificata prin rezultatul lui `recv()`.

### `POLLOUT`

`POLLOUT` cere notificare cand exista spatiu in bufferul kernel pentru a incerca `send()`.

Serverul il activeaza doar daca are date de transmis:

```cpp
descriptor.events = POLLIN;
if (!it->second->outputBuffer.empty())
    descriptor.events |= POLLOUT;
```

Aceasta conditie este importanta. Daca serverul ar cere `POLLOUT` permanent, un socket TCP este de obicei aproape mereu “writable”, iar `poll()` s-ar trezi continuu chiar daca nu exista nimic de trimis. Ar consuma CPU inutil.

La eveniment:

```cpp
if (sockets.find(fd) != sockets.end() && (events & POLLOUT))
    writeTo(sockets[fd]);
```

`POLLIN` si `POLLOUT` nu garanteaza ca operatia va procesa totul; doar indica faptul ca merita incercata. Starea se poate schimba intre `poll()` si `recv()`/`send()`, mai ales in sisteme complexe, de aceea codul trebuie sa trateze si `EAGAIN`.

---

## 4. De ce bufferul de iesire este necesar?

Un apel:

```cpp
send(fd, data, length, 0);
```

nu promite ca trimite `length` bytes. Poate returna:

- un numar pozitiv mai mic decat `length`;
- `-1` cu `EAGAIN` daca socket-ul non-blocking nu poate accepta acum date;
- `-1` cu alta eroare;
- `0` in cazuri speciale, care trebuie tratate cu prudenta.

Serverul nu trimite direct raspunsul. Il pune in coada per client:

```cpp
void Server::send(Socket *socket, const std::string &message)
{
    socket->outputBuffer += message;
}
```

Apoi `writeTo()` consuma coada:

```cpp
while (!socket->outputBuffer.empty())
{
    ssize_t count = ::send(socket->fd,
                           socket->outputBuffer.data(),
                           socket->outputBuffer.size(),
                           MSG_NOSIGNAL);
    if (count < 0)
    {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return;
        closeClient(socket->fd);
        return;
    }
    socket->outputBuffer.erase(0, count);
}
```

Exemplu: daca bufferul contine 1000 bytes, dar `send()` trimite 300:

```text
inainte:  [1000 bytes]
send():   trimite 300
dupa:     [700 bytes]
```

La urmatorul `POLLOUT`, serverul continua de la restul de 700. Fara buffer, serverul ar pierde partea netrimisa sau ar trebui sa blocheze thread-ul pana cand clientul o accepta.

Un server de productie ar lua in calcul si:

- limita maxima a bufferului de iesire per client;
- politica pentru clientii lenti;
- backpressure;
- inchiderea sau incetinirea clientului care acumuleaza prea multe date;
- cozi structurate, nu neaparat concatenare repetata de `std::string`.

---

## 5. Socket-uri blocking versus non-blocking

### Socket blocking

Este comportamentul implicit. Un apel poate astepta:

```cpp
recv(fd, buffer, size, 0);
```

pana cand apare cel putin un byte, clientul inchide conexiunea sau apare o eroare.

La `send()`, apelul poate astepta daca bufferul de transmitere al kernelului este plin.

Blocking este simplu si poate fi potrivit pentru:

- un client dedicat unui thread;
- un program secvential;
- o operatiune unde asteptarea este intentionata si izolata.

Dar este periculos in bucla principala a unui server single-threaded: un client lent poate bloca toti ceilalti clienti.

### Socket non-blocking

Operatia revine imediat:

- cu date, daca exista date;
- cu `-1` si `EAGAIN`/`EWOULDBLOCK`, daca momentan nu exista progres;
- cu eroare reala sau EOF, dupa caz.

Serverul integrat seteaza non-blocking pentru listener:

```cpp
int flags = fcntl(listenerFd, F_GETFL, 0);
fcntl(listenerFd, F_SETFL, flags | O_NONBLOCK);
```

si pentru fiecare client:

```cpp
int flags = fcntl(fd, F_GETFL, 0);
fcntl(fd, F_SETFL, flags | O_NONBLOCK);
```

Motivul este compatibilitatea cu `poll()`: dupa ce `poll()` indica disponibilitate, serverul poate incerca operatia fara sa blocheze intregul proces.

Non-blocking nu inseamna “mai rapid” in sine. Inseamna ca programul trebuie sa gestioneze explicit starea “nu exista progres acum”. Este mai greu de programat, dar esential pentru multiplexare eficienta intr-un singur thread.

---

## 6. `EAGAIN`, `EWOULDBLOCK` si eroarea din `accept()`

Pe Linux, `EAGAIN` si `EWOULDBLOCK` au adesea aceeasi valoare. Semantic, indica faptul ca operatia ar bloca, dar descriptorul este non-blocking. Nu este, in mod normal, o defectiune a conexiunii.

Codul de citire trateaza corect cazul:

```cpp
if (count < 0)
{
    if (errno == EAGAIN || errno == EWOULDBLOCK)
        break;
    closeClient(socket->fd);
    return;
}
```

Interpretarea este:

- `count == 0`: peer-ul a inchis ordonat conexiunea;
- `count < 0` si `EAGAIN`/`EWOULDBLOCK`: nu mai sunt date acum; revino in `poll()`;
- `count < 0` cu alta eroare: conexiune defecta sau eroare I/O; inchide clientul.

La `accept()`:

```cpp
int fd = accept(listenerFd, NULL, NULL);
if (fd < 0)
{
    if (errno == EAGAIN || errno == EWOULDBLOCK)
        return;
    return;
}
```

Prima ramura este corecta: coada de conexiuni a fost golita. Problema este a doua: orice alta eroare este ignorata.

De ce conteaza? Pentru ca erori diferite pot indica:

- descriptor invalid (`EBADF`);
- permisiuni (`EACCES`);
- resurse epuizate (`EMFILE`, `ENFILE`);
- memorie insuficienta (`ENOBUFS`, `ENOMEM`);
- intrerupere de semnal (`EINTR`);
- o problema temporara sau una permanenta.

Un programator experimentat ar loga si ar clasifica eroarea:

```cpp
if (fd < 0)
{
    if (errno == EAGAIN || errno == EWOULDBLOCK)
        return;
    if (errno == EINTR)
        continue;
    throw std::runtime_error(std::string("accept failed: ")
                             + std::strerror(errno));
}
```

In anumite servere, `EMFILE`/`ENFILE` necesita o strategie speciala de “spare descriptor”, limitare de clienti sau backoff. A ignora eroarea transforma o problema operationala intr-un comportament misterios.

---

# II. Serverul modular

## 1. De ce ascundem `send()` si `recv()` in spatele unei interfete?

In [ISocket.hpp](another_demo/headers/ISocket.hpp), serverul vede abstractia:

```cpp
class ISocket {
public:
    virtual ~ISocket() {};
    virtual int getFd() const = 0;
    virtual ssize_t send(const void* data, size_t size) = 0;
    virtual ssize_t receive(void *buffer, size_t size) = 0;
};
```

Implementarea concreta este in [TCPSocket.cpp](another_demo/srcs/TCPSocket.cpp):

```cpp
ssize_t TCPSocket::send(const void* data, size_t size)
{
    return ::send(_fd, data, size, 0);
}

ssize_t TCPSocket::receive(void* buffer, size_t size)
{
    return ::recv(_fd, buffer, size, 0);
}
```

“A ascunde” inseamna ca restul programului nu trebuie sa cunoasca:

- faptul ca se foloseste POSIX;
- numele descriptorului intern `_fd`;
- cum se inchide descriptorul;
- cum va fi inlocuita implementarea.

`User` foloseste doar:

```cpp
user.getSocket()->send(data, size);
```

Fara interfata, `User` ar depinde direct de `sys/socket.h` si de reprezentarea concreta a socket-ului.

Beneficii:

1. **Separarea responsabilitatilor**: `TCPSocket` stie I/O, `User` stie identitatea clientului.
2. **Testare**: putem injecta un socket fals.
3. **Portare**: putem crea `TlsSocket`, `UnixSocket` sau o implementare pentru alt backend.
4. **Control architectural**: mai tarziu putem pune buffering, logging sau retry in adaptor.

Interfata nu rezolva automat partial writes, framing sau ownership. Ea doar ofera un punct clar unde aceste politici pot fi introduse.

---

## 2. Ce este `TRAILING` si de ce este important?

Protocolul IRC are o forma in care ultimul parametru poate incepe cu `:`. Tot textul dupa acel `:` devine un singur parametru, inclusiv spatiile.

Exemplu:

```text
PRIVMSG alex :Salut, acesta este un mesaj cu mai multe cuvinte\r\n
```

Daca toate campurile ar fi separate simplu dupa spatiu, textul ar deveni:

```text
PRIVMSG | alex | Salut, | acesta | este | un | mesaj | cu | mai | multe | cuvinte
```

IRC are nevoie de:

```text
comanda: PRIVMSG
parametrul 1: alex
parametrul 2: "Salut, acesta este un mesaj cu mai multe cuvinte"
```

In [Lexer.cpp](another_demo/srcs/Lexer.cpp):

```cpp
if (peek() == ':')
{
    advance();
    size_t start = _index;
    while (!isAtEnd() && peek() != '\r' && peek() != '\n')
        advance();

    Token tk;
    tk.type = TRAILING;
    tk.value = _src.substr(start, _index - start);
    tokens.push_back(tk);
    break;
}
```

`TRAILING` nu inseamna “un mesaj de log”. Este o categorie sintactica: “ultimul parametru, care poate contine spatii si se intinde pana la finalul liniei”.

---

## 3. De ce `TRAILING` devine `ParamNode`?

Lexer-ul descrie forma textuala. Parserul trebuie sa construiasca forma semantica folosita de comenzi.

Ambele token-uri:

```cpp
PARAMETER
TRAILING
```

reprezinta, pentru un handler, un argument al comenzii. Diferenta de sintaxa este pastrata prin `isTrailing`.

In [ASTParser.cpp](another_demo/srcs/ASTParser.cpp):

```cpp
if (tk.type == PARAMETER || tk.type == TRAILING)
{
    ParamNode* param = new ParamNode();
    param->value = tk.value;
    param->isTrailing = (tk.type == TRAILING);
    msg.params.push_back(param);
}
```

Deci nu se pierde diferenta:

```text
Token(PARAMETER, "#general")
    -> ParamNode("#general", false)

Token(TRAILING, "Salut tuturor")
    -> ParamNode("Salut tuturor", true)
```

Handler-ul poate folosi:

```cpp
ast.params[0]->value; // destinatia
ast.params[1]->value; // textul
```

iar, daca are nevoie:

```cpp
ast.params[1]->isTrailing
```

Important: `MessageAST` nu combina arbitrar parametrii. El doar ii transforma intr-un vector comun de `ParamNode`, pastrand flag-ul `isTrailing`. Parserul stie care a fost trailing deoarece token-ul original are `type == TRAILING`.

---

## 4. Ce este un mock si ce teste ar trebui sa existe?

Un mock este un obiect fals, controlat de test, care implementeaza aceeasi interfata ca obiectul real. Pentru [ISocket](another_demo/headers/ISocket.hpp), un mock poate inregistra ce s-a trimis si poate furniza date prestabilite:

```cpp
class MockSocket : public ISocket
{
public:
    std::string sent;
    std::string received;
    int fd;

    MockSocket(int id) : fd(id) {}

    int getFd() const { return fd; }

    ssize_t send(const void* data, size_t size)
    {
        sent.append(static_cast<const char*>(data), size);
        return size;
    }

    ssize_t receive(void* buffer, size_t size)
    {
        size_t count = received.size();
        if (count > size)
            count = size;
        std::memcpy(buffer, received.data(), count);
        received.erase(0, count);
        return count;
    }
};
```

Cu un mock nu este nevoie de port TCP real, `nc`, timing sau clienti externi pentru a testa handler-ul:

```cpp
MockSocket socket(10);
User user(10, &socket);
MessageAST ast;
// ast.command = "PING"; ast.params contine un ParamNode

CommandDispatcher dispatcher;
dispatcher.dispatch(server, user, ast);

assert(socket.sent == "PONG: server\r\n");
```

### Set minim de teste recomandat

#### Lexer

- comanda fara parametri;
- prefix optional;
- parametri multipli;
- trailing cu spatii;
- trailing gol (`CMD :`);
- linii cu spatii suplimentare;
- `\r\n` si `\n`;
- input gol;
- comanda lowercase, daca protocolul trebuie normalizat.

#### Parser AST

- prefixul ajunge in `MessageAST::prefix`;
- comanda ajunge in `MessageAST::command`;
- ordinea parametrilor este pastrata;
- `TRAILING` devine `ParamNode` cu `isTrailing == true`;
- destructorul elibereaza toti `ParamNode`.

#### Protocol si dispatcher

- `NICK` seteaza nickname-ul;
- `USER` seteaza username-ul;
- `PING` trimite raspunsul corect;
- `PRIVMSG` catre un utilizator existent;
- `PRIVMSG` catre un utilizator inexistent;
- comanda necunoscuta;
- parametri lipsa;
- nickname duplicat si nickname invalid;
- comanda inainte de inregistrare, daca protocolul o interzice.

#### Server si I/O

- acceptarea unei conexiuni;
- inchiderea normala a clientului (`recv() == 0`);
- eroare la `recv()`;
- `POLLERR`, `POLLHUP`, `POLLNVAL`;
- doua comenzi intr-un singur chunk;
- o comanda impartita in mai multe chunk-uri;
- partial write;
- client lent;
- `SIGINT` si inchidere curata.

Testele end-to-end cu socket-uri reale raman necesare. Mock-urile verifica logica rapid si determinist; nu pot demonstra singure ca kernelul, `poll()` si reteaua sunt integrate corect.

---

## 5. De ce evitam un lant lung de `if/else if/else`?

Un dispatcher monolitic ar arata astfel:

```cpp
if (command == "NICK")
    handleNick(...);
else if (command == "USER")
    handleUser(...);
else if (command == "PING")
    handlePing(...);
else if (command == "PRIVMSG")
    handlePrivMsg(...);
else if (command == "JOIN")
    handleJoin(...);
// ...
```

Problemele cresc odata cu comenzile:

- clasa devine lunga;
- modificarea unei comenzi risca sa afecteze altele;
- testarea necesita parcurgerea unei functii centrale;
- devine mai greu de separat permisiunile si validarea;
- codul poate ajunge la duplicate si cazuri tratate inconsistent.

Demo-ul modular foloseste:

```cpp
_handlers["NICK"] = &CommandDispatcher::handleNick;
_handlers["USER"] = &CommandDispatcher::handleUser;
_handlers["PING"] = &CommandDispatcher::handlePing;
_handlers["PRIVMSG"] = &CommandDispatcher::handlePrivMsg;
```

si:

```cpp
std::map<std::string, CommandHandler>::iterator it =
    _handlers.find(ast.command);

if (it != _handlers.end())
    it->second(server, user, ast);
```

Avantajul este un tabel de dispatch. In proiecte mari, un programator ar merge mai departe cu:

- handler-e in clase separate;
- obiecte de comanda cu interfata comuna;
- un registry de comenzi;
- validatori reutilizabili;
- raspunsuri numerice standardizate.

Un lant scurt nu este automat rau. Problema este lungimea, amestecarea responsabilitatilor si lipsa unei structuri care sa creasca odata cu protocolul.

---

## 6. De ce este un punct slab lipsa bufferului per conexiune?

In [Server::handleClientData](another_demo/srcs/Server.cpp), codul face:

```cpp
char buffer[1024];
ssize_t bytesRead =
    _users[clientFd]->getSocket()->receive(buffer, sizeof(buffer) - 1);

if (bytesRead <= 0)
{
    removeClient(clientFd);
    return;
}

buffer[bytesRead] = '\0';
std::string rawData(buffer);

Lexer lxr(rawData);
std::vector<Token> tks = lxr.tokenize();
```

Aici se presupune implicit ca un `recv()` corespunde exact unui mesaj IRC. TCP nu ofera aceasta garantie.

### Cazul A: comanda fragmentata

Clientul trimite:

```text
PRIVMSG alex :Salut
```

dar kernelul livreaza:

```text
recv 1: "PRIVMSG alex :Sa"
recv 2: "lut\r\n"
```

Prima parte este lexata si dispatch-uită ca mesaj incomplet. A doua parte este tratata ca o comanda separata, deci mesajul se corupe.

### Cazul B: doua comenzi in acelasi recv

Kernelul poate livra:

```text
NICK alex\r\nUSER alex 0 * :Alex\r\n
```

intr-un singur `recv()`. Lexer-ul actual nu creeaza doua mesaje independente; va trata continutul ca un singur flux de token-uri.

### Solutia

Fiecare `User` sau fiecare obiect de conexiune trebuie sa aiba un buffer:

```cpp
struct ClientState {
    std::string inputBuffer;
    std::string outputBuffer;
    User* user;
};
```

Algoritmul corect:

```cpp
state.inputBuffer.append(buffer, bytesRead);

std::string::size_type end;
while ((end = state.inputBuffer.find("\r\n"))
       != std::string::npos)
{
    std::string line = state.inputBuffer.substr(0, end);
    state.inputBuffer.erase(0, end + 2);

    processOneIrcLine(line);
}
```

Bucla proceseaza toate comenzile complete si pastreaza coada incompleta pentru urmatorul `recv()`.

---

## 7. Este rolul serverului sa receptioneze “un pachet TCP format din doua pachete”?

Nu in sensul unui pachet logic IRC. TCP nu expune aplicatiei pachetele IP/TCP ca mesaje. Aplicatia vede un **flux ordonat de octeti**.

Reteaua poate:

- fragmenta un mesaj intr-un segment;
- combina doua scrieri intr-un segment;
- retransmite intern;
- reordona la nivelul IP, apoi TCP reface ordinea inainte ca aplicatia sa citeasca.

Serverul nu trebuie sa recunoasca pachete IP individuale. Trebuie sa reconstruiasca mesajele aplicatiei folosind un protocol de framing. Pentru IRC, delimitatorul este de obicei `\r\n`.

Exemplu conceptual:

```text
Aplicatie client:
    write("NICK alex\r\n");
    write("USER alex 0 * :Alex\r\n");

TCP poate livra:
    "NICK alex\r\nUSER alex 0 * :Alex\r\n"

sau:
    "NICK alex\r"
    "\nUSER alex 0 * :Alex\r\n"
```

Ambele trebuie sa produca doua mesaje IRC. De aceea framing-ul este responsabilitatea stratului aplicatiei, nu a TCP.

---

## 8. De ce sunt importante `POLLERR`, `POLLHUP` si `POLLNVAL`?

Serverul modular verifica doar:

```cpp
if (_pollFds[i].revents & POLLIN)
{
    // accept sau handleClientData
}
```

Acest lucru poate rata stari importante.

### `POLLERR`

Indica o eroare asociata descriptorului. Nu trebuie interpretat ca un mesaj IRC. In mod normal:

1. se logheaza;
2. se incearca obtinerea erorii detaliate, de exemplu cu `getsockopt(..., SO_ERROR, ...)`;
3. se inchide clientul daca nu exista recuperare.

### `POLLHUP`

Indica hang-up: peer-ul a inchis sau conexiunea nu mai poate continua. Uneori `POLLIN` si `POLLHUP` apar impreuna; serverul trebuie sa citeasca datele ramase si apoi sa inchida cand `recv()` returneaza `0`, nu sa presupuna orbește ca nu mai exista date.

### `POLLNVAL`

Indica un file descriptor invalid, de exemplu inchis deja sau introdus gresit in vector. Este o eroare de stare interna. Trebuie eliminat din `_pollFds` si investigat ownership-ul, nu tratat ca trafic normal.

Un model de baza:

```cpp
short events = _pollFds[i].revents;

if (events & POLLNVAL)
{
    removeClient(_pollFds[i].fd);
    continue;
}

if (events & (POLLERR | POLLHUP))
{
    handlePossibleRemainingDataOrClose(_pollFds[i].fd);
    continue;
}

if (events & POLLIN)
    handleClientData(_pollFds[i].fd);
```

Ordinea exacta depinde de politica serverului si de faptul ca `POLLHUP` poate coexista cu date inca necitite.

---

## 9. Ce inseamna ca `server` neutilizat indica integrare incompleta?

Handler-ele au semnatura:

```cpp
typedef void (*CommandHandler)(Server&, User&, const MessageAST&);
```

Dar unele nu folosesc serverul:

```cpp
void CommandDispatcher::handleNick(Server& server,
                                   User& user,
                                   const MessageAST& ast)
{
    if (ast.params.empty())
        return;
    user.setNickname(ast.params[0]->value);
}
```

Parametrul exista deoarece toate handler-ele au aceeasi semnatura, dar `NICK` nu verifica:

- daca nickname-ul este deja folosit de alt client;
- daca formatul este valid;
- daca trebuie actualizate indexuri;
- daca schimbarea nickname-ului trebuie anuntata altor clienti;
- daca utilizatorul are dreptul sa schimbe nickname-ul in starea curenta.

Acest lucru este un semnal architectural: handler-ul modifica doar obiectul local, desi regula reala apartine serverului si afecteaza toti clientii.

### Cum ar rezolva un programator experimentat?

Ar evita sa puna logica globala direct in `User`. De exemplu:

```cpp
bool Server::tryChangeNickname(User& user,
                               const std::string& newNick,
                               std::string& error)
{
    if (!isValidNickname(newNick))
    {
        error = "Erroneous nickname";
        return false;
    }

    User* existing = getUserByNick(newNick);
    if (existing != NULL && existing != &user)
    {
        error = "Nickname is already in use";
        return false;
    }

    user.setNickname(newNick);
    return true;
}
```

Iar handler-ul ar coordona:

```cpp
void CommandDispatcher::handleNick(Server& server,
                                   User& user,
                                   const MessageAST& ast)
{
    if (ast.params.empty())
        return;

    std::string error;
    if (!server.tryChangeNickname(user, ast.params[0]->value, error))
    {
        sendNumericError(user, error);
        return;
    }
}
```

O arhitectura mai curata separa:

- parserul: “ce a trimis clientul?”;
- validatorul: “parametrii au forma corecta?”;
- serverul/registry-ul: “este schimbarea consistenta global?”;
- handler-ul: “coordoneaza operatia si raspunsul”.

In plus, trebuie eliminate warning-urile `-Werror` prin folosirea parametrului sau printr-o semnatura mai potrivita. Nu este recomandat sa ascunzi problema cu cast-uri inutile.

---

## 10. Cum ar arata o robustete TCP ridicata?

Un server modular robust ar separa explicit patru niveluri:

```text
socket non-blocking
    -> multiplexare poll/epoll
    -> buffer de intrare/iesire per client
    -> framing IRC cu CRLF
    -> lexer/parser pentru o singura linie
    -> validare si dispatch
```

### Structura recomandata

```cpp
struct ClientConnection {
    int fd;
    TCPSocket socket;
    std::string inputBuffer;
    std::string outputBuffer;
    User user;
    bool closing;
};
```

### Citire robusta

```cpp
void Server::handleReadable(ClientConnection& client)
{
    char buffer[4096];

    while (true)
    {
        ssize_t count = client.socket.receive(buffer, sizeof(buffer));

        if (count > 0)
        {
            client.inputBuffer.append(buffer, count);

            if (client.inputBuffer.size() > MAX_INPUT_BUFFER)
            {
                disconnect(client, "input buffer too large");
                return;
            }
            continue;
        }

        if (count == 0)
        {
            disconnect(client, "peer closed");
            return;
        }

        if (errno == EAGAIN || errno == EWOULDBLOCK)
            break;

        disconnect(client, "receive failed");
        return;
    }

    processCompleteLines(client);
}
```

Observatie: bucla citeste pana la `EAGAIN`, nu doar un singur `recv()`. Apoi:

```cpp
void Server::processCompleteLines(ClientConnection& client)
{
    std::string::size_type end;

    while ((end = client.inputBuffer.find("\r\n"))
           != std::string::npos)
    {
        std::string line = client.inputBuffer.substr(0, end);
        client.inputBuffer.erase(0, end + 2);

        if (line.size() + 2 > 512)
        {
            disconnect(client, "IRC line too long");
            return;
        }

        processOneLine(client, line);
    }
}
```

### Scriere robusta

```cpp
void Server::handleWritable(ClientConnection& client)
{
    while (!client.outputBuffer.empty())
    {
        ssize_t count = client.socket.send(
            client.outputBuffer.data(),
            client.outputBuffer.size());

        if (count > 0)
        {
            client.outputBuffer.erase(0, count);
            continue;
        }

        if (count < 0 &&
            (errno == EAGAIN || errno == EWOULDBLOCK))
            return;

        disconnect(client, "send failed");
        return;
    }

    disablePolloutFor(client.fd);
}
```

`POLLOUT` trebuie activat doar cand `outputBuffer` nu este gol. Cand coada s-a golit, trebuie dezactivat.

### Ce mai trebuie adaugat pentru robustete

- verificarea rezultatului fiecarui `socket()`, `setsockopt()`, `fcntl()`, `bind()`, `listen()`, `accept()`, `recv()` si `send()`;
- tratarea `EINTR`;
- `SO_REUSEADDR` verificat, nu ignorat;
- `MSG_NOSIGNAL` sau `SIGPIPE` gestionat la trimitere;
- limite pentru bufferul de intrare si iesire;
- timeout-uri si politici pentru clienti lenti;
- inchidere idempotenta, ca aceeasi conexiune sa nu fie eliminata de doua ori;
- procesarea `POLLERR`, `POLLHUP`, `POLLNVAL`;
- normalizarea comenzilor si raspunsuri numerice IRC;
- parser care proceseaza exact o linie completa;
- teste unitare, teste de integrare si teste end-to-end;
- ownership explicit pentru `User` si socket;
- loguri cu fd, nickname, eroare si cauza inchiderii.

---

# Concluzie

Serverul integrat are deja doua elemente foarte importante pentru retele reale:

1. socket-uri non-blocking;
2. buffer per conexiune pentru framing si buffer de iesire pentru partial writes.

Serverul modular are o separare arhitecturala mai buna:

```text
Server -> TCPSocket -> Lexer -> ASTParser -> CommandDispatcher -> handler
```

Dar in forma actuala ii lipseste stratul de transport robust dintre `recv()` si lexer. Directia cea mai solida pentru versiunea finala este:

- pastrarea `ISocket`/`TCPSocket` si a separarii pe componente;
- adaugarea unui obiect de conexiune cu input/output buffers;
- implementarea framing-ului `\r\n`;
- folosirea rezultatelor complete ale `poll()`;
- mutarea regulilor globale in `Server` sau in servicii de protocol;
- testarea separata a transportului, parserului, dispatcher-ului si regulilor IRC.

