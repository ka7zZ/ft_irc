# Explicatia diagramei — arhitectura serverului IRC, pentru cineva la inceput de drum

> Colegul specializat pe Excalidraw a desenat diagrama pornind exclusiv de
> la `AUDIT_ROBUSTETE_SI_AJUSTARI.md` (primul fisier). Fisierul
> `Diagrama_Arhitectura_IRC_Server.excalidraw` se deschide direct pe
> excalidraw.com (Menu -> Open, sau tragi fisierul in fereastra browserului).
> Acest document explica **gandirea din spatele fiecarei forme si sageti**,
> fara niciun cod — scopul e sa intelegi *ce* trebuie construit si *de ce*
> arată asa, inainte sa scrii vreo linie.

---

## 1. Cum se citeste diagrama (legenda, explicata)

Patru forme, patru intrebari diferite:

- **Dreptunghi = o clasa.** Raspunde la intrebarea "ce *tip de lucru*
  exista in program?" O clasa e un sablon: `User` e sablonul dupa care
  arata *orice* utilizator conectat, nu un utilizator anume.
- **Rotund (oval) = un atribut.** Raspunde la intrebarea "ce *stie* sau ce
  *tine minte* acest lucru?" Un atribut e o informatie pastrata, nu o
  actiune.
- **Romb = o functie/metoda.** Raspunde la intrebarea "ce *poate face*
  acest lucru, sau ce i se poate cere sa faca?" O functie e o actiune,
  intotdeauna legata de o clasa anume (romburile din diagrama sunt asezate
  chiar sub dreptunghiul clasei careia ii apartin).
- **Sageata = comunicare intre doua clase.** Raspunde la intrebarea "cine
  vorbeste cu cine, si ce ii trimite?" Fiecare sageata are o eticheta
  scurta care spune exact ce circula intre cele doua clase in acel moment.
- **Patratul galben = punctul de start.** `main()` nu e o clasa in sensul
  de mai sus — e literalmente primul lucru care ruleaza cand pornesti
  programul. De aceea are o forma si o culoare diferite de tot restul: nu
  face parte din "lumea" claselor, el doar o **pune in miscare**.

---

## 2. De ce diagrama e organizata ca un lant, de sus in jos

Cea mai importanta decizie de desen, luata inainte de a plasa vreo forma:
diagrama urmareste **drumul pe care il face o singura comanda IRC**, de la
momentul in care intra pe fir pana la momentul in care cineva primeste un
raspuns. Nu e o organizare aleatorie — e alegerea deliberata de a arata
programul ca pe o **poveste cu inceput si sfarsit**, nu ca pe o lista de
clase fara legatura vizuala intre ele.

Motivul pedagogic: cineva la inceput de drum se pierde rapid intr-o
diagrama care arata toate clasele "plutind" fara ordine, pentru ca nu stie
*in ce ordine sa le citeasca*. Un lant de sus in jos elimina aceasta
problema — citesti diagrama exact cum ai citi o poveste, un pas dupa altul.

---

## 3. Ce inseamna fiecare "veriga" a lantului si de ce exista

### `main()` — punctul galben

E prima piesa de pe diagrama, sus de tot. Singurul lucru pe care il face e
sa **creeze** un obiect `Server` si sa ii spuna sa porneasca (`run()`).
Dupa acest moment, `main()` nu mai face nimic activ — sta si asteapta ca
`Server` sa isi termine treaba (ceea ce, la un server, inseamna practic
"pana e oprit"). E important sa intelegi asta devreme: **`main()` nu
gestioneaza clienti, canale sau mesaje.** El doar apasa butonul de pornire.

### `Server` — dreptunghiul mare, centrul diagramei

E cea mai mare clasa din diagrama, si nu intamplator: `Server` e locul
unde traiesc toate colectiile mari ale programului (toti clientii, toate
canalele) si tot ce tine de "a asculta lumea exterioara". Atributele lui
(rotundele de sub dreptunghi) spun exact ce tine minte:

- **`_listenFd`** — "urechea" prin care asculta conexiuni noi. E un singur
  numar, mereu acelasi cat timp serverul ruleaza.
- **`_users`** — o legatura intre "cine e conectat pe ce fir" (fd) si
  "cine e acest utilizator" (obiectul `User`). Fara aceasta legatura,
  serverul nu ar putea sti carui client sa ii raspunda.
- **`_channels`** — la fel, dar pentru canale: numele canalului duce catre
  obiectul `Channel` corespunzator.
- **`_pollFds`** — lista tuturor firelor (fd-urilor) pe care serverul le
  "verifica" periodic ca sa vada daca a sosit ceva nou.

Functiile lui (romburile) spun ce *face* efectiv, in ordinea logica in
care se intampla:

- **`run()`** — bucla mare, care nu se opreste (cat timp serverul e pornit).
- **`acceptPendingConnections()`** — verifica daca a batut cineva nou la
  usa (o conexiune noua) si o accepta.
- **`readClient()`** — citeste ce a trimis un client deja conectat.
- **`reapDisconnectedUsers()`** — face curatenie dupa clientii care au
  plecat, la sfarsitul fiecarei runde de verificare.
- **`removeClient()`** — scoate efectiv un client din toate listele in
  care apare (asa cum a fost explicat pe larg in documentul de cazuri
  limita, C10).

### `Fd` — mica, dar desenata separat intentionat

Ai putea intreba: de ce `Fd` are propriul dreptunghi, daca e "doar un
numar intreg"? Raspunsul e chiar ideea centrala din auditul de robustete:
un fd (numarul care identifica o conexiune la nivel de sistem de operare)
**trebuie sa aiba un singur proprietar clar**, care il inchide o singura
data. Desenandu-l ca o clasa separata (nu doar ca un atribut ascuns in alta
parte), diagrama iti aminteste vizual: "acesta e un lucru de care trebuie
sa ai grija explicit, nu doar un numar oarecare".

### `Connection` — "cutia postala" a fiecarui client

`Connection` retine tot ce tine de comunicarea cu **un singur** client:

- **`_input`** — textul brut, neprocesat inca, primit de la client. Poate
  fi o comanda incompleta, o comanda completa, sau mai multe lipite.
- **`_output`** — mesajele care asteapta sa fie trimise inapoi catre
  client.
- **`_outputBytes`** — cati octeti sunt in asteptare, ca sa nu lasi coada
  sa creasca la nesfarsit (vezi Ajustarea 3 din auditul de baza).

Functiile ei transforma text brut in comenzi utilizabile si invers:

- **`appendInput()`** — adauga octeti noi primiti de la client.
- **`popLine()`** — scoate o singura comanda completa din tot acel text
  brut (daca exista una gata).
- **`queue()`** — pune un mesaj de raspuns in asteptare pentru a fi
  trimis.
- **`flush()`** — trimite efectiv, cat se poate, din mesajele in
  asteptare.

### `IrcParser` — "traducatorul"

Primeste o linie de text simpla (ex: `NICK ana`) si o transforma intr-o
forma pe care restul programului o poate intelege usor: o comanda
(`NICK`) plus parametrii ei (`ana`). Are o singura functie, `parseLine()`,
pentru ca are o singura responsabilitate: **traducere**, nimic mai mult —
nu decide daca comanda e permisa acum, doar daca e scrisa corect.

### `CommandDispatcher` — "dispecerul"

Primeste comanda deja tradusa si decide **cui ii trimite treaba mai
departe**: daca e `NICK`, merge la `UserRegistry`; daca e `JOIN`, `PART`,
`KICK` sau `PRIVMSG` catre un canal, merge la `Channel`. E desenat cu o
singura functie, `dispatch()`, pentru acelasi motiv ca la `IrcParser`:
responsabilitate unica, de data asta "trimite treaba la locul potrivit",
nu "rezolva treaba insusi".

### `UserRegistry` — "cartea de telefon" a serverului

Tine o singura legatura simpla, dar critica: nickname -> `User`. E
desenata separat de `Server` (desi ar putea parea similara cu `_users`)
pentru ca rezolva o problema diferita: `Server._users` stie "ce fir
apartine cui", `UserRegistry` stie "cine se numeste cum". Functia
`rename()` e evidentiata separat pentru ca acolo se intampla exact
problema descrisa in Ajustarea 6 din auditul de baza: schimbarea unui
nickname trebuie sa actualizeze aceasta carte de telefon **atomic**, dintr-o
singura miscare, nu in mai multi pasi separati care ar putea lasa
inregistrarea inconsistenta.

### `Channel` — "camera de discutii"

Tine numele camerei si lista celor prezenti in ea (`members`).
`broadcast()` e functia care trimite un mesaj catre toata lumea din camera
(vezi sageata catre `Connection`, explicata mai jos), iar `removeMember()`
scoate pe cineva din camera — si, daca ramane goala, o desfiinteaza
(Ajustarea 7 din auditul de baza).

### `User` — "persoana" din spatele conexiunii

Ultima veriga a lantului: `nick`-ul persoanei si lista canalelor la care e
prezenta. E desenata dupa `UserRegistry` si `Channel`, nu inaintea lor, ca
sa arate ceva important: `User` e "cunoscut" de celelalte doua clase, ele
il actualizeaza, nu invers — `User` insusi nu decide singur sa se
inregistreze sau sa intre intr-un canal, ci raspunde la ce ii spun
`UserRegistry` si `Channel`.

---

## 4. De ce exista exact aceste sageti, si nu altele

Fiecare sageata a fost gandita ca raspuns la o intrebare de forma "cand se
intampla X, cine trebuie sa afle despre asta?":

1. **`main()` -> `Server`** ("creeaza si porneste"): fara aceasta sageata,
   nimic altceva din diagrama nu ar porni vreodata. E singura sageata care
   se intampla o singura data, la inceput.
2. **`Server` -> `Fd`** ("detine / inchide fd"): arata ca `Server` e
   responsabil pentru fd-ul principal de ascultare — el il deschide la
   pornire si trebuie sa il inchida la oprire.
3. **`Server` -> `Connection`** ("detine, citeste/scrie"): pentru fiecare
   client acceptat, `Server` creeaza si detine cate un obiect `Connection`.
4. **`Fd` -> `Connection`** ("detine fd-ul clientului"): fiecare
   `Connection` are propriul ei `Fd`, diferit de cel al lui `Server`
   (unul e "usa principala", celalalt e "linia telefonica a unui client
   anume").
5. **`Connection` -> `IrcParser`** ("linie IRC completa"): abia cand
   `Connection` a adunat o linie completa (cu `\r\n` la capat), o trimite
   mai departe. Aceasta sageata e "poarta" care garanteaza ca traducatorul
   nu primeste niciodata fragmente de comanda.
6. **`IrcParser` -> `CommandDispatcher`** ("comanda validata"): dupa
   traducere, treaba merge la dispecer.
7. **`CommandDispatcher` -> `UserRegistry`** ("NICK / inregistrare"):
   pentru comenzi care schimba identitatea utilizatorului.
8. **`CommandDispatcher` -> `Channel`** ("JOIN / PART / KICK / PRIVMSG"):
   pentru comenzi care schimba sau folosesc o camera de discutii.
9. **`CommandDispatcher` -> `Connection`** ("raspuns numeric"): nu orice
   comanda ajunge la `UserRegistry` sau `Channel` — uneori dispecerul
   raspunde direct (de exemplu, o eroare "comanda necunoscuta"), fara sa
   mai implice restul lantului.
10. **`UserRegistry` -> `User`** ("actualizeaza nick"): cand un nickname e
    schimbat cu succes, persoana (`User`) trebuie sa afle si ea noul nume.
11. **`Channel` -> `User`** ("membru al canalului"): cand cineva intra
    intr-o camera, atat camera cat si persoana trebuie sa stie asta —
    de aici invarianta din Ajustarea 7 a auditului de baza (`user.channels`
    contine canalul <=> `channel.members` contine persoana).
12. **`Channel` -> `Connection`** ("broadcast() -> queue() la fiecare
    membru"): aceasta e sageata cea mai importanta de retinut pentru
    intelegerea *fluxului invers*. Un mesaj nu merge doar "inainte" (de la
    client, prin parser, prin dispecer); rezultatul unei comenzi (de
    exemplu un `PRIVMSG` catre un canal) trebuie sa ajunga **inapoi**, la
    coada de iesire (`queue()`) a *fiecarui* `Connection` al membrilor
    canalului, nu doar a celui care a trimis comanda initial.

**Observatia de retinut, dupa ce ai citit toate cele 12 sageti:** fluxul nu
e o singura linie dreapta de la inceput la sfarsit. E un lant care merge
"inainte" (de la text brut la comanda inteleasa) pana la `CommandDispatcher`,
apoi se **ramifica** (spre `UserRegistry` sau `Channel`, dupa tipul
comenzii), si in final se **intoarce** spre `Connection` — uneori catre
conexiunea initiala (raspuns direct), alteori catre conexiunile *altor*
utilizatori cu totul (broadcast catre un canal). Aceasta ramificare si
intoarcere e exact motivul pentru care un server de chat e mai complicat
decat un simplu "intreaba-raspunde": un singur mesaj poate avea mai multi
destinatari, decisi dinamic, in functie de cine e prezent unde in acel
moment.

---

## 5. O intrebare-ghid, pentru cand adaugi o comanda IRC noua

Odata ce ai inteles diagrama, orice comanda IRC noua pe care vrei sa o
adaugi (de exemplu `TOPIC`, `WHO`, `AWAY`) se verifica punand aceleasi
intrebari pe care le-a raspuns deja diagrama pentru comenzile existente:

1. Unde intra in lant? (Aproape mereu: `IrcParser` -> `CommandDispatcher`,
   fara exceptie — punctul de intrare e mereu acelasi.)
2. Spre ce se ramifica dupa `CommandDispatcher`? (`UserRegistry`,
   `Channel`, sau raspuns direct catre `Connection`?)
3. Cine trebuie sa primeasca rezultatul — doar expeditorul, sau si alti
   utilizatori (caz in care ai nevoie de o sageata de tip "broadcast", ca
   cea de la `Channel` la `Connection`)?

Daca poti raspunde la aceste trei intrebari pentru o comanda noua, stii deja
exact unde se incadreaza in arhitectura desenata, fara sa mai trebuiasca sa
regandesti tot programul de la zero.
