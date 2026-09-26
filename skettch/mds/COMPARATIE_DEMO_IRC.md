# Comparatie intre cele doua demo-uri IRC

## 1. Scopul comparatiei

In folderul `ft_irc` exista doua directii de implementare:

- **Demo-ul integrat**: `demo_sources/` + `main.cpp`, compilat de `Makefile` ca `irc_demo`.
- **Demo-ul modular**: `another_demo/`, bazat pe lexer, parser AST si un dispatcher de comenzi.

Comparatia de mai jos se refera la codul existent in aceste directoare, nu la o versiune finala de server IRC. Am verificat si compilarea:

- Demo-ul integrat compileaza cu `-Wall -Wextra -Werror -std=c++98`.
- Demo-ul modular nu trece aceeasi compilare deoarece `CommandDispatcher.cpp` are parametri `server` neutilizati, tratati ca erori de `-Werror`.

## 2. Rezumat rapid

| Criteriu | Demo integrat (`demo_sources`) | Demo modular (`another_demo`) |
|---|---|---|
| Arhitectura | Server monolitic, dar complet functional pentru un subset IRC | Separare Lexer -> ASTParser -> CommandDispatcher |
| Multiplexare I/O | `poll()` cu lista refacuta la fiecare iteratie | `poll()` cu vector persistent de `pollfd` |
| Buffering TCP | Buffer per socket, suporta linii fragmentate si mai multe comenzi | Citeste un singur bloc si il parseaza direct |
| Protocol IRC | `NICK`, `USER`, `JOIN`, `PING`, raspunsuri numerice de baza | `NICK`, `USER`, `PING`, `PRIVMSG` partial |
| Canale | Prezente: membri, operator initial, lista NAMES | Lipsesc; `PRIVMSG` catre canal doar afiseaza un mesaj de debug |
| Validare | Nickname, canal, numar de parametri, stare de inregistrare | Validare minima; majoritatea comenzilor pot fi executate fara reguli IRC complete |
| Management memorie | Pointeri raw, dar ciclul principal este gestionat coerent | Pointeri raw; socket-ul este detinut indirect de `User`, cu riscuri la inchidere |
| Usurinta de extindere | Mai greu: logica de protocol este concentrata in `Server.cpp` | Mai buna pentru parsing si comenzi noi |
| Maturitate pentru demo functional | Mai ridicata | Mai buna ca schelet arhitectural, dar incompleta functional |

## 3. Varianta 1: demo-ul integrat

Fisierele relevante sunt [Server.hpp](demo_sources/headers/Server.hpp), [Server.cpp](demo_sources/srcs/Server.cpp), [Socket.hpp](demo_sources/headers/Socket.hpp), [User.hpp](demo_sources/headers/User.hpp) si [Channel.hpp](demo_sources/headers/Channel.hpp).

### 3.1 Cum functioneaza

Serverul pastreaza:

- un file descriptor pentru socket-ul de ascultare;
- `map<int, Socket*>` pentru conexiunile active;
- `map<string, User*>` pentru utilizatori, indexati dupa nickname normalizat;
- `map<string, Channel*>` pentru canale, indexate dupa nume normalizat.

Fiecare conexiune are:

- buffer de intrare;
- buffer de iesire;
- stare de inregistrare (`UNREGISTERED`, `NICK_SEEN`, `REGISTERED`);
- nickname, username si realname;
- pointer catre obiectul `User`.

### 3.2 Algoritmii principali, explicati simplu

#### A. Multiplexarea conexiunilor cu `poll()`

1. Serverul pune socket-ul de ascultare in lista de descriptorii monitorizati.
2. Adauga fiecare client in aceeasi lista.
3. Activeaza `POLLIN` pentru citire.
4. Activeaza si `POLLOUT` daca exista date in bufferul de iesire.
5. `poll()` asteapta pana cand apare o conexiune sau date.
6. Pentru fiecare descriptor activ:
   - accepta clienti noi;
   - citeste date;
   - trimite date ramase;
   - inchide conexiunile cu erori sau hang-up.

**Avantaj:** un singur fir de executie poate servi mai multi clienti fara blocarea pe un singur socket.

#### B. Buffering si delimitarea comenzilor

TCP nu pastreaza granita comenzilor: o comanda poate veni in mai multe bucati sau mai multe comenzi pot veni intr-un singur `recv()`.

Implementarea:

1. Citeste datele si le adauga in `inputBuffer`.
2. Cauta separatorul IRC `\r\n`.
3. Extrage fiecare linie completa.
4. Pastreaza restul incomplet pentru urmatorul `recv()`.
5. Respinge conexiunea daca bufferul depaseste 4096 bytes sau o linie depaseste 512 bytes.

Acesta este un algoritm simplu de **framing**: bufferul este coada de octeti, iar `\r\n` este delimitatorul.

#### C. Parsarea parametrilor IRC

Functia interna `splitParameters()` separa cuvintele dupa spatii, dar trateaza un parametru care incepe cu `:` ca parametru final:

```text
USER alex 0 * :Alex Katz
```

devine:

```text
USER | alex | 0 | * | Alex Katz
```

Astfel, numele real poate contine spatii, conform formei uzuale a mesajelor IRC.

#### D. Inregistrarea utilizatorului

Inregistrarea are doua etape:

1. `NICK` verifica formatul si unicitatea nickname-ului.
2. `USER` retine username-ul si realname-ul.
3. Cand ambele informatii exista, `finishRegistration()` creeaza `User`.
4. Serverul trimite mesajele numerice `001`-`004`.

Nickname-urile sunt normalizate cu lowercase pentru verificarea duplicatelor. Nickname-ul original ramane afisat clientului.

#### E. Crearea canalului si JOIN

1. Se verifica daca utilizatorul este inregistrat.
2. Se verifica numele canalului: trebuie sa inceapa cu `#` sau `&`, sa aiba lungime acceptata si sa nu contina caractere interzise.
3. Daca canalul nu exista, este creat.
4. Primul membru devine operator (`@`).
5. Utilizatorul este adaugat atat in canal, cat si in setul sau de canale.
6. Se transmite mesajul `JOIN` tuturor membrilor.
7. Se trimit `353` cu lista membrilor si `366` pentru finalul listei.

### 3.3 Puncte tari

- Este cea mai completa dintre cele doua ca demo IRC functional.
- Gestioneaza corect partial-read si multiple comenzi pe aceeasi conexiune.
- Are buffer de iesire, deci nu presupune ca `send()` trimite totul imediat.
- Foloseste socket-uri non-blocking si `poll()`.
- Are verificari utile pentru lungimea liniilor si a bufferului.
- Are stare explicita pentru inregistrarea clientului.
- Include canale, membri, operator initial si raspunsuri IRC de baza.
- Tratarea inchiderii clientului elimina utilizatorul din canale si din indexul de nickname.
- Codul compileaza curat cu nivel strict de warning-uri.

### 3.4 Puncte slabe

- `Server.cpp` contine prea multe responsabilitati: socket I/O, framing, parsing, validare si business logic.
- Foloseste pointeri raw si containere de pointeri, ceea ce face ownership-ul mai greu de urmarit.
- Nu exista o clasa dedicata pentru mesajul IRC sau pentru registry-ul de clienti.
- Suportul de comenzi este foarte restrans: lipsesc `PART`, `QUIT`, `MODE`, `TOPIC`, `KICK`, `INVITE`, `PRIVMSG` si multe altele.
- `JOIN` accepta un singur canal, nu liste de canale sau chei.
- Nu exista validarea completa pentru username, realname si continutul tuturor parametrilor.
- La inchiderea unui canal ramas fara membri, obiectul canalului nu este eliminat din `channels`.
- `acceptClients()` ignora eroarea `accept()` daca aceasta nu este `EAGAIN`/`EWOULDBLOCK`, in loc sa o raporteze explicit.
- `main.cpp` foloseste `atoi`, deci inputul numeric invalid nu este raportat suficient de precis.
- Bucla reconstruieste vectorul de `pollfd` la fiecare iteratie; este simplu, dar nu ideal pentru un numar mare de clienti.

## 4. Varianta 2: demo-ul modular

Fisierele relevante sunt [Server.hpp](another_demo/headers/Server.hpp), [Server.cpp](another_demo/srcs/Server.cpp), [Lexer.hpp](another_demo/headers/Lexer.hpp), [Lexer.cpp](another_demo/srcs/Lexer.cpp), [ASTParser.hpp](another_demo/headers/ASTParser.hpp), [ASTParser.cpp](another_demo/srcs/ASTParser.cpp) si [CommandDispatcher.cpp](another_demo/srcs/CommandDispatcher.cpp).

### 4.1 Cum functioneaza

Arhitectura este impartita in straturi:

1. `Server` gestioneaza socket-urile si evenimentele `poll()`.
2. `TCPSocket` ascunde apelurile POSIX `send()` si `recv()`.
3. `Lexer` transforma textul brut in token-uri.
4. `ASTParser` transforma token-urile intr-un `MessageAST`.
5. `CommandDispatcher` cauta handlerul comenzii intr-un `map`.
6. Handlerul modifica utilizatorul sau trimite un raspuns.

Aceasta separare este mai apropiata de o arhitectura extensibila decat parsarea directa din primul demo.

### 4.2 Algoritmii principali, explicati simplu

#### A. Lexer-ul

Lexer-ul parcurge textul cu un index:

1. Daca mesajul incepe cu `:`, citeste prefixul pana la spatiu.
2. Sare peste spatii.
3. Citeste primul cuvant ca `COMMAND`.
4. Citeste parametrii separati prin spatiu.
5. Daca intalneste `:`, citeste tot restul liniei ca `TRAILING`.
6. Adauga token-ul `END_OF_LINE`.

Exemplu:

```text
PRIVMSG #general :Salut tuturor
```

devine aproximativ:

```text
COMMAND("PRIVMSG")
PARAMETER("#general")
TRAILING("Salut tuturor")
END_OF_LINE
```

#### B. Parserul AST

Parserul foloseste un cursor peste vectorul de token-uri:

1. Citeste optional prefixul.
2. Citeste comanda.
3. Transforma fiecare `PARAMETER` sau `TRAILING` intr-un `ParamNode`.
4. Le pune in `MessageAST`.

Rezultatul este o structura mai usor de transmis handlerelor decat un sir neprelucrat.

#### C. Dispatcher-ul

Constructorul inregistreaza handler-ele intr-un tabel:

```text
NICK    -> handleNick
USER    -> handleUser
PING    -> handlePing
PRIVMSG -> handlePrivMsg
```

La primirea unei comenzi, `dispatch()` face o cautare in `map`, apoi apeleaza functia asociata. Adaugarea unei comenzi noi presupune, in principiu, un handler nou si o inregistrare in tabel.

#### D. Multiplexarea cu `poll()`

Spre deosebire de primul demo, vectorul `_pollFds` este pastrat ca membru al serverului:

1. Socket-ul server este adaugat la pornire.
2. Clientii noi sunt adaugati in vector.
3. `poll()` asteapta evenimente.
4. Pentru socket-ul server se apeleaza `acceptClient()`.
5. Pentru client se apeleaza `handleClientData()`.
6. La deconectare, utilizatorul si descriptorul sunt eliminate.

### 4.3 Puncte tari

- Separarea Lexer/Parser/Dispatcher face codul mai usor de inteles si de extins.
- Exista un model intermediar (`MessageAST`) care poate fi reutilizat de mai multe comenzi.
- `ISocket` permite inlocuirea implementarii concrete a socket-ului cu un mock pentru teste.
- `CommandDispatcher` evita un lant lung de `if/else if` pentru comenzile noi.
- `main.cpp` valideaza portul intre 1 si 65535.
- Este o baza buna pentru adaugarea unui parser IRC conform cu RFC-ul.

### 4.4 Puncte slabe

- Demo-ul nu compileaza cu `-Werror` din cauza parametrilor neutilizati din handler-ele `NICK`, `USER` si `PING`.
- Citirea nu are buffer per conexiune: presupune ca un `recv()` contine un mesaj complet.
- Daca un `recv()` contine doua comenzi, acestea nu sunt separate corect.
- Daca o comanda este impartita intre doua pachete TCP, prima parte este parsata prematur si se pierde contextul.
- Nu exista verificare pentru limita IRC de 512 bytes.
- Nu sunt tratate explicit `POLLERR`, `POLLHUP` si `POLLNVAL` in bucla principala.
- Socket-ul client este inchis in destructorul `TCPSocket`, dar `User::~User()` nu elibereaza `_socket`; ownership-ul nu este clar si poate produce leak sau double-close dupa extinderi.
- `removeClient()` sterge `User`, dar nu este evidenta inchiderea socket-ului in acelasi loc; se bazeaza indirect pe destructorul socket-ului.
- `NICK` accepta orice valoare, fara verificarea formatului sau a unicitatii.
- `USER` seteaza doar username-ul; nu implementeaza parametrii standard si nu finalizeaza o stare de autentificare.
- `PING` trimite un format de raspuns nestandard (`PONG: ...`).
- `PRIVMSG` catre un canal nu este implementat.
- Nu exista clasa `Channel`, broadcast catre membri, JOIN sau lista de membri.
- Comenzile necunoscute sunt doar afisate in log, fara raspuns numeric IRC.
- Lexer-ul pastreaza literele comenzii asa cum vin; dispatcher-ul cauta chei uppercase, deci o comanda precum `nick` poate sa nu fie recunoscuta.
- Variabila `server` este neutilizata in mai multe handler-e; pe langa eroarea de compilare, acest lucru arata ca integrarea cu starea serverului este incompleta.

## 5. Comparatie pe categorii

### Functionalitate IRC

**Avantaj: demo-ul integrat.** Are fluxul `NICK` + `USER`, raspunsuri numerice, `JOIN`, operator initial si `NAMES`. Demo-ul modular are mai multe nume de handler-e, dar mai putina functionalitate reala.

### Robustetea comunicarii TCP

**Avantaj clar: demo-ul integrat.** Bufferul per socket si delimitarea cu `\r\n` rezolva doua probleme fundamentale ale TCP: fragmentarea si agregarea mesajelor. Demo-ul modular trebuie sa adauge exact acest strat inainte de a fi folosit cu clienti reali.

### Claritatea arhitecturii

**Avantaj: demo-ul modular.** Lexer-ul, AST-ul si dispatcher-ul separa responsabilitatile. In primul demo, `Server.cpp` este mai greu de modificat fara a afecta alte parti.

### Testabilitate

**Potential mai mare: demo-ul modular.** `ISocket` permite mock-uri si teste izolate. Totusi, acest avantaj nu este valorificat inca prin teste si este afectat de ownership-ul neclar.

### Extinderea cu noi comenzi

**Avantaj: demo-ul modular.** Un tabel de handler-e este mai usor de extins decat un lant de comparatii in `processLine()`. In practica, trebuie adaugate si validarea, raspunsurile IRC si starea de protocol.

### Pregatirea pentru o versiune finala

Cele doua directii sunt complementare:

- se recomanda pastrarea stratului de I/O si a buffering-ului din demo-ul integrat;
- se recomanda preluarea separarii Lexer/AST/Dispatcher din demo-ul modular;
- modelele `User`, `Channel` si starea de inregistrare din primul demo trebuie integrate intr-o arhitectura modulara;
- ownership-ul trebuie clarificat, ideal prin obiecte detinute explicit sau containere cu responsabilitati documentate, compatibil cu regulile C++98 ale proiectului.

## 6. Recomandare finala

Pentru o demonstratie imediata, **demo-ul integrat este alegerea mai buna**, deoarece poate accepta clienti, poate finaliza inregistrarea si poate executa `JOIN` intr-un mod mai apropiat de IRC.

Pentru dezvoltarea proiectului pe termen lung, **demo-ul modular este directia arhitecturala mai buna**, dar numai dupa corectarea fundamentelor:

1. adaugarea unui input buffer per client si procesarea tuturor liniilor `\r\n`;
2. normalizarea comenzilor la uppercase;
3. validarea parametrilor si a starilor IRC;
4. raspunsuri IRC standard pentru erori si succes;
5. clarificarea ownership-ului pentru `User` si `ISocket`;
6. finalizarea `PRIVMSG`, a canalelor si a broadcast-ului;
7. eliminarea warning-urilor care blocheaza compilarea cu `-Werror`;
8. adaugarea de teste pentru lexer, parser, framing TCP si dispatcher.

**Concluzie:** cea mai buna baza pentru versiunea finala este o combinatie: **I/O robust si modelul de canale din primul demo + parserul si dispatcher-ul din al doilea demo**.

