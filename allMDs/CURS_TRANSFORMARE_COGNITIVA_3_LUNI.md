# De la procese cognitive de baza la gandire de arhitect — curs comprimat, 3 luni

> **O precizare inainte de orice, pentru ca merita sa fie spusa direct:**
> nu exista cercetare stiintifica secreta, nepublicata, care sa transforme
> pe cineva peste noapte intr-un "super-programator". Ce urmeaza mai jos e
> o sinteza densa a unor cercetari reale, publicate, din stiinta expertizei
> si a invatarii (Ericsson, Sweller, Bjork, Chase & Simon, Kahneman) plus
> practici de inginerie software documentate (Polya, Naur, Ousterhout, SEI/
> ATAM, arhitectii care scriu ADR-uri). Motivul pentru care multe din ele
> "par secrete" e simplu: sunt raspandite in surse de nisa (jurnale de
> psihologie cognitiva, rapoarte tehnice ale institutelor de cercetare,
> carti de specialitate putin citite de programatori), nu pentru ca ar fi
> ascunse deliberat. Colegul la care am apelat e cel care le-a adunat si
> le-a testat pe el si pe oamenii pe care i-a mentorat; ce gasesti mai jos
> e distilarea directa a raspunsului lui, fara sa promit nimic magic.

---

## Partea 1 — De ce functioneaza transformarea (baza stiintifica)

### 1.1 Chunking — motivul pentru care un expert "vede" solutia, nu o calculeaza

Studiul clasic al lui **Chase & Simon (1973)** pe jucatori de sah a aratat
ceva contraintuitiv: marii maestri nu au memorie mai buna in general — au
memorie mai buna **doar** pentru pozitii de sah reale, pentru ca le-au
vazut de mii de ori si le stocheaza in memorie ca unitati mari ("chunk-uri"),
nu piesa cu piesa. Pus in fata unei pozitii aleatorii (fara sens de joc),
performanta lor scade la nivelul unui incepator.

Aplicat direct la programare: un programator senior "vede" ca o bucata de
cod e o problema de framing TCP sau de iterator invalidation dintr-o
privire, nu pentru ca gandeste mai repede in general, ci pentru ca a
construit deja acel *chunk* — a vazut suficiente cazuri similare incat
tiparul intreg (nu detaliile lui) sa fie recunoscut instant. **Concluzia
practica:** capacitatea de "a-ti da seama ce algoritm trebuie" nu e talent
abstract, e o biblioteca de tipare construita prin expunere repetata si
deliberata la aceleasi forme de problema, in variante suficient de diferite
incat sa generalizezi tiparul, nu exemplul.

### 1.2 Incarcarea cognitiva (Cognitive Load Theory — John Sweller)

Memoria de lucru (working memory) e extrem de limitata — clasicul "7±2"
al lui Miller, revizuit ulterior spre 4±1 pentru elemente complexe.
Sweller imparte incarcarea cognitiva in trei tipuri:

- **Intrinseca** — complexitatea inerenta a problemei (nu o poti reduce
  fara sa simplifici problema insasi).
- **Extraneana** — efort irosit pe forma in care e prezentata informatia
  (cod scris prost, sintaxa noua, unelte nefamiliare) — **aceasta trebuie
  eliminata agresiv cand inveti ceva nou**.
- **Germana** — efortul care chiar construieste scheme mentale noi (utile,
  de pastrat).

**Concluzia practica:** cand inveti un algoritm nou, elimina orice alta
noutate simultana (limbaj nou, unealta noua, domeniu nou). Exersezi
algoritmi noi in limbajul in care esti deja fluent; exersezi limbaj nou pe
probleme deja rezolvate mental. Niciodata amandoua deodata — asta satureaza
memoria de lucru si nu mai ramane capacitate pentru invatarea reala
(incarcarea germana).

### 1.3 Worked examples effect si "fading" (Sweller, Renkl, Atkinson)

Incepatorii invata **mai repede** din exemple complet rezolvate, explicate
pas cu pas, decat incercand sa rezolve de la zero ("descoperire ghidata"
functioneaza prost la inceput — genereaza incarcare extraneana masiva).
Pe masura ce competenta creste, exemplele complet rezolvate devin
ineficiente (efectul de "expertise reversal") si trebuie inlocuite cu
probleme partial rezolvate, apoi complet nerezolvate — asta se numeste
**fading**.

**Concluzia practica, directa pentru cele doua fisiere anterioare:**
documentul cu 15 cazuri limita si solutii complete (C1-C15) e exact un set
de *worked examples* — corect ca punct de plecare. Pasul urmator, esential,
e sa nu ramai la "citesc solutii complete": treci constient la probleme cu
solutia partial data (doar diagnosticul, tu scrii fix-ul), apoi la probleme
complet nerezolvate (doar simptomul: "serverul crapa quasi-aleator sub
sarcina", tu diagnostichezi si rezolvi).

### 1.4 Deliberate practice (Anders Ericsson)

Diferenta dintre "10.000 de ore de experienta" si "10.000 de ore de
practica deliberata" e enorma. Practica deliberata are patru conditii,
toate obligatorii — daca lipseste una, practica stagneaza:

1. **Obiectiv specific**, putin peste nivelul curent (nu general — "vreau
   sa scriu cod mai bun" nu e un obiectiv de practica deliberata).
2. **Concentrare totala** in timpul exercitiului (nu practica pasiva,
   automata, "pe pilot").
3. **Feedback imediat**, ideal de la cineva mai bun sau de la un mecanism
   obiectiv (teste care trec/pica, code review, un compilator strict).
4. **Repetitie cu ajustare** — repeti aceeasi clasa de problema, ajustand
   dupa fiecare feedback, pana la automatizare.

**Concluzia practica:** "am scris cod 3 luni" nu inseamna nimic fara aceste
patru conditii. Programul din Partea 8 e construit explicit sa le respecte
pe toate patru, in fiecare saptamana.

### 1.5 Desirable difficulties (Robert Bjork)

Trei descoperiri contraintuitive, foarte solide in literatura de psihologie
cognitiva, dar prost cunoscute in afara domeniului:

- **Spacing effect**: inveti mai bine acelasi volum de informatie distribuit
  in timp (cate putin, des) decat concentrat (o sesiune lunga). Reluarea
  dupa cateva zile, cand ai inceput deja sa uiti putin, consolideaza mult
  mai bine decat repetitia imediata.
- **Interleaving**: practicarea mai multor tipuri de probleme amestecate
  (nu bloc dupa bloc, "azi doar RAII, maine doar STL") forteaza creierul sa
  discrimineze intre tipare, exact abilitatea de care ai nevoie ca sa
  "stii ce algoritm trebuie" — pentru ca in realitate problemele nu vin
  etichetate.
- **Retrieval practice (testing effect)**: incercarea activa de a-ti
  aminti/reconstrui o solutie (fara sa te uiti la ea) consolideaza memoria
  mult mai bine decat recitirea pasiva a solutiei.

**Concluzia practica:** programul din Partea 8 amesteca deliberat categorii
de probleme in aceeasi saptamana (nu "saptamana RAII, saptamana STL"), si
foloseste des tehnica "inchide documentul si reconstruieste solutia din
memorie", nu recitire pasiva.

### 1.6 Metacognitie si self-explanation effect

Studiile lui **Chi et al. (1989)** au aratat ca studentii care isi explica
lor insisi, in cuvinte proprii, de ce fiecare pas dintr-un exemplu rezolvat
functioneaza (nu doar il citesc) invata semnificativ mai bine si transfera
cunostinta la probleme noi mai usor. Asta se numeste **self-explanation
effect**.

**Concluzia practica:** pentru fiecare tehnica din catalogul de algoritmi
(fisierul anterior), scrii singur, in propriile cuvinte, raspunsul la:
"de ce functioneaza asta, ce s-ar strica exact daca nu as folosi-o, si in
ce alta problema, aparent diferita, ar mai functiona acelasi principiu?"
Aceasta ultima intrebare e cea care construieste transferul (vezi 1.7).

### 1.7 Transfer (apropiat vs. departat) si de ce "stiu solutia" nu inseamna "stiu cand sa o folosesc"

**Transferul apropiat** (near transfer) inseamna sa aplici o tehnica la o
problema aproape identica cu cea invatata. **Transferul departat** (far
transfer) inseamna sa recunosti acelasi principiu intr-o problema care
arata complet diferit la suprafata. Cercetarea arata consistent ca
transferul departat e mult mai greu de obtinut si necesita expunere la
**variatie de suprafata cu principiu identic** — exact tehnica de
interleaving de mai sus, dar aplicata deliberat: aceeasi tehnica (de
exemplu "marcheaza, apoi aplica dupa", din C2/C11/C13), vazuta intr-un
context de retea (iteratori invalidati), apoi intr-un context de UI
(evenimente care modifica o lista de widget-uri in timpul randarii), apoi
intr-un context de baze de date (tranzactii care modifica randuri citite de
alt cursor).

**Concluzia practica:** de fiecare data cand inveti o tehnica noua, cauti
constient **cel putin un exemplu dintr-un domeniu complet diferit** unde se
aplica acelasi principiu. Asta e diferenta reala dintre "am memorat solutii"
si "am inteles principiul".

### 1.8 Gandire duala (Kahneman) — expertiza reala e System 1 antrenat, nu System 2 rapid

Kahneman descrie doua moduri de gandire: **System 1** (rapid, automat,
bazat pe tipare — "intuitie") si **System 2** (lent, deliberat, analitic —
"rationament explicit"). Ideea gresita, foarte raspandita, e ca expertul e
cineva care "gandeste mai repede" cu System 2. De fapt, **expertiza reala
muta procesele din System 2 in System 1**: dupa suficienta practica
deliberata, recunoasterea tiparului devine automata (System 1), iar
System 2 ramane liber sa se ocupe doar de partea cu adevarat noua a
problemei.

**Concluzia practica, cea mai importanta din toata Partea 1:** obiectivul
celor 3 luni nu e sa inveti "mai multe lucruri" cu System 2 (mai multe
fapte, mai multe denumiri), ci sa **muti in System 1**, prin repetitie
deliberata si spatiata, un set de recunoasteri care acum iti cer efort
constient. Cand vezi un `vector` modificat in timpul iteratiei si reactia
"aha, erase-remove idiom" apare fara sa te gandesti, ai reusit transferul.

---

## Partea 2 — Cand trebuie implementat fiecare algoritm: antrenarea recunoasterii de tipar

### 2.1 Harta simptom -> tehnica (instrumentul central)

Cea mai directa aplicatie a lui 1.1 (chunking): in loc sa memorezi
algoritmi izolat, construiesti o harta bidirectionala **simptom -> tehnica**
si **tehnica -> simptome in care apare**. Plecand de la catalogul de
algoritmi din fisierul anterior, iata formatul exact recomandat (il
construiesti tu, pe cartonase fizice sau digital, nu il citesti gata facut
— actul de a-l scrie e cel care construieste chunk-ul, vezi 1.6):

```text
SIMPTOM: "un pointer/referinta catre un obiect poate deveni invalid intre
          doi pasi ai aceleiasi operatii"
  -> Registry + verificare "mai exista?" prin identificator stabil
  -> weak_ptr (C++ modern) / re-cautare prin id (C++98)
  -> vezi: C3, C13 din auditul de cazuri limita

SIMPTOM: "modific o colectie in timp ce o parcurg"
  -> erase-remove idiom
  -> "marcheaza, apoi aplica dupa" (colecteaza intr-un container separat)
  -> vezi: C2, C11

SIMPTOM: "o resursa trebuie eliberata garantat, indiferent de calea de iesire"
  -> RAII
  -> vezi: Fd, Connection

SIMPTOM: "o operatie de retea/disc poate returna mai putin decat am cerut"
  -> bucla de retry cu verificare explicita a cazului partial
  -> vezi: partial read/write loop
```

**Regula de constructie a hartii:** fiecare intrare are exact trei parti —
simptomul descris in cuvinte, tehnica (numele exact din catalog), si un
exemplu concret unde ai vazut-o aplicata. Fara al treilea element (exemplul
concret), harta ramane abstracta si nu se transforma in recunoastere
automata.

### 2.2 Adaptarea metodei lui Polya (patru pasi) pentru alegerea algoritmului

George Polya, in *How to Solve It* (1945), a formalizat rezolvarea de
probleme matematice in patru pasi — extrem de transferabili la alegerea
unui algoritm/tehnica in programare:

1. **Intelege problema.** Care sunt exact intrarile, iesirile,
   constrangerile? (Pentru un caz limita: care e secventa exacta de
   evenimente care il declanseaza?)
2. **Devise a plan (cauta o legatura cu ceva cunoscut).** Ai mai vazut o
   problema *cu aceeasi structura*, chiar daca in alt domeniu? Aici intra
   direct harta simptom -> tehnica de la 2.1.
3. **Executa planul.** Implementezi, verificand la fiecare pas ca esti
   inca aliniat cu planul (nu improvizezi la mijloc fara sa te intorci la
   pasul 2 daca planul nu se potriveste).
4. **Priveste inapoi (look back).** Functioneaza solutia si pentru cazuri
   la limita? Exista o solutie mai simpla? **Poti generaliza tehnica
   folosita la o alta clasa de probleme?** — acest ultim pas e cel care
   hraneste transferul departat (1.7) si e cel mai des sarit de programatori
   grabiti.

### 2.3 Exercitiu de "etichetare a problemei" (tag-uri, ca la judecatorii de concurs)

Platformele de programare competitiva (Codeforces, LeetCode) eticheteaza
fiecare problema cu tag-uri ("two pointers", "dynamic programming",
"binary search"), tocmai pentru ca recunoasterea tiparului e o abilitate
antrenabila separat de rezolvarea efectiva. Exercitiul recomandat:

1. Iei un caz nou (dintr-un audit, dintr-un bug real, dintr-o problema de
   concurs) **fara sa citesti solutia**.
2. Scrii, inainte sa incerci sa rezolvi, ce tag-uri crezi ca se aplica
   ("asta miroase a iterator invalidation", "asta miroase a state machine
   lipsa").
3. Abia apoi rezolvi.
4. Compari etichetele tale dinainte cu tehnica reala folosita in solutie.

Diferenta dintre etichetele tale si solutia reala **este** exact ce trebuie
antrenat — repetitia asta, facuta constient si spatiat (nu 20 deodata,
2-3 pe zi, pe parcursul saptamanilor), e mecanismul central prin care se
formeaza chunk-urile din 1.1.

---

## Partea 3 — Cum procesezi informatia ca sa iti scape mai putine lucruri de implementat

### 3.1 Citire in trei treceri (multi-pass reading), nu o singura trecere liniara

Cand primesti o specificatie/cerinta noua (ca cea a serverului IRC), o
singura citire liniara garanteaza ca ratezi lucruri — atentia si memoria de
lucru sunt limitate (1.2). In loc de asta, trei treceri separate, fiecare
cu un singur obiectiv:

- **Trecerea 1 — entitati si cicluri de viata.** Ignori complet
  interactiunile. Faci doar o lista: ce "lucruri" exista (User, Channel,
  Connection), si pentru fiecare, care e ciclul lui de viata complet, de
  la creare la distrugere, cu toate starile intermediare.
- **Trecerea 2 — interactiuni si moduri de esec.** Acum te uiti doar la
  cum interactioneaza entitatile intre ele, si explicit: **ce se intampla
  cand fiecare interactiune esueaza sau se intampla in ordine neasteptata?**
  Aici apar exact cazurile C3, C11, C13 din auditul anterior.
- **Trecerea 3 — cerinte non-functionale.** Performanta, limite de
  resurse, securitate, testabilitate. Acestea sunt aproape mereu implicite
  in cerinta ("trebuie sa reziste la orice input" e o cerinta non-functionala
  ascunsa intr-o propozitie banala).

### 3.2 Premortem (Gary Klein) — tehnica anti-optimism

**Premortem-ul**, descris de psihologul Gary Klein, inverseaza post-mortem-ul:
inainte sa incepi implementarea, presupui explicit ca **proiectul a esuat
deja, complet**, si scrii toate motivele posibile pentru care s-ar fi putut
intampla asta. Cercetarea lui Klein (si replicari ulterioare) arata ca
aceasta reformulare ("de ce a esuat" in loc de "ce ar putea sa mearga
prost") creste semnificativ numarul de riscuri identificate, pentru ca
elimina bias-ul de optimism care ne face sa subestimam probabilitatea
propriilor greseli.

**Exercitiu concret pentru orice proiect nou:** scrii, inainte de a scrie
prima linie de cod, 10 propozitii de forma "serverul a crapat in productie
pentru ca...". Fiecare propozitie e un candidat direct pentru un caz limita
de testat, exact ca cele din C1-C15.

### 3.3 FMEA adaptat la software (imprumutat direct din ingineria de siguranta)

**FMEA (Failure Mode and Effects Analysis)** e o metoda formala din
ingineria de siguranta (folosita in aviatie, auto, medical) — foarte
relevanta pentru cineva orientat spre domeniul de aparare, pentru ca e
literalmente instrumentul standard folosit acolo. Structura, adaptata la o
componenta software:

| Componenta | Mod de esec posibil | Cauza | Efect | Severitate (1-10) | Probabilitate (1-10) | Cum se detecteaza | Actiune |
|---|---|---|---|---|---|---|---|
| `Connection::popLine` | linie fara `\r\n` la infinit | client rau-intentionat | consum memorie nelimitat | 8 | 6 | test de fuzzing cu octeti fara CRLF | `MAX_PENDING_INPUT` |

**Concluzia practica:** pentru orice componenta noua, inainte sa o
implementezi, completezi cel putin 3-5 randuri din acest tabel. Nu trebuie
sa fie formal ca in industrie — chiar si un tabel simplu, facut constient,
prinde cazuri pe care "gandirea libera" le rateaza, exact pentru ca
forteaza o parcurgere sistematica in loc de una asociativa.

### 3.4 Modelare de domeniu prin diagrame de stare, pentru fiecare entitate

Pentru fiecare entitate identificata la 3.1 (trecerea 1), desenezi explicit
diagrama de stari — nu doar in cap, pe hartie sau intr-un fisier. De
exemplu, pentru `Connection`:

```text
CONECTAT -> ASTEAPTA_PASS -> ASTEAPTA_NICK -> ASTEAPTA_USER -> INREGISTRAT -> DECONECTAT
```

Intrebarea care prinde cele mai multe bug-uri la acest pas: **"ce se
intampla daca primesc orice alt eveniment/comanda in afara celei asteptate,
in fiecare stare?"** Fiecare celula goala dintr-un tabel "stare x eveniment"
completat sistematic e un caz limita neacoperit.

### 3.5 Gandire de ordinul al doilea ("si-apoi-ce")

Tehnica, populara in luarea de decizii (folosita explicit in analiza de
risc si in strategia militara/de afaceri), consta in a nu te opri la primul
efect al unei decizii, ci a intreba explicit **"si-apoi-ce se intampla"**
de cel putin doua-trei ori la rand:

```text
Decizie: "sterg canalul cand ramane fara membri" (efect de ordinul 1)
-> Si-apoi-ce? "orice pointer catre acel canal, tinut in alta parte a
    codului, devine dangling" (efect de ordinul 2)
-> Si-apoi-ce? "trebuie sa gasesc toate locurile care ar putea tine
    un asemenea pointer peste mai multe iteratii" (efect de ordinul 3,
    exact C13 din audit)
```

Aceasta e, de fapt, mecanismul explicit din spatele lui C3, C10, C13 din
documentul anterior — nu le-am gasit ghicind, le-am gasit aplicand
sistematic "si-apoi-ce" pe fiecare decizie de design.

---

## Partea 4 — Intrebari care decid arhitectura unui proiect

Lista de mai jos nu e in ordine de importanta — e in ordinea in care ar
trebui puse, pentru ca raspunsurile timpurii restrang optiunile ulterioare:

1. **Care sunt actorii externi si ce asteapta ei de la sistem?** (clienti
   IRC, alt server IRC in retea, administratorul serverului)
2. **Care e modelul de concurenta?** Un singur fir de executie cu event
   loop (`poll`), sau mai multe fire? Aceasta decizie unica influenteaza
   aproape tot restul (necesitatea lacate/mutex-uri, structura buclei
   principale, cum arata testele).
3. **Unde sunt granitele de incredere (trust boundaries)?** Ce vine direct
   de la un client neautentificat trebuie tratat complet diferit fata de
   date generate intern de server.
4. **Ce se intampla cand fiecare componenta esueaza?** (nu doar "daca",
   ci explicit "cand" — orice componenta de retea esueaza garantat, la un
   moment dat)
5. **Cine detine (ownership) fiecare resursa si obiect?** Raspunsul la
   aceasta intrebare, pus explicit *inainte* de a scrie clase, elimina
   direct clasele de bug-uri C3/C10/C13.
6. **Ce parti ale sistemului se vor schimba des, si ce parti raman
   stabile?** (Open/Closed Principle: izolezi partea volatila in spatele
   unei interfete stabile — de exemplu, comenzile IRC noi nu ar trebui sa
   ceara modificarea buclei de `poll`)
7. **Ce trebuie sa poata fi testat izolat, fara restul sistemului pornit?**
   Raspunsul la aceasta intrebare decide direct granitele modulelor
   (`ISocket` desprins de `Server`, de exemplu).
8. **Care sunt constrangerile dure, ne-negociabile?** (aici: C++98,
   fara librarii externe, single-threaded probabil) — acestea elimina
   direct optiuni intregi din start (nu discuti `std::shared_ptr` daca
   esti pe C++98).
9. **Care operatii sunt frecvente (hot path) si care sunt rare (cold
   path)?** Designul optimizeaza pentru claritate pe cold path si pentru
   eficienta pe hot path — a trata ambele la fel duce fie la cod prea
   complicat pentru operatii rare, fie prea incet pe cele frecvente.
10. **Ce se intampla la scara?** (10 clienti vs. 10.000 clienti) — chiar
    daca proiectul curent nu are nevoie de scara mare, intrebarea arata
    unde structura actuala s-ar rupe prima data.

---

## Partea 5 — Ce analiza faci concret pentru a ajunge la arhitectura

### 5.1 Modelul C4 (Simon Brown) — patru nivele de zoom

O confuzie frecventa la programatori mai putin experimentati e sa discute
arhitectura la un singur nivel de detaliu (de obicei prea jos, direct la
clase). Modelul C4 separa explicit patru nivele, fiecare cu propriul
public si scop:

1. **Context** — sistemul ca o cutie neagra, si cine/ce interactioneaza
   cu el (clienti IRC, admin). Public: oricine, inclusiv non-tehnic.
2. **Container** — "cutii" mari care ruleaza separat (in acest proiect:
   un singur proces server, dar intr-un sistem mai mare ar fi si o baza
   de date, un load balancer). Public: echipa tehnica extinsa.
3. **Component** — subsistemele din interiorul unui container (Listener,
   Dispatcher, UserRegistry, ChannelRegistry). Public: echipa de
   dezvoltare directa.
4. **Code** — clasele si functiile efective. Public: cine scrie/mentine
   codul respectiv.

**Concluzia practica:** cand analizezi arhitectura unui proiect nou, treci
explicit prin toate cele patru nivele, in aceasta ordine, inainte sa scrii
prima clasa. Cele doua audituri anterioare au fost scrise, de fapt, direct
la nivelul Component (Listener -> Connection -> LineFramer -> IrcParser ->
CommandDispatcher) — asta nu e intamplator, e nivelul unde se decid cele
mai multe dintre problemele de robustete.

### 5.2 Architecture Decision Records (ADR)

O practica din ce in ce mai raspandita (dar inca departe de universala) e
sa documentezi **fiecare decizie de arhitectura semnificativa** intr-un
fisier scurt, standardizat, in loc sa ramana doar in capul cuiva sau
intr-o discutie de Slack pierduta. Format tipic, foarte scurt:

```text
# ADR 003: Folosim poll() in loc de select()

## Context
select() are limita fixa FD_SETSIZE si scrie in afara bitmap-ului fara
verificare daca fd > limita (vezi C1 din audit).

## Decizie
Folosim exclusiv poll(), cu std::vector<pollfd> care creste dinamic.

## Consecinte
Nu mai exista limita fixa de conexiuni impusa de API-ul de I/O
multiplexing insusi (ramane limita de sistem, ulimit -n, tratata separat
prin MAX_CLIENTS). Codul care ar fi presupus select() (rar, dar posibil
in cod copiat de pe internet) trebuie interzis explicit in code review.
```

**Concluzia practica:** aceasta practica, foarte ieftina (5 minute per
decizie), rezolva o problema reala de echipa: previne "arheologia" —
intrebarea "de ce am facut asa?", pusa peste 3 luni, la care nimeni nu mai
stie raspunsul.

### 5.3 ATAM (Architecture Tradeoff Analysis Method) — analiza de compromisuri

**ATAM**, dezvoltat de Software Engineering Institute (SEI) de la
Carnegie Mellon, e o metoda formala de evaluare a unei arhitecturi prin
identificarea explicita a **compromisurilor** intre atribute de calitate
(performanta, disponibilitate, securitate, mentenabilitate). Ideea centrala,
utila chiar si aplicata informal, solo, pe un proiect mic:

- Orice decizie de arhitectura **imbunatateste cel putin un atribut de
  calitate si inrautateste (sau nu afecteaza) cel putin altul.** Nu exista
  decizii "gratuite".
- Faci explicit acest schimb: "Folosind `std::map` pentru registry
  (`O(log n)`) in loc de `std::vector` cu cautare liniara (`O(n)`), castig
  performanta la cautare, pierd putin la memorie si complexitate de
  implementare, dar nu conteaza la scara acestui proiect."

**Concluzia practica:** pentru fiecare decizie majora de arhitectura, scrii
explicit tabelul "ce castig / ce pierd", chiar daca informal — asta previne
alegerea unei solutii "corecte pe hartie" care de fapt nu se potriveste
constrangerilor reale ale proiectului (vezi si intrebarea 8 din Partea 4).

---

## Partea 6 — Organizare, scriere pe foaie, documentare

### 6.1 Zettelkasten (Niklas Luhmann) — notite atomice, legate intre ele

Sistemul folosit de sociologul Niklas Luhmann pentru a produce un volum
extraordinar de lucrari academice: fiecare notita contine **o singura
idee**, scrisa complet, in propriile cuvinte, cu un identificator unic, si
legaturi explicite catre alte notite inrudite. Diferenta fata de un caiet
obisnuit: nu organizezi notitele pe categorii fixe de la inceput
(ierarhie), le legi organic, pe masura ce descoperi conexiuni.

**Aplicat la programare:** fiecare tehnica din catalogul de algoritmi
devine o notita separata (nu o lista lunga intr-un singur fisier). Cand
inveti o tehnica noua si iti dai seama ca se leaga de una veche (transfer,
vezi 1.7), adaugi explicit legatura intre cele doua notite. Dupa cateva
luni, aceasta retea de notite legate devine harta ta personala de tipare —
mult mai puternica decat orice lista lineara, pentru ca legaturile insele
sunt cunostinta (nu doar continutul notitelor).

### 6.2 Jurnal tehnic (engineering notebook) si decision log

Separat de codul insusi, un jurnal in care, dupa fiecare sesiune de lucru
notezi trei lucruri, in aceasta ordine:

1. **Ce problema am rezolvat, in cuvinte simple** (nu cod).
2. **Ce tehnica am folosit si de ce am ales-o pe aceasta, nu alta** (asta
   forteaza articularea explicita a legaturii simptom -> tehnica de la 2.1).
3. **Ce as face diferit daca as lua-o de la capat** (mini-post-mortem,
   personal, dupa fiecare sesiune, nu doar la final de proiect).

### 6.3 Tehnica Feynman — explici ca sa intelegi

Richard Feynman e cunoscut pentru abordarea: daca nu poti explica un
concept in termeni simpli, nu il intelegi cu adevarat inca. Pasii:

1. Alegi un concept (ex: "erase-remove idiom").
2. Il explici in scris, ca pentru cineva fara experienta, fara jargon.
3. Cand te blochezi sau folosesti un termen pe care nu il poti explica mai
   departe, ai gasit exact golul din intelegerea ta.
4. Te intorci la sursa, umpli golul, si rescrii explicatia mai simplu.

**Aplicat direct la documentele deja create:** documentul cu 15 cazuri
limita a fost scris explicit in acest stil (explicatii pentru "cineva la
inceput de drum") — recomandarea colegului e sa faci acelasi exercitiu tu
insuti, pentru fiecare tehnica noua, nu doar sa citesti explicatia deja
facuta de altcineva.

### 6.4 Rubber duck debugging, extins la design

Tehnica veche si cunoscuta (explici codul, linie cu linie, unei rațe de
cauciuc, iar in procesul de a articula explicatia iti dai singur seama de
bug) se extinde natural la design: **explici arhitectura, decizie cu
decizie, unui ascultator imaginar care intreaba "de ce?" la fiecare pas.**
Daca la un moment dat nu ai un raspuns bun la "de ce ai ales asa?", ai gasit
o decizie neexaminata cu adevarat.

### 6.5 Code katas si code retreats

**Code kata** (termen popularizat de Dave Thomas, dupa analogia cu
katele din artele martiale — exercitii repetate pana la automatism) e o
problema mica, rezolvata repetat, de multe ori, nu ca sa gasesti o solutie
noua, ci ca sa exersezi **executia fluenta** a unei tehnici deja cunoscute
(vezi 1.4, deliberate practice). **Code retreat** (popularizat de Corey
Haines) e o zi intreaga dedicata exclusiv katas, in perechi, stergand
codul dupa fiecare sesiune de 45 de minute — constrangerea de a "arunca"
codul elimina tentatia de a "termina" si forteaza concentrarea pe proces,
nu pe rezultat.

---

## Partea 7 — Unde te documentezi si de unde te inspiri (surse concrete)

### Carti fundamentale (stiinta expertizei si a invatarii)
- *How to Solve It* — George Polya
- *Peak: Secrets from the New Science of Expertise* — Anders Ericsson & Robert Pool
- *Make It Stick: The Science of Successful Learning* — Peter Brown, Henry Roediger, Mark McDaniel
- *Thinking, Fast and Slow* — Daniel Kahneman
- *Sources of Power: How People Make Decisions* — Gary Klein (aici e descris premortem-ul si recunoasterea de tipar la experti reali — pompieri, medici de urgenta)

### Carti fundamentale (inginerie software si arhitectura)
- *A Philosophy of Software Design* — John Ousterhout (complexitate, module profunde vs. superficiale)
- *The Pragmatic Programmer* — David Thomas & Andrew Hunt
- *Software Architecture in Practice* — Len Bass, Paul Clements, Rick Kazman (sursa directa pentru ATAM)
- *Domain-Driven Design* — Eric Evans (util chiar si partial, pentru limbajul de modelare a domeniului)
- *Programming as Theory Building* — Peter Naur (eseu scurt, esential, despre ce inseamna de fapt "a intelege" un program)
- *No Silver Bullet* — Fred Brooks (eseu, despre complexitatea esentiala vs. accidentala)

### Metodologii si rapoarte tehnice formale
- ATAM — rapoartele tehnice publice ale SEI/Carnegie Mellon (cautabile ca
  "SEI ATAM technical report")
- C4 model — documentatia libera de la c4model.com (Simon Brown)
- FMEA — standarde de inginerie de siguranta (originar din industria
  auto/aerospatiala, documentatie publica prin standardele SAE/IEC)

### Practica activa (unde exersezi recunoasterea de tipar)
- Codeforces, LeetCode, Project Euler — pentru exercitiul de "etichetare
  inainte de rezolvare" din 2.3
- *Cracking the Coding Interview* — Gayle Laakmann McDowell — catalog bun
  de tipare, organizat exact pe principiul simptom -> tehnica
- Code retreats locale/online (cautabile dupa "code retreat" + orasul tau)
- Citirea de cod open-source matur (ngIRCd, Redis, SQLite — surse
  renumite pentru cod C robust, citite pentru arhitectura, nu copiate)

### Comunitati si resurse continue
- *Guru of the Week* (GotW) — Herb Sutter — arhiva libera online
- isocpp.org — FAQ si discutii ale comitetului de standardizare
- cppreference.com — sursa primara pentru garantiile exacte ale STL/limbajului

---

## Partea 8 — Programul de 3 luni, saptamana cu saptamana

Structura respecta explicit interleaving-ul (1.5) — fiecare saptamana
amesteca cel putin trei categorii diferite de exercitiu, nu se blocheaza
pe o singura tema.

### Luna 1 — Fundatia: obiceiuri, vocabular, worked examples

**Saptamana 1-2.**
- Zilnic (20-30 min): citesti un caz din C1-C15, apoi **inchizi documentul
  si il rescrii din memorie** (retrieval practice, 1.5), inainte sa
  compari cu originalul.
- Zilnic (10 min): un card nou in harta simptom -> tehnica (2.1).
- De 2-3 ori pe saptamana: un code kata scurt (30-45 min), pe o tehnica
  deja cunoscuta, doar pentru fluenta de executie.
- Incepi jurnalul tehnic (6.2) — o intrare dupa fiecare sesiune, fara
  exceptie, chiar si de 3 randuri.

**Saptamana 3-4.**
- Adaugi exercitiul de etichetare (2.3): 2-3 probleme noi pe saptamana,
  etichetate inainte de rezolvare, comparate dupa.
- Incepi analiza FMEA (3.3) pe o componenta reala din proiectul IRC —
  minim 5 randuri de tabel, pentru o componenta pe care nu ai analizat-o
  inca.
- Prima trecere completa de citire in trei pasi (3.1) pe o specificatie
  noua (poate fi chiar o parte din proiectul IRC pe care nu ai atacat-o
  inca).

### Luna 2 — Recunoastere de tipar si arhitectura

**Saptamana 5-6.**
- Treci de la worked examples complete la probleme partial rezolvate
  (1.3, fading): iei un caz nou, ti se da doar diagnosticul, tu scrii
  fix-ul, apoi compari.
- Scrii primele 3 ADR-uri (5.2) pentru decizii deja luate in proiectul IRC
  (retroactiv — practica la fel de utila ca facuta din timp).
- Faci primul premortem complet (3.2) pentru o parte noua a proiectului,
  minim 10 propozitii de esec.

**Saptamana 7-8.**
- Probleme complet nerezolvate (fading complet): doar simptomul, fara
  diagnostic, tu faci tot procesul de la 2.2 (Polya, 4 pasi).
- Analiza C4 completa (5.1) a proiectului IRC, toate cele 4 nivele, scrisa
  explicit, nu doar gandita.
- Tabelul ATAM (5.3) pentru cel putin doua decizii de arhitectura majore
  deja luate.

### Luna 3 — Integrare, transfer departat, predare

**Saptamana 9-10.**
- Cauti explicit transfer departat (1.7): pentru fiecare din cele 10
  tehnici principale invatate, gasesti un exemplu dintr-un domeniu diferit
  de retea/IRC unde se aplica acelasi principiu.
- Un proiect mic, nou, complet diferit ca domeniu (nu retea) unde aplici
  constient minim 5 tehnici din harta ta simptom -> tehnica.

**Saptamana 11.**
- Aplici tehnica Feynman (6.3) — explici in scris, ca pentru altcineva,
  toate cele 15 cazuri limita, fara sa te uiti la documentul original.
- Rubber duck de arhitectura (6.4): explici, decizie cu decizie, arhitectura
  proiectului IRC unui ascultator imaginar/real.

**Saptamana 12.**
- Post-mortem complet (opusul premortem-ului) pe cele 3 luni: ce a
  functionat, ce nu, ce cauzeaza inca greseli.
- Rescrii, de la zero si din memorie, catalogul de algoritmi (fisierul
  anterior), doar denumirile si contextul, ca test final de retrieval
  (1.5) — compari cu originalul doar la final.

---

## Partea 9 — Rutine de mentinut dupa cele 3 luni (nu se opresc aici)

Cercetarea despre "uitare" (curba lui Ebbinghaus, confirmata de zeci de
ani de replicari) arata clar: fara reluare periodica, orice se invata se
pierde treptat. Rutinele de mentinut, minime dar constante:

- **Saptamanal:** o intrare in jurnalul tehnic, chiar si scurta.
- **Saptamanal:** un card nou sau revizuit in harta simptom -> tehnica.
- **Lunar:** un premortem pe orice proiect nou inceput.
- **Lunar:** un ADR pentru orice decizie de arhitectura semnificativa.
- **Trimestrial:** revizuirea completa, din memorie, a catalogului de
  algoritmi — cele care s-au "tocit" (nu mai vin automat in minte) se
  reintroduc in ciclul de practica deliberata.

---

## Concluzie

Nimic din documentul acesta nu inlocuieste practica efectiva — fiecare
tehnica de mai sus e, prin insasi natura cercetarii din spatele ei
(deliberate practice, 1.4), inutila daca e doar citita si nu exersata cu
feedback si repetitie spatiata. Diferenta reala dintre un programator care
"stie despre" aceste tehnici si unul care le-a transformat in reflex
(System 1, 1.8) e exact volumul de practica deliberata, spatiata si cu
feedback, nu volumul de lectura. Programul din Partea 8 e gandit sa forteze
exact acest lucru — daca il parcurgi doar citind, fara sa faci exercitiile
scrise (jurnal, ADR-uri, premortem, etichetare inainte de rezolvare),
rezultatul va fi cunoastere declarativa ("stiu ca exista tehnica X"), nu
cunoastere procedurala automata ("aplic tehnica X fara sa ma gandesc") —
exact distinctia pe care tot acest document a incercat sa o clarifice de
la inceput.
