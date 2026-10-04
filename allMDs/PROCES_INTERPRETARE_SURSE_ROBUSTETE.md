# Procesul de interpretare a surselor si derivarea recomandarilor

## Nota despre format

Documentul nu reproduce rationament intern privat sau un jurnal complet al gandirii. Ofera in schimb o **trasabilitate tehnica verificabila**:

```text
sursa/context observabil
    -> interpretare tehnica
    -> codul relevant din proiect
    -> buna practica derivata
```

Astfel, fiecare recomandare poate fi verificata independent in documentatia si fisierele indicate.

---

## 1. Multiplexarea cu `poll()`

### Contextul citit

In documentatia Linux pentru [`poll(2)`](https://man7.org/linux/man-pages/man2/poll.2.html), `poll()` este descris ca o functie care asteapta evenimente pe mai multi file descriptor-i. Structura `pollfd` contine descriptorul, evenimentele cerute si evenimentele produse.

### Sintetizarea contextului

Am interpretat acest context astfel:

```text
poll() nu citeste mesajele si nu accepta singur clientii.
poll() spune programului pe ce descriptor merita sa incerce o operatie fara blocare.
```

### Corespondenta in cod

In [serverul integrat](demo_sources/srcs/Server.cpp), lista este construita astfel:

```cpp
pollfd listener;
listener.fd = listenerFd;
listener.events = POLLIN;
listener.revents = 0;
descriptors.push_back(listener);
```

Iar asteptarea are loc aici:

```cpp
if (poll(&descriptors[0], descriptors.size(), -1) < 0)
{
    if (errno == EINTR)
        continue;
    throw std::runtime_error("poll failed");
}
```

### Buna practica derivata

Am aplicat urmatoarea regula:

```text
un server single-threaded trebuie sa astepte simultan pe listener si pe toti clientii,
nu sa execute recv() blocant pentru un singur client.
```

De aici rezulta explicatia despre multiplexare si observatia ca `poll()` este suficient pentru demo, dar `epoll`/`kqueue` pot fi mai potrivite la volume mari.

---

## 2. `POLLIN` pe socket-ul de ascultare

### Contextul citit

Documentatia [`accept(2)`](https://man7.org/linux/man-pages/man2/accept.2.html) spune ca `accept()` extrage prima conexiune din coada de conexiuni in asteptare si ca un eveniment de citire poate indica o conexiune noua pe listener.

### Sintetizarea contextului

Interpretarea a fost:

```text
POLLIN pe listener nu inseamna ca exista text IRC.
Inseamna ca accept() are probabil o conexiune disponibila.
```

### Corespondenta in cod

```cpp
if (descriptors[0].revents & POLLIN)
    acceptClients();
```

si:

```cpp
int fd = accept(listenerFd, NULL, NULL);
if (fd < 0)
    ...
sockets[fd] = new Socket(fd);
```

### Buna practica derivata

Am separat conceptual cele doua roluri:

```text
listenerFd -> accepta conexiuni
clientFd   -> primeste si trimite octeti
```

Aceasta separare evita interpretarea gresita conform careia listener-ul ar trebui tratat ca un client IRC obisnuit.

---

## 3. `POLLIN` si `POLLOUT`

### Contextul citit

Documentatia [`poll(2)`](https://man7.org/linux/man-pages/man2/poll.2.html) descrie `POLLIN` ca disponibilitate pentru citire si `POLLOUT` ca disponibilitate pentru scriere.

Documentatia [`send(2)`](https://man7.org/linux/man-pages/man2/send.2.html) explica faptul ca transmiterea poate astepta cand bufferul kernelului este plin.

### Sintetizarea contextului

```text
POLLIN: exista o sansa rezonabila ca recv() sa faca progres.
POLLOUT: exista o sansa rezonabila ca send() sa accepte date.
Niciunul nu este o garantie ca operatia va procesa tot mesajul.
```

### Corespondenta in cod

```cpp
descriptor.events = POLLIN;
if (!it->second->outputBuffer.empty())
    descriptor.events |= POLLOUT;
```

```cpp
if (events & POLLIN)
    readFrom(sockets[fd]);

if (events & POLLOUT)
    writeTo(sockets[fd]);
```

### Buna practica derivata

Am derivat doua reguli:

1. `POLLOUT` se activeaza numai cand exista date de trimis;
2. dupa golirea cozii de iesire, `POLLOUT` trebuie dezactivat.

Motivul este evitarea trezirilor permanente si a consumului inutil de CPU.

---

## 4. Transmiterea partiala si output buffer

### Contextul citit

In [`send(2)`](https://man7.org/linux/man-pages/man2/send.2.html), documentatia precizeaza ca `send()` poate bloca daca bufferul de trimitere este plin, iar pe non-blocking poate intoarce `EAGAIN`/`EWOULDBLOCK`.

### Sintetizarea contextului

```text
send(fd, data, n) nu trebuie interpretat ca “n bytes au ajuns”.
Valoarea pozitiva returnata este numarul de bytes acceptati acum de kernel.
```

### Corespondenta in cod

Serverul pune mesajul in coada:

```cpp
void Server::send(Socket *socket, const std::string &message)
{
    socket->outputBuffer += message;
}
```

Consumul cozii:

```cpp
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
```

### Buna practica derivata

Am modelat mental operatia ca pe o coada:

```text
coada = [mesaj complet]
send() accepta partial -> sterg doar prefixul acceptat
urmatorul POLLOUT -> continui cu restul
```

De aici recomandarea pentru limita bufferului de iesire, politici pentru clienti lenti si backpressure.

---

## 5. Socket blocking versus non-blocking

### Contextul citit

Documentatia [`recv(2)`](https://man7.org/linux/man-pages/man2/recv.2.html) spune ca:

- pe socket blocking, apelul poate astepta date;
- pe socket non-blocking, lipsa datelor produce `-1` cu `EAGAIN`/`EWOULDBLOCK`.

Documentatia [`accept(2)`](https://man7.org/linux/man-pages/man2/accept.2.html) descrie acelasi principiu pentru coada listener-ului.

### Sintetizarea contextului

```text
blocking este simplu, dar poate bloca bucla globala.
non-blocking cere tratarea explicita a “nu exista progres acum”.
```

### Corespondenta in cod

```cpp
int flags = fcntl(listenerFd, F_GETFL, 0);
fcntl(listenerFd, F_SETFL, flags | O_NONBLOCK);
```

si pentru client:

```cpp
int flags = fcntl(fd, F_GETFL, 0);
fcntl(fd, F_SETFL, flags | O_NONBLOCK);
```

### Buna practica derivata

Am legat non-blocking de multiplexare:

```text
poll() indica unde merita incercat
recv()/send() fac progres cat pot
EAGAIN inseamna “revino in poll()”
```

Nu am tratat non-blocking ca pe o optimizare universala. Este o alegere arhitecturala potrivita cand un thread deserveste multe conexiuni.

---

## 6. `EAGAIN` si `EWOULDBLOCK`

### Contextul citit

Atat [`recv(2)`](https://man7.org/linux/man-pages/man2/recv.2.html), cat si [`send(2)`](https://man7.org/linux/man-pages/man2/send.2.html) documenteaza `EAGAIN`/`EWOULDBLOCK` pentru operatii non-blocking care nu pot face progres imediat.

### Sintetizarea contextului

```text
EAGAIN nu inseamna automat “client defect”.
Inseamna ca operatia ar bloca daca am astepta.
```

### Corespondenta in cod

```cpp
if (count < 0)
{
    if (errno == EAGAIN || errno == EWOULDBLOCK)
        break;
    closeClient(socket->fd);
    return;
}
```

### Buna practica derivata

Am clasificat rezultatul unei operatii I/O in trei categorii:

```text
progres       -> procesez bytes
EOF           -> peer-ul a inchis conexiunea
EAGAIN        -> nu exista progres acum, revin la poll()
alta eroare   -> loghez si inchid sau tratez explicit
```

Aceasta clasificare sta la baza recomandarii de a nu ignora alte erori.

---

## 7. De ce nu trebuie ignorata eroarea `accept()`

### Contextul citit

[`accept(2)`](https://man7.org/linux/man-pages/man2/accept.2.html) precizeaza ca `-1` indica o eroare si ca `errno` identifica motivul. Documentatia mentioneaza inclusiv erori de retea raportate de Linux prin `accept()`.

### Sintetizarea contextului

Codul actual face:

```cpp
if (fd < 0)
{
    if (errno == EAGAIN || errno == EWOULDBLOCK)
        return;
    return;
}
```

Interpretarea este:

```text
EAGAIN este normal dupa golirea cozii.
Orice alta eroare ascunsa poate insemna o problema reala.
```

### Buna practica derivata

Am propus clasificare:

```cpp
if (errno == EINTR)
    continue;

if (errno == EAGAIN || errno == EWOULDBLOCK)
    return;

throw std::runtime_error("accept failed");
```

In practica, eroarea ar trebui sa includa `strerror(errno)` si, pentru erori operationale precum `EMFILE` sau `ENFILE`, serverul ar trebui sa aiba o politica explicita de limitare/backoff.

---

## 8. TCP este flux, nu mesaj

### Contextul citit

Documentatia `recv(2)` spune ca apelul poate returna datele disponibile pana la dimensiunea ceruta, nu neaparat intreaga cantitate ceruta.

### Sintetizarea contextului

```text
TCP nu pastreaza granitele apelurilor send() ale aplicatiei.
Aplicatia trebuie sa defineasca si sa reconstruiasca propriul framing.
```

### Corespondenta in codul integrat

```cpp
socket->inputBuffer.append(buffer, count);

while ((end = socket->inputBuffer.find("\r\n")) != std::string::npos)
{
    std::string line = socket->inputBuffer.substr(0, end);
    socket->inputBuffer.erase(0, end + 2);
    processLine(socket, line);
}
```

### Corespondenta in codul modular

```cpp
char buffer[1024];
ssize_t bytesRead = ...receive(buffer, sizeof(buffer) - 1);
std::string rawData(buffer);
Lexer lxr(rawData);
```

### Buna practica derivata

Am comparat cele doua forme:

```text
integrat: recv -> append -> extrag toate liniile complete -> parsez
modular:  recv -> parsez imediat tot chunk-ul
```

Din aceasta comparatie a rezultat recomandarea principala pentru modular:

```cpp
inputBuffer.append(buffer, count);
while (exista "\r\n")
    parseOneCompleteLine();
```

---

## 9. `TRAILING` in IRC

### Contextul citit

In [RFC 2812, sectiunea 2.3](https://datatracker.ietf.org/doc/html/rfc2812#section-2.3) si [sectiunea 2.3.1](https://datatracker.ietf.org/doc/html/rfc2812#section-2.3.1), mesajele IRC sunt structurate in prefix optional, comanda si parametri. Ultimul parametru poate fi de forma trailing, pentru a permite spatii.

### Sintetizarea contextului

```text
spatiul separa parametrii normali;
“:” incepe parametrul final, al carui rest este pastrat ca o singura valoare.
```

### Corespondenta in cod

```cpp
if (peek() == ':')
{
    advance();
    ...
    tk.type = TRAILING;
    tk.value = ...
    tokens.push_back(tk);
    break;
}
```

### Buna practica derivata

Am testat mental exemplul:

```text
PRIVMSG alex :Salut tuturor
```

si l-am transformat astfel:

```text
COMMAND("PRIVMSG")
PARAMETER("alex")
TRAILING("Salut tuturor")
```

Concluzia este ca trailing nu este un tip de mesaj separat, ci o regula de framing/parsing pentru ultimul argument.

---

## 10. `TRAILING` si `ParamNode`

### Contextul citit

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

### Sintetizarea contextului

```text
Lexer-ul descrie sintaxa.
AST-ul ofera handler-ului argumente uniforme.
isTrailing pastreaza informatia sintactica atunci cand aceasta este necesara.
```

### Buna practica derivata

Am pastrat distinctia in modelul mental:

```text
PARAMETER("alex")       -> ParamNode("alex", false)
TRAILING("Salut tuturor") -> ParamNode("Salut tuturor", true)
```

Astfel handler-ele pot folosi uniform `ast.params`, fara sa piarda complet originea token-ului.

---

## 11. De ce este utila interfata `ISocket`

### Contextul citit

In [ISocket.hpp](another_demo/headers/ISocket.hpp), operatiile sunt virtuale:

```cpp
virtual ssize_t send(const void* data, size_t size) = 0;
virtual ssize_t receive(void *buffer, size_t size) = 0;
```

In [TCPSocket.cpp](another_demo/srcs/TCPSocket.cpp), implementarea apeleaza POSIX:

```cpp
return ::send(_fd, data, size, 0);
return ::recv(_fd, buffer, size, 0);
```

### Sintetizarea contextului

```text
codul de protocol nu trebuie sa depinda de descriptorul POSIX concret.
```

### Buna practica derivata

Am aplicat principiul dependentei de abstractie:

```text
User/dispatcher -> ISocket
TCPSocket       -> POSIX send/recv
MockSocket      -> comportament controlat in teste
```

Rezultatul este testarea logicii fara port TCP real si fara timing nedeterminist.

---

## 12. Ce am dedus din parametrul `server` neutilizat

### Contextul citit

Semnatura comuna:

```cpp
typedef void (*CommandHandler)(Server&, User&, const MessageAST&);
```

Dar handler-ul `NICK` face doar:

```cpp
user.setNickname(newNick);
```

si nu foloseste `server`.

### Sintetizarea contextului

```text
schimbarea nickname-ului pare locala, desi unicitatea si indexarea sunt globale.
```

### Buna practica derivata

Am propus ca serverul sa detina regula globala:

```cpp
bool Server::tryChangeNickname(User& user,
                               const std::string& newNick,
                               std::string& error);
```

Handler-ul coordoneaza, iar serverul valideaza consistenta globala:

```text
parserul: ce a trimis clientul?
validatorul: este forma valida?
serverul: conflict cu alt client?
handler-ul: ce raspuns trimit?
```

Aceasta este o recomandare de design derivata din efectul comenzii asupra starii partajate, nu dintr-un mesaj de eroare al compilatorului luat izolat.

---

## 13. `POLLERR`, `POLLHUP`, `POLLNVAL`

### Contextul citit

Aceste evenimente sunt definite in documentatia [`poll(2)`](https://man7.org/linux/man-pages/man2/poll.2.html) ca stari diferite de simpla disponibilitate la citire:

- `POLLERR`: eroare;
- `POLLHUP`: hang-up;
- `POLLNVAL`: descriptor invalid.

### Sintetizarea contextului

```text
revents trebuie inspectat ca stare a descriptorului, nu doar ca “are date”.
```

### Corespondenta in codul integrat

```cpp
if (events & (POLLERR | POLLHUP | POLLNVAL))
{
    closeClient(fd);
    continue;
}
```

### Corespondenta in codul modular

Bucla modulara verifica explicit doar:

```cpp
if (_pollFds[i].revents & POLLIN)
```

### Buna practica derivata

Am recomandat:

```text
POLLNVAL -> bug/descriptor invalid, eliminare si investigare
POLLERR  -> log, SO_ERROR unde este relevant, inchidere
POLLHUP  -> citire date ramase, apoi inchidere cand recv() confirma EOF
POLLIN   -> procesare date
```

Ordinea trebuie adaptata politicii serverului, deoarece `POLLHUP` poate aparea impreuna cu date inca necitite.

---

## 14. Mock-uri si strategia de testare

### Contextul citit

`ISocket` permite o implementare alternativa, deoarece `send()` si `receive()` sunt metode virtuale pure.

### Sintetizarea contextului

```text
un mock poate controla intrarea si poate inregistra iesirea fara retea reala.
```

### Buna practica derivata

Am separat testarea in straturi:

```text
Lexer/Parser         -> teste unitare deterministe
Dispatcher/handlers  -> MockSocket si Server controlat
poll()/TCP           -> teste de integrare cu socket-uri reale
nc/end-to-end        -> verificare manuala si scenarii complete
```

Aceasta separare evita folosirea unui test end-to-end lent pentru fiecare eroare de parser si evita concluzia gresita ca un parser este corect doar pentru ca un client simplu s-a conectat.

---

## 15. Forma rezultata pentru robustete TCP

### Contextele combinate

Am combinat informatiile din:

- `recv(2)`: date partiale, `EAGAIN`, EOF;
- `send(2)`: partial write, buffer kernel plin;
- `poll(2)`: evenimente I/O;
- RFC 2812: delimitarea mesajelor IRC;
- implementarea integrata: buffer per socket;
- implementarea modulara: separarea parser/dispatcher.

### Sintetizarea contextului

```text
transportul trebuie sa reconstruiasca liniile IRC inainte ca parserul sa le interpreteze.
scrierea trebuie sa pastreze restul netrimis.
bucla trebuie sa proceseze toate evenimentele relevante.
```

### Buna practica derivata

Arhitectura recomandata:

```text
socket non-blocking
    -> poll/epoll
    -> ClientConnection
         inputBuffer
         outputBuffer
         User
    -> framing CRLF
    -> Lexer pentru o singura linie
    -> ASTParser
    -> validare
    -> CommandDispatcher
    -> raspuns in outputBuffer
```

Bucla de citire rezultata:

```cpp
client.inputBuffer.append(buffer, count);

while ((end = client.inputBuffer.find("\r\n"))
       != std::string::npos)
{
    std::string line = client.inputBuffer.substr(0, end);
    client.inputBuffer.erase(0, end + 2);
    processOneLine(client, line);
}
```

Bucla de scriere rezultata:

```cpp
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

    disconnect(client);
    return;
}
```

Aceasta este o **sinteza de design** obtinuta prin corelarea contractelor API cu limitarile observate in cod; nu este copiata dintr-o singura sursa.

---

## 16. Rezumatul metodei folosite

Pentru fiecare subiect am urmat acest sablon:

```text
1. am identificat afirmatia tehnica din documentatie;
2. am verificat ce contract are apelul POSIX/RFC;
3. am cautat implementarea corespondenta in repository;
4. am simulat cazurile limita relevante;
5. am comparat comportamentul actual cu acel contract;
6. am formulat o recomandare care pastreaza contractul si gestioneaza cazurile limita.
```

Exemplu compact:

```text
Documentatie:
    recv() poate returna mai putini bytes decat bufferul cerut.

Cod:
    Server::handleClientData() parseaza imediat un singur recv().

Interpretare:
    o comanda IRC poate fi fragmentata sau mai multe comenzi pot fi unite.

Buna practica:
    inputBuffer per client + extragere pe delimitatorul CRLF.
```

Acest proces este verificabil in:

- [CLARIFICARI_SERVER_IRC.md](CLARIFICARI_SERVER_IRC.md);
- [serverul integrat](demo_sources/srcs/Server.cpp);
- [serverul modular](another_demo/srcs/Server.cpp);
- documentatia `poll(2)`, `recv(2)`, `send(2)`, `accept(2)` si RFC 2812 indicate mai sus.

