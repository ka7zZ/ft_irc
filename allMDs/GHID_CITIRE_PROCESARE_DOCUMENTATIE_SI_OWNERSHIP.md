# Ghid practic: citirea si procesarea documentatiei tehnice

## Scopul ghidului

Acest ghid descrie un proces repetabil pentru a transforma documentatia tehnica in:

```text
contracte clare
    -> intelegerea codului existent
    -> identificarea presupunerilor gresite
    -> design corect
    -> implementare
    -> teste
```

Metoda este utila in special pentru:

- API-uri POSIX;
- socket-uri si TCP;
- managementul memoriei;
- ownership;
- fisiere si descriptor-i;
- mutex-uri si thread-uri;
- parsere si protocoale;
- cod C si C++.

---

# 1. Formula generala

Procesul recomandat este:

```text
Documentatie
    ↓
Contractul API/protocolului
    ↓
Ipoteze si invariabile
    ↓
Mapare pe codul existent
    ↓
Cazuri normale + cazuri-limita
    ↓
Implementare minima
    ↓
Testare si observare
    ↓
Corectare/refactorizare
```

Intr-o forma mai practica:

```text
Citesc
    → extrag contractul
    → localizez codul
    → simulez executia
    → identific diferentele
    → implementez
    → testez
    → verific efectele secundare
```

Nu exista o metoda garantata ca fiind “cea mai buna statistic” pentru orice problema. Aceasta este insa o metoda robusta, generalizabila si verificabila, deoarece combina:

- contracte;
- cazuri limita;
- invariabile;
- ownership;
- contraexemple;
- testare.

---

# 2. Citeste documentatia ca pe un contract

Un incepator citeste:

> `recv()` primeste date de la socket.

Un programator experimentat intreaba:

- Ce returneaza la succes?
- Poate returna mai putini bytes decat am cerut?
- Ce inseamna `0`?
- Ce inseamna `-1`?
- Ce valori poate avea `errno`?
- Ce se intampla pe socket blocking?
- Ce se intampla pe socket non-blocking?
- Functia pastreaza granita mesajelor?
- Ce trebuie sa fac eu dupa apel?

Din documentatie trebuie extras un contract de forma:

```text
Intrare:
    fd valid
    buffer valid
    dimensiune pozitiva

Succes:
    returneaza numarul de bytes cititi

EOF:
    returneaza 0

Esec temporar:
    returneaza -1
    errno = EAGAIN sau EWOULDBLOCK

Esec definitiv:
    returneaza -1
    errno = alta valoare

Responsabilitatea apelantului:
    proceseaza doar numarul de bytes returnat
    pastreaza datele incomplete
    trateaza separat EOF si erorile
```

Transformarea esentiala este:

```text
propozitie din documentatie
    ↓
tabel de rezultate posibile
    ↓
ramuri de cod
```

---

# 3. Citeste documentatia in trei treceri

## Trecerea 1: modelul conceptual

Intreaba:

```text
Ce problema rezolva aceasta functie?
```

Pentru `poll()`:

```text
Nu citeste date.
Nu accepta clienti.
Doar asteapta evenimente pe mai multi descriptori.
```

Pentru `recv()`:

```text
Copiaza bytes disponibili din kernel in bufferul aplicatiei.
Nu reconstruieste automat mesajele aplicatiei.
```

Pentru TCP:

```text
Ofera un flux de bytes, nu mesaje discrete.
```

## Trecerea 2: contractul exact

Noteaza:

- parametrii;
- valoarea de retur;
- erorile;
- comportamentul blocking/non-blocking;
- ownership-ul;
- limitele;
- efectele secundare.

## Trecerea 3: implicatiile pentru cod

Intreaba:

```text
Ce trebuie sa faca codul meu pentru a respecta contractul?
```

Exemple:

```text
recv() poate returna date partiale
    ↓
am nevoie de inputBuffer
```

```text
send() poate trimite partial
    ↓
am nevoie de outputBuffer
```

```text
poll() poate raporta HUP sau ERR
    ↓
nu verific doar POLLIN
```

---

# 4. Transforma afirmatiile in tabele de comportament

Aceasta este una dintre cele mai eficiente tehnici pentru cod de retea.

## Tabel pentru `recv()`

| Rezultat | Interpretare | Actiune |
|---|---|---|
| `> 0` | Au sosit bytes | Adauga bytes in buffer |
| `0` | Peer-ul a inchis conexiunea | Inchide clientul curat |
| `-1 + EAGAIN` | Nu exista date momentan | Revino in `poll()` |
| `-1 + EINTR` | Apel intrerupt de semnal | Reincearca |
| `-1 + alt errno` | Eroare reala | Logheaza si inchide/trateaza |

## Tabel pentru `send()`

| Rezultat | Interpretare | Actiune |
|---|---|---|
| `> 0`, mai mic decat lungimea | Scriere partiala | Sterge doar prefixul trimis |
| `-1 + EAGAIN` | Kernelul nu accepta acum date | Pastreaza coada |
| `-1 + EPIPE/ECONNRESET` | Peer-ul a disparut | Inchide conexiunea |
| `0` | Nu s-a facut progres | Nu presupune succes |

Aceasta tehnica previne codul de forma:

```cpp
if (recv(...) < 0)
    close(fd);
```

care confunda o situatie temporara cu o eroare definitiva.

---

# 5. Gaseste codul care apeleaza API-ul

Dupa ce ai inteles documentatia, cauta toate locurile in care functia apare.

Pentru `recv()`:

```text
cine apeleaza recv()?
cine detine bufferul?
cine decide ca mesajul este complet?
cine trateaza EOF?
cine inchide socket-ul?
```

In `another_demo`, fluxul este:

```cpp
receive(buffer, size);
Lexer lexer(rawData);
ASTParser parser(tokens);
dispatcher.dispatch(...);
```

Comparatia corecta este:

```text
Documentatia:
    recv() poate returna o parte din flux

Codul:
    parseaza imediat rezultatul unui singur recv()

Diferenta:
    lipseste framing-ul si inputBuffer-ul per conexiune
```

Nu spune doar:

> “Codul nu este robust.”

Spune:

```text
Contractul permite X.
Codul presupune Y.
Prin urmare cazul Z este tratat gresit.
```

---

# 6. Urmareste fluxul de date

Pentru cod de retea, urmareste datele de la intrare pana la iesire:

```text
client
  ↓ bytes TCP
recv()
  ↓ bytes partiali
inputBuffer
  ↓ linie completa delimitata cu CRLF
Lexer
  ↓ Token[]
ASTParser
  ↓ MessageAST
CommandDispatcher
  ↓ handler
User / Server state
  ↓ mesaj de raspuns
outputBuffer
  ↓ POLLOUT
send()
  ↓ TCP
client
```

Pentru fiecare sageata intreaba:

- Ce tip de date circula?
- Cine detine datele?
- Cine le poate modifica?
- Ce se intampla daca datele sunt incomplete?
- Ce se intampla daca apar prea multe date?
- Ce se intampla daca destinatarul dispare?

Aceasta diagrama este adesea mai utila decat memorarea ierarhiei claselor.

---

# 7. Identifica invariabilele

O invarianta este o regula care trebuie sa ramana adevarata permanent.

Pentru un server IRC:

```text
1. Fiecare client activ din _pollFds exista si in _users.
2. Fiecare fd apare cel mult o data in _pollFds.
3. Un nickname nu poate apartine simultan la doi utilizatori.
4. Datele din inputBuffer nu se pierd pana la delimitatorul CRLF.
5. Datele eliminate din outputBuffer au fost acceptate de send().
6. Un descriptor inchis nu mai este folosit.
7. Parserul primeste exact o linie IRC completa.
8. Un client deconectat nu mai poate primi mesaje.
```

Apoi verifici codul in jurul acestor reguli.

Exemplu:

```cpp
socket->outputBuffer.erase(0, count);
```

Intrebarea corecta:

```text
Este garantat ca `count` este numarul exact de bytes acceptati?
```

Daca da, operatia pastreaza invarianta.

---

# 8. Trucul fundamental pentru ownership

Pentru orice resursa — socket, memorie, mutex, fisier, buffer — intreaba:

```text
Cine o creeaza?
Cine o foloseste?
Cine o modifica?
Cine o inchide sau distruge?
Cine raspunde daca operatia esueaza?
```

Exemplu:

```cpp
TCPSocket* socket = new TCPSocket(clientFd);
User* user = new User(clientFd, socket);
_users[clientFd] = user;
```

Intrebarile corecte sunt:

```text
Daca sterg User, cine sterge socket-ul?
Daca sterg socket-ul, cine mai poate folosi ISocket*?
Daca apare o eroare dupa accept(), cine inchide fd?
```

---

# 9. Creatorul nu este intotdeauna owner-ul

Nu presupune:

```text
cine creeaza obiectul il si detine
```

Ownership-ul este dat in primul rand de **responsabilitatea de distrugere**, nu doar de locul unde apare `new`.

Modele posibile:

```text
Model A:
User detine socket-ul si il distruge in ~User()

Model B:
Server detine User si socket separat

Model C:
TCPSocket este membru direct in ClientConnection

Model D:
nimeni nu este clar owner
```

Trick mental:

> Cauta `delete`, `close`, destructorii si `erase`, nu doar `new`.

Comanda de cautare:

```bash
rg "new |delete |close\\(|~[A-Za-z_]+|erase\\(" .
```

Daca gasesti:

```cpp
delete it->second;
```

intreaba:

```text
Ce distruge destructorul?
Ce pointeri raman invalizi dupa acest delete?
```

---

# 10. Testul obiectului abandonat

Imagineaza-ti ca owner-ul dispare:

```cpp
delete user;
```

Apoi intreaba:

```text
Ce obiecte mai exista?
Ce pointeri indica spre user?
Ce cod poate incerca sa-l foloseasca?
```

Daca ai:

```cpp
class User
{
    ISocket* _socket;
};
```

dupa:

```cpp
delete user;
```

trebuie sa stii:

- daca `User` dispare;
- daca `_socket` ramane;
- daca `_socket` este detinut sau doar imprumutat;
- ce pointeri catre `User` devin dangling;
- ce structuri trebuie curatate.

Regula:

```text
Dupa distrugerea owner-ului, toate aliasurile catre resursa trebuie
sterse, invalidate sau sa nu mai fie folosite.
```

---

# 11. Deosebeste ownership-ul de acces

Un pointer nu inseamna automat ownership.

Un pointer poate fi:

- **owner** — responsabil de distrugere;
- **borrowed/reference** — doar il foloseste;
- **observer** — il poate consulta, dar nu il tine in viata;
- **shared owner** — ownership distribuit;
- **handle** — identifica resursa, dar nu o detine.

Trick mental:

Inlocuieste temporar numele generic:

```cpp
ISocket* _socket;
```

cu variante explicite:

```cpp
ISocket* ownedSocket;
ISocket* borrowedSocket;
ISocket* observerSocket;
```

Daca nu poti decide care varianta este corecta, designul este ambiguu.

---

# 12. Urmareste verbele din documentatie

Verbele indica deseori responsabilitatea.

## Verbe care sugereaza transfer de ownership

- `create`;
- `allocate`;
- `open`;
- `acquire`;
- `take ownership`;
- `adopt`;
- `release`;
- `destroy`;
- `close`.

## Verbe care sugereaza imprumut

- `use`;
- `read`;
- `inspect`;
- `access`;
- `borrow`;
- `reference`;
- `pass`;
- `view`.

Exemplu:

```text
accept() creates a new connected socket
```

Interpretare:

```text
accept() produce un fd nou, iar aplicatia devine responsabila
sa il gestioneze si sa il inchida.
```

Nu inseamna ca `listenerFd` devine owner al clientului.

---

# 13. Testul „cine plateste?”

Pentru orice resursa intreaba:

```text
Cine plateste costul daca resursa ramane deschisa?
```

Exemple:

- fd deschis prea mult → procesul consuma descriptori;
- memorie neeliberata → procesul consuma RAM;
- buffer de iesire nelimitat → clientul lent consuma memorie;
- mutex blocat → alte thread-uri raman blocate;
- conexiune abandonata → serverul pastreaza stare inutila.

Cine suporta consecinta trebuie, de regula, sa aiba si responsabilitatea controlului.

---

# 14. Trucul pentru buffer: cine pastreaza lucrurile incomplete?

Pentru orice API de retea intreaba:

```text
Operatia poate produce date incomplete?
Daca da, cine pastreaza restul?
```

Pentru TCP:

```text
recv() poate returna jumatate de comanda
```

Deci trebuie sa existe:

```cpp
std::string inputBuffer;
```

Intrebarea de ownership devine:

```text
Al cui este inputBuffer-ul?
```

Raspunsul corect:

```text
Al conexiunii, pentru ca fragmentarea apartine fluxului acelei conexiuni.
```

Nu il pui, de regula, in:

- `Server` global — ar amesteca clientii;
- `Lexer` — lexer-ul nu detine conexiunea;
- `User` — posibil, dar doar daca `User` reprezinta complet conexiunea;
- `ClientConnection` — de obicei cea mai clara alegere.

Regula:

```text
Starea care poate diferi intre doua conexiuni trebuie pastrata per conexiune.
```

---

# 15. Trucul celor doua conexiuni simultane

Imagineaza-ti:

```text
Client A trimite jumatate de comanda
Client B trimite o comanda completa
```

Intreaba:

```text
Daca folosesc o singura variabila globala, se amesteca datele?
```

Daca raspunsul este da, starea trebuie mutata in:

```cpp
ClientConnection
```

fiecare conexiune avand:

```cpp
inputBuffer
outputBuffer
registrationState
user
```

Exemplu gresit:

```cpp
std::string globalInputBuffer;
```

Exemplu corect:

```cpp
std::map<int, ClientConnection> clients;
```

---

# 16. Pentru fiecare pointer, scrie propozitia completa

Ia declaratia:

```cpp
ISocket* _socket;
```

Completeaza:

```text
`_socket` este un pointer catre ______,
detinut de ______,
folosit de ______,
valid cat timp ______,
distrus de ______.
```

Exemplu:

```text
`_socket` este un pointer catre socket-ul TCP,
detinut de Server,
folosit de User,
valid cat timp conexiunea este activa,
inchis de TCPSocket.
```

Daca nu poti completa propozitia, ownership-ul nu este clar.

---

# 17. Pentru fiecare functie, intreaba „ce se schimba?”

Nu te uita doar la valoarea returnata. Urmareste efectele secundare.

Pentru:

```cpp
ssize_t recv(int fd, void* buffer, size_t size, int flags);
```

intreaba:

```text
Ce se schimba?
```

Posibile raspunsuri:

- bufferul aplicatiei este modificat;
- pozitia interna a fluxului socket-ului avanseaza;
- datele sunt consumate din bufferul kernelului;
- `errno` poate fi modificat;
- conexiunea poate ajunge la EOF.

Pentru:

```cpp
close(fd);
```

efectele sunt mai mari:

- fd-ul devine invalid;
- kernelul elibereaza resurse;
- structurile care pastreaza fd-ul trebuie actualizate;
- operatiile viitoare pe acel fd pot lovi alta resursa daca numarul este reutilizat.

---

# 18. Regula „valoare returnata + errno + stare”

Pentru orice functie de sisteme, citeste simultan:

```text
valoarea returnata
errno
starea resursei
```

Exemplu `recv()`:

| Return | `errno` | Starea conexiunii |
|---|---|---|
| `> 0` | irelevant | Date primite |
| `0` | irelevant | Peer-ul a inchis ordonat |
| `-1` | `EAGAIN` | Nu exista progres acum |
| `-1` | `EINTR` | Apel intrerupt, posibil retry |
| `-1` | altceva | Eroare reala |

Greseala comuna:

```cpp
if (result < 0)
    disconnect();
```

Forma corecta:

```cpp
if (result > 0)
    process(result);
else if (result == 0)
    disconnect();
else if (errno == EAGAIN || errno == EWOULDBLOCK)
    waitForNextEvent();
else if (errno == EINTR)
    retry();
else
    failConnection();
```

---

# 19. Clasifica erorile in patru categorii

## 1. Succes

Operatia a facut progres:

```text
recv() > 0
send() > 0
accept() >= 0
```

## 2. EOF/deconectare

Peer-ul a inchis conexiunea:

```text
recv() == 0
```

## 3. Temporar/retry

Nu este o defectiune permanenta:

```text
EAGAIN
EWOULDBLOCK
EINTR
```

## 4. Fatal sau necesita politica

Necesita log, cleanup sau decizie:

```text
EBADF
ECONNRESET
EMFILE
ENFILE
ENOMEM
```

Trick mental:

> Daca repet imediat operatia, ar trebui sa ma astept la succes?

Daca da, eroarea este probabil temporara. Daca nu, trebuie investigata sau conexiunea trebuie inchisa.

---

# 20. Citeste si ce NU garanteaza API-ul

Documentatia este la fel de valoroasa prin ceea ce nu garanteaza.

`recv()` garanteaza:

```text
Iti returneaza bytes disponibili.
```

Nu garanteaza:

```text
Iti returneaza o comanda completa.
```

`send()` garanteaza:

```text
Incearca sa accepte date pentru transmitere.
```

Nu garanteaza:

```text
Peer-ul a primit deja datele.
```

`poll()` garanteaza:

```text
Iti indica descriptorii cu activitate.
```

Nu garanteaza:

```text
Operatia urmatoare va reusi sigur.
```

Regula:

```text
Orice lucru pe care API-ul nu il garanteaza trebuie rezolvat in codul tau.
```

---

# 21. Trucul „ce se intampla daca operatia este intrerupta aici?”

Pentru fiecare functie importanta, opreste imaginar executia in puncte intermediare.

Exemplu:

```cpp
int fd = accept(...);
User* user = new User(...);
_users[fd] = user;
```

Intreaba:

```text
Ce se intampla daca new User esueaza?
Ce se intampla daca inserarea in mapa esueaza?
Cine inchide fd?
```

Alt exemplu:

```cpp
send(fd, buffer, size, 0);
```

Intreaba:

```text
Ce se intampla daca trimite doar jumatate?
Ce se intampla daca peer-ul dispare?
Ce se intampla daca procesul primeste SIGPIPE?
```

Aceasta tehnica este foarte puternica pentru resurse si cleanup.

---

# 22. Diagrama de viata a unui client IRC

```text
CREAT
  ↓
accept()
  ↓
socket non-blocking
  ↓
User/ClientConnection creat
  ↓
adaugat in poll
  ↓
primeste date
  ↓
inregistrare
  ↓
trimite mesaje
  ↓
EOF / eroare / QUIT
  ↓
sters din poll
  ↓
inchis socket
  ↓
sters din registry
```

Intrebari de verificare:

```text
Exista vreun salt care omite cleanup?
Exista un obiect folosit dupa momentul distrugerii?
Exista un fd in poll dupa close()?
Exista un User in map fara socket valid?
```

---

# 23. Identifica rolul clasei inainte de ownership

Intreaba:

```text
Clasa reprezinta:
- o resursa?
- o conexiune?
- identitatea unui utilizator?
- un mesaj?
- un serviciu?
- un registry?
```

Exemple:

```text
TCPSocket        -> resursa OS
User             -> identitate/protocol
ClientConnection -> stare de transport + protocol
Server           -> orchestrator si owner al conexiunilor
Lexer            -> transformare, fara ownership permanent
MessageAST       -> model temporar al unui mesaj
```

Apoi ownership-ul devine mai clar:

```text
Server detine conexiunile active.
ClientConnection detine buffer-ele si socket-ul.
User poate fi parte din ClientConnection.
Lexer nu ar trebui sa detina socket-ul.
```

---

# 24. Semnale de design periculos

Fii atent la aceste combinatii:

```cpp
new T
T*
delete T*
```

fara reguli clare;

```cpp
T*
```

pastrat in mai multe containere;

```cpp
close(fd)
```

in mai multe clase;

```cpp
User* user;
ISocket* socket;
```

dar fara destructor clar;

```cpp
std::string buffer;
```

intr-un obiect global pentru date care apartin clientilor;

```cpp
recv()
```

urmat direct de parser, fara framing;

```cpp
send()
```

fara output buffer;

```cpp
poll()
```

dar se verifica doar `POLLIN`.

Acestea nu demonstreaza automat un bug, dar sunt locuri unde trebuie investigat ownership-ul si starea.

---

# 25. Metoda invarianta + contraexemplu

Pentru fiecare componenta formuleaza:

```text
Ce ar trebui sa fie mereu adevarat?
Cum pot construi un caz care sparge aceasta regula?
```

Exemplu:

```text
Invarianta:
    parserul primeste o singura linie IRC completa.

Contraexemplu:
    recv() primeste jumatate de linie.

Concluzie:
    lipseste inputBuffer-ul.
```

Alt exemplu:

```text
Invarianta:
    nickname-ul este unic.

Contraexemplu:
    doi clienti trimit NICK alex.

Concluzie:
    handler-ul trebuie sa consulte Server, nu doar User.
```

---

# 26. Separarea bug-ului de limitarea arhitecturala

Nu toate problemele sunt de acelasi tip.

## Bug punctual

```cpp
if (errno == EAGAIN)
    return;
```

dar codul uita `EWOULDBLOCK`.

Soluția poate fi mica:

```cpp
if (errno == EAGAIN || errno == EWOULDBLOCK)
    return;
```

## Limitare arhitecturala

```text
recv()
    -> Lexer direct
```

fara buffer per client.

Aici nu este suficient un `if`. Trebuie introdus un concept:

```text
ClientConnection::inputBuffer
```

## Problema de model

```cpp
User* user;
ISocket* socket;
```

dar nu este clar cine detine socket-ul.

Aici trebuie clarificat ownership-ul inainte de modificarea functiilor individuale.

Aceasta clasificare evita tratarea unei probleme structurale cu un patch superficial.

---

# 27. Implementeaza schimbarea minima care restabileste contractul

Ordinea buna este:

```text
1. Repara contractul incalcat.
2. Pastreaza comportamentul existent.
3. Adauga validarea necesara.
4. Testeaza cazul care a esuat.
5. Abia apoi refactorizeaza.
```

Pentru bufferul TCP din `another_demo`:

```cpp
struct ClientConnection
{
    int fd;
    ISocket* socket;
    std::string inputBuffer;
    std::string outputBuffer;
};
```

Citirea:

```cpp
client.inputBuffer.append(buffer, bytesRead);

std::string::size_type end;
while ((end = client.inputBuffer.find("\r\n"))
       != std::string::npos)
{
    std::string line = client.inputBuffer.substr(0, end);
    client.inputBuffer.erase(0, end + 2);

    processLine(client, line);
}
```

Important:

```text
Lexer-ul nu trebuie sa primeasca fragmente arbitrare de TCP.
El trebuie sa primeasca o unitate completa de protocol.
```

```text
TCP chunk != IRC message
```

---

# 28. Foloseste testele pentru intelegere, nu doar pentru verificare

Testele sunt un mod de a intelege contractul.

## Test pentru fragmentare

```text
"NICK al"
"ex\r\n"
```

Rezultat asteptat:

```text
un singur mesaj NICK alex
```

## Test pentru agregare

```text
"NICK alex\r\nUSER alex 0 * :Alex\r\n"
```

Rezultat asteptat:

```text
doua mesaje procesate separat
```

## Test pentru partial write

```text
send() returneaza 4 din 10 bytes
```

Rezultat asteptat:

```text
outputBuffer pastreaza ultimii 6 bytes
```

## Test pentru `TRAILING`

```text
PRIVMSG alex :Salut tuturor
```

Rezultat asteptat:

```text
params[0] = "alex"
params[1] = "Salut tuturor"
params[1].isTrailing == true
```

---

# 29. Verifica sursa in trei directii

Cand citesti documentatie, verifica afirmatia prin:

## A. Documentatie

Ce spune contractul oficial?

## B. Implementarea locala

Codul respecta contractul?

## C. Experiment

Poti demonstra comportamentul?

Exemplu:

```text
Documentatie:
    recv() poate returna date partiale.

Cod:
    parserul primeste direct un singur recv().

Experiment:
    trimite o comanda in doua send-uri.

Concluzie:
    daca serverul esueaza, lipseste framing-ul.
```

---

# 30. Nu confunda documentatia cu o solutie gata de copiat

Documentatia spune:

```text
recv() poate returna EAGAIN.
```

Nu spune automat:

```text
pune un while aici.
```

Solutia se deduce din arhitectura:

```text
EAGAIN inseamna ca nu exista progres acum
    ↓
nu inchid clientul
    ↓
pastrez starea deja acumulata
    ↓
ma intorc in poll()
```

La fel:

```text
TCP este stream
    ↓
nu exista delimitare naturala pentru IRC
    ↓
IRC foloseste CRLF
    ↓
am nevoie de inputBuffer + extragere pe CRLF
```

---

# 31. Cum gandeste un programator bun de sisteme

Nu inseamna sa scrie cod complicat. Inseamna precizie in urmatoarele domenii:

## Contracte

```text
Ce promite sistemul?
Ce nu promite?
```

## Ownership

```text
Cine creeaza obiectul?
Cine il detine?
Cine il distruge?
```

## Stare

```text
Ce trebuie pastrat intre doua apeluri?
```

## Erori

```text
Care erori sunt temporare?
Care sunt definitive?
Care sunt recuperabile?
```

## Invariabile

```text
Ce trebuie sa fie mereu adevarat?
```

## Observabilitate

```text
Cum stiu ca implementarea se comporta corect?
```

## Simplitate

```text
Care este cea mai simpla implementare care respecta contractul complet?
```

Programarea buna de sisteme inseamna contracte clare, cod direct, verificarea presupunerilor, atentie la cazurile-limita si evitarea abstractiilor care ascund comportamente reale.

---

# 32. Formular de analiza pentru orice API

Completeaza acest formular cand citesti o functie de sistem:

```text
Numele API-ului:
Scop:
Parametri:
Valoare de retur:
Rezultat de succes:
EOF:
Erori temporare:
Erori definitive:
Poate face operatie partiala?
Poate bloca?
Cine detine resursa?
Cine face cleanup?
Ce stare trebuie pastrata?
Ce nu garanteaza API-ul?
Ce cod local il apeleaza?
Ce presupune codul local?
Care este contraexemplul?
Care este invarianta?
Cum testez?
```

Exemplu pentru `recv()`:

```text
Numele API-ului:
    recv()

Scop:
    copiaza bytes disponibili din socket in buffer

Succes:
    returneaza numarul de bytes primiti

EOF:
    returneaza 0

Temporar:
    EAGAIN/EWOULDBLOCK

Partial:
    da

Mesaj complet garantat:
    nu

Stare necesara:
    inputBuffer per conexiune

Invarianta:
    nu pierd bytes pana cand nu extrag o linie completa

Test:
    trimit o comanda in doua bucati
```

---

# 33. Rezumatul esential

Cele mai importante intrebari mentale sunt:

```text
Cine creeaza?
Cine detine?
Cine distruge?
Ce se intampla daca operatia esueaza?
Ce se intampla daca operatia reuseste doar partial?
Ce stare trebuie pastrata?
Ce nu garanteaza API-ul?
Ce invarianta trebuie sa fie mereu adevarata?
Cum pot construi un contraexemplu?
Cum demonstrez printr-un test?
```

Formula finala:

```text
documentatie
    → contract
    → rezultate posibile
    → ownership
    → stare persistenta
    → invariabile
    → contraexemple
    → implementare
    → teste
```

Daca repeti procedura pentru fiecare API de sistem, incepi sa citesti documentatia nu ca pe o descriere abstracta, ci ca pe un set de obligatii concrete pe care codul tau trebuie sa le respecte.

