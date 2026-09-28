# El servidor IRC en C++98: robustez, casos extremos, diseño y trabajo en equipo

> **Guía Única**, resultado de la fusión de seis documentos escritos por dos colegas
> (auditorías de robustez, catálogo de casos límite, consultas de diseño,
> comparación de las dos demostraciones). No aplica cambios a las fuentes. Contener
> el análisis, las soluciones propuestas (fragmentos de C++98), las pruebas requeridas y las reglas
> trabajo en equipo.
>
> **Para quién:** desarrolladores junior que ingresan a un proyecto de
> comunicaciones de red (TCP/IP, sockets, procesos). Cada problema es
> explicado en el orden: *qué sucede → por qué se agrieta → cómo arreglarlo → cómo probarlo*.
>
> **Convenciones:** código compatible `-std=c++98 -Wall -Wextra -Werror` (no
> `nullptr`, `unique_ptr`, lambda, mover). Los casos tienen identificaciones estables
> ( `T1…` , `L1…` , `P1…` ) y están asignados a los documentos originales en el Apéndice B.

---

## Contenidos

1. Resumen ejecutivo
2. Estado actual: las dos demostraciones
3. Conceptos básicos
4. Cómo encontrar casos límite
5. Arquitectura de destino
6. Catálogo de casos límite
7. El esqueleto integrado del bucle de eventos.
8. Orden de implementación
9. Pruebas y herramientas
10. Trabajo en equipo (dos personas, un repositorio)
11. Catálogo de algoritmos y patrones (C++98 y modernos)
12. Fuentes de documentación
- Apéndice A: Errores y ambigüedades corregidos de los documentos fuente.
- Anexo B: Trazabilidad (documento original → apartado)

---

## 1. Resumen ejecutivo

El prototipo tiene buena separación conceptual (socket, usuario, lexer, AST,
despachador), pero **el transporte TCP es frágil**. Cuestiones prioritarias:

| # | Problema | Consecuencia |
|---|---|---|
| 1 | Un `recv()` tratado como un comando completo | comandos de cortar/pegar, mal interpretados |
| 2 |  `send()` asumió plena | mensajes perdidos silenciosamente |
| 3|  `SIGPIPE` desprotegido | proceso muere en un par cerrado |
| 4|  `User` no suelta el zócalo | pérdida de memoria y fd en cada cliente |
| 5|  `pollfd` /listas modificadas durante la navegación | uso después de la liberación, bloqueo |
| 6| Manejador de señales con STL/ `cout` | comportamiento indefinido |
| 7| Sin límites de entrada/salida/cliente | memoria ilimitada, bucle ocupado |
| 8| Apodo sin validación/singularidad | estado inconsistente |
| 9| El analizador no informa entradas no válidas | AST aparentemente válido debido a una entrada rota |

**Objetivo realista:** un cliente defectuoso u hostil puede desconectarse
controlado, sin afectar a otros clientes y sin bloquearse. El servidor no
promete aceptar cualquier aportación; promete que lo rechaza controlado y que un
el cliente no puede corromper la memoria, bloquear a otros, consumir memoria
ilimitado, perder silenciosamente una salida válida, terminar el proceso ( `SIGPIPE` ) o
para dejar fds/objetos atrás.

### Los cinco principios que solucionan casi todo el catálogo

1. **No modifique una colección a medida que la revisa.** Marque y luego aplique por ( `T7` , `L2` , `L4` , `L6` ).
2. **Un objeto que vive en varias colecciones tiene un único punto de "muerte"**, con un orden de pasos fijo ( `L3` ).
3. **No guarde punteros en varios pasos.** Vuelva a buscar por identificador estable (nombre, fd) y trate "ya no existe" como un caso normal ( `L5` , `L6` ).
4. **Cualquier entrada externa se valida explícitamente**: red, argumentos de inicio, parámetros numéricos ( `P1` , `P5` , `P6` ).
5. **El fallo de un participante no derriba al resto del sistema**: ni a otro cliente ni a todo el proceso ( `T2` , `L5` , `P2` ).

---

## 2. Estado actual: las dos demostraciones

Hay dos direcciones de implementación, cada una con fortalezas complementarias.
(La auditoría `ANALIZA_ROBUSTETE…` hace referencia a una tercera carpeta, `skettch`, con
Misma estructura modular que `another_demo`: `User`, `Socket`, `Server`,
`CommandDispatcher`. Las conclusiones para `another_demo` también se aplican allí;
Las diferencias entre archivos deben ser confirmadas por el equipo.)

| Criterio |  `demo_sources` (integrado) |  `another_demo` (modulares) |
|---|---|---|
| Arquitectura | Monolítico: todo en `Server.cpp` | Lexer → ASTParser → CommandDispatcher |
| E/S |  `poll()`, lista reconstruida en cada iteración |  `poll()`, vector persistente de `pollfd` |
| Encuadre TCP | **Sí**: búfer por socket, `\r\n`, límites 4096/512 | **No**: a `recv()` = un comando |
| Escritura | Búfer de salida, `send()` parcial, `MSG_NOSIGNAL`; **sin límite** | Resultado `send()` ignorado; sin protección `SIGPIPE` |
| Protocolo |  `NICK`, `USER`, `JOIN`, `PING`, numérico `001–004`, `353/366` |  `NICK`, `USER`, `PING` (`PONG:` no estándar), `PRIVMSG` parcial |
| Canales | Sí (miembros, operador inicial, `NAMES`); **los canales vacíos permanecen en el mapa** | No hay |
| Validación | Nick, canal, no. parámetros, estado de registro | Mínimo; los comandos distinguen entre mayúsculas y minúsculas (no se reconoce `nick`) |
| Propiedad | Indicadores crudos, pero ciclo coherente |  `User::~User()` no elimina el zócalo → fuga;  `Socket` copiable → riesgo de doble cierre |
| Construcción estricta | Compilar limpiamente | **No**: parámetros no utilizados `server` + `-Werror` |
| Comprobabilidad | Alto acoplamiento de red |  `ISocket` permite simulacros |
| Extensibilidad | Pesado (cadena de `if`) | Hola (tabla de manejadores) |

**Problemas específicos `demo_sources`:** cambiar el apodo puede
desincronizar índices; producción ilimitada; errores `accept()` diferentes de
`EAGAIN` se ignoran;  `atoi` para puerto;  `JOIN` para un solo canal;
miembros públicos en `User` / `Channel`; estuche plegable aprox.

**Problemas específicos `another_demo`:** falta marco;  `send()` ignorado;
`SIGPIPE`; fuga en el enchufe; manejador de señales inseguro;  `POLLERR/HUP/NVAL`
sin tratar;  `NICK` acepta cualquier cosa;  `USER` incompleto;  `PRIVMSG` al canal
no implementado; Comandos desconocidos sin respuesta numérica.

### Veredicto

- **Para una demostración inmediata:** `demo_sources`.
- **Para desarrollo a largo plazo:** estructura modular de `another_demo`, pero solo después de reparar los cimientos.
- **Receta recomendada:** *modelo de canal y transporte robusto de `demo_sources` + Lexer/Parser/Dispatcher de `another_demo` + propiedad explícita + pruebas de protocolo.*

---

## 3. Conceptos básicos

- **Descriptor de archivo (fd):** entero mediante el cual el núcleo identifica un socket/archivo/tubería. Debe cerrarse **exactamente una vez**: cero veces = fuga, dos veces = falla o cerrar el fd de otra persona.
- **Socket de escucha vs. socket de cliente:** el socket de escucha no lleva mensajes IRC, solo anuncia vía `accept()` que se ha producido una conexión. Cada cliente recibe su propio fd.
`` `texto
socket → setsockopt(SO_REUSEADDR) → enlazar → escuchar → aceptar
recv/enviar… → cerrar
```
- ** `recv()` / `send()` ** funciona con bytes, no con "comandos". Ambos son **parciales**: `recv()` puede dar menos de lo que hay, `send()` puede escribir menos de lo que usted solicitó.
- **TCP es una secuencia, no una secuencia de paquetes.** El cliente puede enviar `PI` + `NG :abc\r\nPING :xyz\r\n` y el servidor puede recibir los mismos datos en cualquier fragmento. Sólo `\r\n` marca el final de un comando IRC.
- **`poll()`** no lee ni escribe. Respuestas: "¿En qué fd vale la pena intentar una operación sin bloqueo ahora?".  `POLLIN` → puedes probar `recv()`;  `POLLOUT` → puedes probar `send()`.
- **IPC:** el mecanismo mediante el cual procesos separados intercambian datos; un socket TCP es un canal IPC.
- **Invalidación del iterador:** modificas un `vector`/`map` a medida que lo revisas y los iteradores/índices dejan de ser válidos → UB (a veces fallan inmediatamente, a veces mucho más tarde).
- **Uso después de liberar/puntero colgante:** utiliza un puntero a un objeto que ya está `delete` -uit.
- **Async-signal-safe:** función invocable de forma segura desde un manejador de señales.  `new`, `malloc`, `std::cout`, cualquier método STL **no** lo son.
- **Contrapresión:** cuando el receptor no consume, el productor debe parar o ser desconectado, no acumular sin cesar.
- **Activado por nivel:** `poll()` informa un fd como "listo" **mientras** la condición persiste, no solo en caso de cambio. Consecuencia: Si no consume el evento, `poll()` regresa inmediatamente, hasta el infinito (bucle ocupado). Recuerde, es la clave de `T6`.

---

## 4. Cómo encontrar casos límite

Para cada función que recibe datos del exterior (cliente, `argv`, otro servidor), haga las mismas seis preguntas:

1. ¿Qué sucede si **faltan datos** (parámetro ausente, búfer vacío)?
2. ¿Qué pasa si hay **demasiados** (una fila enorme, miles de conexiones, mensajes repetidos hasta el infinito)?
3. ¿Qué pasa si tienen **formato incorrecto** (bytes no válidos, NULL, números fuera de rango, texto en lugar de dígitos)?
4. ¿Qué pasa si la operación **sólo se realiza parcialmente** ( `send` / `recv` )?
5. ¿Qué pasa si **dos eventos se superponen** (el cliente se va en medio del procesamiento de otro comando suyo; señal en medio de un cambio de contenedor)?
6. ¿Quién **es** propietario** del recurso y quién puede eliminarlo?

---

## 5. Arquitectura de destino

### 5.1 El gráfico de propiedad

Regla de oro: **un propietario para cada recurso; no mezcle el patrón "Eliminar servidor" con "Eliminar usuario".**

```text
Server
 ├── Listener (fd de listening, non-blocking)
 ├── std::map<int, User*>   ← Server deține User
 │      └── User            ← User deține Connection
 │            └── Connection ← deține Fd (RAII), input buffer, output queue
 ├── UserRegistry (nick normalizat → User*, NON-owning)
 ├── ChannelRegistry (nume normalizat → Channel*, Server deține Channel)
 └── CommandDispatcher (nume comandă → handler)
```

**Variante de destino (limpia):** Transporte e identidad IRC separados.

```text
ClientConnection: socket, input buffer, output queue, stare transport, deadline
UserSession:      nickname, username, realname, hostname, stare autentificare, canale
```

**Variante pragmática (fase 1):** mantienes la clase `User`, pero declaras explícitamente
que posee `Connection`, buffers, estado de registro, canales y estado
cierre. El código de esta guía utiliza la variante pragmática.
(`user.connection()`); la refactorización a la variante de destino es posterior.

### 5.2 Responsabilidades

| Componente | Hacer | **No** hacer |
|---|---|---|
|  `Server` (Fachada + Mediador) | escucha de configuración, bucle de eventos, coordinación de registro, cierre ordenado | no analiza líneas, no llama a `send()`, no valida apodos |
|  `Listener` |  `socket/setsockopt/bind/listen`, sin bloqueo, `accept()` a `EAGAIN` | sin lógica IRC |
|  `Fd` / `Socket` | único propietario de fd ( `fd>=0` ⇒ cerrar) | no copiado |
|  `Connection` | fd, `appendInput/nextLine` (encuadre), `queue/flush` (salida) | no tokeniza, no ejecuta comandos |
|  `IrcParser` | línea completa → `ParseResult{ok,error,…}` | no toca el enchufe |
|  `CommandDispatcher` | normalizar comando, verificar estado de registro, delegar, responder **por `queue()` ** | no llame directamente a `send()` |
|  `UserRegistry` | unicidad, reserva atómica, cambiar nombre, eliminar | – |
|  `ChannelRegistry` / `Channel` | miembros, transmisión, limpieza de canales vacíos | – |

### 5.3 Contratos entre componentes

```cpp
// Transportul produce numai linii IRC complete, fără "\r\n".
Connection::FrameStatus Connection::nextLine(std::string& line);
// Protocolul nu apelează send(): pune răspunsul în coadă.
bool Connection::queue(const std::string& message);
```

Los contratos previenen los dos errores clásicos: el analizador supone que un
`recv()` es un comando completo y el despachador supone que `send()`
envía el mensaje completo.

### 5.4 Ciclo de vida (FSM)

```text
Accepted → Connected → Unregistered (PASS/NICK/USER) → Registered → Closing → Closed
```

Hay **dos tipos de cierre**:

- `markForRemoval(fd)`: error/EOF/ `POLLERR`. Eliminar al final de la iteración, sin escribir más.
- `markForClosing(fd)`: `QUIT`, entrada demasiado larga. Deja de leer, intenta vaciar la cola de salida (por ejemplo, mensaje de despedida) y luego la elimina; con **fecha límite** como cliente no lector para no permanecer en `Closing` indefinidamente.

Ambos son **idempotentes** (dos eventos de cierre para el mismo fd no producen una doble eliminación). El orden de limpieza está en `L3`.

### 5.5 Invariantes para verificar en código y en revisión

```text
fiecare fd activ apare o singură dată în poll set
users_by_nick[normalize(user.nick)] == &user
user.channels conține channel  <=>  channel.members conține user
_input.size() <= MAX_PENDING_INPUT ; _outputBytes <= MAX_OUTPUT_BYTES
nicio ștergere de User/Channel în timpul parcurgerii unei colecții
```

### 5.6 Patrones: obligatorio vs. segunda fase

Consejo del consultor: *no elijas un patrón porque "suena bien"; Elígelo
para el problema exacto en la auditoría.* En un proyecto pequeño, el exceso de ingeniería es un riesgo.

**Obligatorio, en este orden:**

| Patrón | Dónde | Resolver |
|---|---|---|
| RAII + no copiable + Regla de Tres |  `Fd`, `Connection`, `Socket` | propiedad fd, doble cierre, fuga ( `L1` ) |
| Registro (índice) |  `UserRegistry`, `ChannelRegistry` | unicidad de nick/canal, cambio de nombre atómico ( `P4` ) |
| Estados Unidos (FSM) | registro: `CONNECTED → PASS_OK → NICK_SET → USER_SET → REGISTERED` | pedidos en orden incorrecto (`JOIN` antes de `NICK`) |
| Comando | cada comando IRC un controlador, despachador = `map<nume, handler>` |  `if/else` gigantesco |

**Fase dos (especialmente útil para pruebas unitarias sin TCP real):**
Fachada ( `Server` ), Mediador ( `Server` entre `User` y `Channel` ), Estrategia
(validar por comando), Método de fábrica (IRC numérico: `makeNumericError`,
`makeWelcome`), observador (transmisión), adaptador + inyección de dependencia
(`ISocket` simulable), interfaz no virtual, constructor nombrado
( `Connection::fromAcceptedSocket` ), Objeto nulo ( `findChannel()==0` ).

---

## 6. Catálogo de casos límite

### 6.0 Taxonomía

| Categoría | Síntoma típico | Casos |
|---|---|---|
| Transporte / E/S | mensajes perdidos, fallas, bucle ocupado, fallas en pares cerrados |  `T1` – `T8` |
| Ciclo de vida y memoria |  `SIGSEGV`, `SIGABRT`, fuga de fd |  `L1` – `L6` |
| Protocolo y entrada | UB, `terminate()`, estado inconsistente |  `P1` – `P6` |

### 6.1 Constantes comunes (un lugar, `Limits.hpp`)

```cpp
static const std::size_t MAX_IRC_LINE      = 512;    // inclusiv "\r\n" (RFC 2812)
static const std::size_t MAX_PENDING_INPUT = 4096;   // buffer de intrare per conexiune
static const std::size_t MAX_OUTPUT_BYTES  = 65536;  // buffer de ieșire per conexiune
static const std::size_t MAX_CLIENTS       = 512;    // < (ulimit -n) - rezervate, vezi T6
static const std::size_t MAX_CHANNEL_MEMBERS = 200;
static const int         MAX_LINES_PER_TURN  = 32;   // echitate între clienți
```

---

## Transporte y E/S

### T1. Encuadre TCP: un `recv()` no es un comando

**Problema.** `handleClientData()` en `another_demo` entrega el bloque recibido directamente al lexer.
**Consecuencias.** Comandos fragmentados rechazados o mal interpretados; dos comandos en un bloque se convierten en uno; La entrada sin `\r\n` no tiene un comportamiento claro. Además, `std::string(buffer)` después de `recv()` se trunca al primer NUL, porque la longitud válida es el valor devuelto por `recv()`.
**Solución.** Búfer persistente por conexión.  `recv` agrega bytes, el analizador obtiene **solo líneas completas**.

```cpp
// Connection.hpp (extras)
class Connection
{
public:
    enum FrameStatus { FRAME_NEED_MORE, FRAME_LINE, FRAME_TOO_LONG };
    enum IoStatus    { IO_DONE, IO_WOULD_BLOCK, IO_ERROR };

    explicit Connection(int fd) : _fd(fd), _outputBytes(0) {}

    int  fd() const { return _fd.get(); }
    std::size_t inputSpace() const { return MAX_PENDING_INPUT - _input.size(); }
    bool appendInput(const char* data, std::size_t size);
    FrameStatus nextLine(std::string& line);
    bool queue(const std::string& message);   // false => limită depășită
    IoStatus flush();
    bool wantsWrite() const { return _outputBytes > 0; }

private:
    Fd _fd;                                   // RAII, vezi L1
    std::string _input;
    std::deque<std::string> _output;
    std::size_t _outputBytes;

    Connection(const Connection&);            // non-copyable (C++98)
    Connection& operator=(const Connection&);
};
```

```cpp
bool Connection::appendInput(const char* data, std::size_t size)
{
    if (size > MAX_PENDING_INPUT - _input.size())   // fără overflow aritmetic
        return false;
    _input.append(data, size);
    return true;
}

Connection::FrameStatus Connection::nextLine(std::string& line)
{
    std::string::size_type end = _input.find("\r\n");
    if (end == std::string::npos)
        // Fără terminator și deja >= 512 octeți: linia nu mai poate fi validă.
        return (_input.size() >= MAX_IRC_LINE) ? FRAME_TOO_LONG : FRAME_NEED_MORE;
    if (end + 2 > MAX_IRC_LINE)
        return FRAME_TOO_LONG;
    line.assign(_input, 0, end);
    _input.erase(0, end + 2);
    return FRAME_LINE;
}
```

Utilicé un **resultado de tres estados**, no `bool`: "aún no hay línea"
(normal) debe distinguirse de "línea demasiado larga" (error) (consulte el Apéndice A, #1).

> **Decisión de documentar: ¿`\r\n` estricto o tolerante?** RFC requiere `\r\n`, pero
> Las pruebas `nc` / `telnet` a menudo envían solo `\n`. Muchos servidores admiten `\n` y
> elimine cualquier `\r` anterior. Elija explícitamente y escriba
> `Server.hpp`; las pruebas del §9 asumen el comportamiento elegido.

### T2. Línea sin terminador: Agotamiento de la memoria a través de un solo socket

**Escenario.** Un cliente envía bytes al infinito, sin `\r\n`.
El **riesgo real** no es solo el búfer, sino **lo que sucede cuando se excede el límite**: si la respuesta es un `throw` que no se detecta, un solo cliente detiene todo el servidor. Peor aún: si la excepción solo se detecta en el bucle principal y **el estado del cliente permanece sin cambios**, el mismo búfer produce la misma excepción en cada iteración (*píldora venenosa*: registro infinito + 100 % de CPU).

**Solución.** La excepción/error se maneja **en el límite del cliente** y el cliente se marca para su apagado en el mismo lugar.

```cpp
class ProtocolError : public std::runtime_error
{
public:
    explicit ProtocolError(const std::string& m) : std::runtime_error(m) {}
};

void Server::readClient(User& user)
{
    Connection& conn = user.connection();
    std::size_t room = conn.inputSpace();
    if (room == 0)                       // recv(len=0) întoarce 0 = fals EOF!
        return;

    char buffer[4096];
    ssize_t count = ::recv(conn.fd(), buffer,
                           room < sizeof(buffer) ? room : sizeof(buffer), 0);
    if (count == 0)  { markForRemoval(conn.fd()); return; }          // EOF ordonat
    if (count < 0)
    {
        if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
            return;                                                   // T5
        markForRemoval(conn.fd());
        return;
    }
    try
    {
        if (!conn.appendInput(buffer, static_cast<std::size_t>(count)))
            throw ProtocolError("input buffer overflow");
        processLines(user);               // poate arunca ProtocolError (T1: FRAME_TOO_LONG)
    }
    catch (const ProtocolError&)
    {
        conn.queue(makeNumericError(417, "Input line was too long")); // 417 = extensie modernă
        markForClosing(conn.fd());       // din acest moment nu mai citim de la el
    }
    catch (const std::bad_alloc&)
    {
        markForRemoval(conn.fd());
    }
}
```

**Regla general:** un error causado por un cliente se gestiona en la frontera
a ese cliente ( `readClient` ), nunca se le permite subir el bucle
principal. Esto sigue siendo sólo la red final para errores imprevistos ( `P2` ).

### Q3.  `send()` parcial, cliente lento y contrapresión

**Problema.** En el socket sin bloqueo, `send()` solo puede escribir parte o devolver `EAGAIN`. Resultado ignorado ⇒ mensajes perdidos. Cola ilimitada ⇒ un cliente que no lee consume memoria del servidor.
**Solución.** Cola de salida por cliente, con límite;  `POLLOUT` activo **solo** siempre que la cola no esté vacía.

```cpp
bool Connection::queue(const std::string& message)
{
    if (message.size() > MAX_OUTPUT_BYTES - _outputBytes)
        return false;                     // clientul lent va fi deconectat de apelant
    _output.push_back(message);
    _outputBytes += message.size();
    return true;
}

Connection::IoStatus Connection::flush()
{
    while (!_output.empty())
    {
        std::string& cur = _output.front();
        ssize_t sent = ::send(_fd.get(), cur.data(), cur.size(), MSG_NOSIGNAL);
        if (sent > 0)
        {
            _outputBytes -= static_cast<std::size_t>(sent);
            if (static_cast<std::size_t>(sent) == cur.size())
                _output.pop_front();
            else
                cur.erase(0, static_cast<std::size_t>(sent));   // păstrăm restul
            continue;
        }
        if (sent < 0 && errno == EINTR)
            continue;
        if (sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
            return IO_WOULD_BLOCK;        // restul rămâne, așteptăm POLLOUT
        return IO_ERROR;                  // EPIPE, ECONNRESET, altele
    }
    return IO_DONE;
}
```

Elegí **códigos de estado**, no excepciones, para `queue` / `flush` (consulte el Apéndice A,
#9): la persona que llama decide (marca para cerrar) sin error del cliente
para cruzar la pila. El despachador sólo llama a `queue()`, nunca a `send()`.
Para tuberías de salida grandes, reemplace `std::deque<std::string>` con
desplazamiento único `std::string` + para evitar duplicados.

### Q4.  `SIGPIPE` / `EPIPE`

Si el par cerró la conexión exactamente antes de `send()`, el kernel puede enviar `SIGPIPE` y la acción predeterminada **termina el proceso**. Las fuentes recomiendan `MSG_NOSIGNAL`; aquí recomendamos **ambos**, para portabilidad:

```cpp
#ifndef MSG_NOSIGNAL          // macOS/BSD nu îl definesc
# define MSG_NOSIGNAL 0
#endif
// … și la pornire, ignorarea globală (vezi installSignalHandlers în T8):
//    sigaction(SIGPIPE, {SIG_IGN}, 0)
```

Los errores de escritura ( `EPIPE` , `ECONNRESET` ) hacen que el cliente se cierre
respectivamente, no al detener una transmisión ( `L5` ).

### T5. Resultados POSIX, tratados de manera diferente

| Resultado | Significado | Acción |
|---|---|---|
|  `recv > 0` | datos disponibles | añadir en el búfer |
|  `recv == 0` | par cerrado ordenado (EOF) |  `markForRemoval` |
|  `recv < 0`, `EINTR` | interrumpido por señal | intentarlo de nuevo/volver a `poll` |
|  `recv < 0` , `EAGAIN` / `EWOULDBLOCK` | ya no se dan ahora | vuelve a `poll` (**no** está desconectado) |
|  `recv < 0`, de lo contrario error | error permanente |  `markForRemoval` |

Mismas reglas para `send()`. Para `accept()` (recorrido por `EAGAIN`):

```cpp
void Server::acceptPendingConnections()
{
    for (;;)
    {
        if (_users.size() >= MAX_CLIENTS)      // vezi T6: aici NU doar `break`
        {
            pauseListener();
            return;
        }
        int clientFd = ::accept(_listenFd, 0, 0);
        if (clientFd >= 0)  { registerClient(clientFd); continue; } // setează O_NONBLOCK aici
        if (errno == EINTR) continue;
        if (errno == EAGAIN || errno == EWOULDBLOCK) return;
        handleAcceptFailure(errno);            // EMFILE/ENFILE/ECONNABORTED/… vezi T6
        return;
    }
}
```

### T6. Agotamiento de fds ( `EMFILE` / `ENFILE` ) y límite de clientes

**Escenario.** El proceso tiene un límite de fds ( `ulimit -n` , a menudo 1024). Sin su propio límite, `accept()` comienza a devolver `EMFILE`.
**El error en los documentos fuente.** La idea "nos detenemos en `accept()` y dejamos las conexiones en el trabajo pendiente" es solo la mitad de correcta: `poll()` está **activado por nivel**, por lo que el oyente permanece como `POLLIN` mientras haya conexiones en la cola y `poll()` regresa inmediatamente, indefinidamente ⇒ **bucle ocupado con 100 % de CPU**, exactamente lo que se quería evitar (Apéndice A, #3). Además, `MAX_CLIENTS = 1024` igual a `ulimit -n` no llega a: stdin/stdout/stderr, el oyente y cualquier archivo de registro también consumen fds.

**Solución (tres partes).**

1. `MAX_CLIENTS` estrictamente por debajo del límite del proceso (verifica con `getrlimit(RLIMIT_NOFILE)` al inicio y deja caer una reserva de ~16).
2. Cuando se alcance el límite, **elimine `POLLIN` del oyente** ( `pauseListener()` ) y vuelva a colocarlo cuando un cliente se vaya ( `resumeListener()` ). Alternativa simple: `accept()` + enviar `ERROR :Server full` + `close()`.
3. Para `EMFILE`, la técnica clásica *"backup fd"*: mantenga abierto un `open("/dev/null")`; en `EMFILE` lo cierras, haces `accept()` + `close()` (rechazas la limpieza, vacías la cola), luego vuelves a abrir la reserva.  `ECONNABORTED` se ignora y continúa.

### T7. Bucle de eventos: invalidación de listas e interpretación de `revents`

**Problema.** `push_back` puede reasignar `_pollFds`; eliminar el elemento actual mueve los punteros; un controlador puede eliminar el cliente cuyos eventos aún estás procesando.
**Soluciones, en orden de preferencia:**

- **Se reconstruye `pollfd` en cada iteración** (como en `demo_sources`): el vector iterado es su propia copia y aparecen nuevos clientes en la siguiente iteración. El más simple; suficiente para un proyecto educativo.
- **Instantánea** de un vector persistente + eliminaciones/adiciones diferidas.
- **Regla general:** los controladores no eliminan; **marca** ( `markForRemoval` ). La eliminación física ocurre **solo una vez, al final de la iteración** ( `reapDisconnectedUsers` ). Por lo tanto, tampoco se produce *fd reuse* en la misma iteración (un nuevo cliente obtiene el fd de uno cerrado y cualquier evento "antiguo" de la instantánea se le aplica por error).

**Orden correcto de manejo `revents` :**

1. `POLLNVAL` → eliminar (error: fd ya cerrado).
2. `POLLERR` → diagnóstico y eliminación.
3. `POLLIN` → **leer primero**, incluso si se configura `POLLHUP`: un cliente que envía `QUIT` / `PRIVMSG` y se cierra inmediatamente produce `POLLIN|POLLHUP`; Si elimina `POLLHUP` antes de leer, perderá los últimos datos (Apéndice A, #8). El EOF viene de todos modos como `recv()==0`.
4. `POLLHUP` sin `POLLIN` → eliminación.
5. Si el usuario no está marcado, `POLLOUT` → `flush()`.

**Requisito:** el oyente se vacía repitiendo `accept()` hasta `EAGAIN` (ver `T5`).

** `select()` está prohibido** (ex caso `C1`): `FD_SET` no comprueba el límite de `FD_SETSIZE` (normalmente 1024); un fd ≥ 1024 escribe fuera del mapa de bits ⇒ corrupción de pila, que pasa las pruebas con 500 clientes y falla bajo carga real. Usamos `poll()` exclusivamente y documentamos la prohibición en `Server.hpp` (más un `grep -n "select(\|FD_SET" srcs/` en revisión).

### T8. Señales y apagado

**Problema.** El controlador en `another_demo` escribe en `std::cout` y llama a `stop()`: no es seguro para señales asíncronas.

```cpp
#include <csignal>
volatile std::sig_atomic_t g_stop = 0;

extern "C" void handleSignal(int) { g_stop = 1; }   // singura operație permisă

void installSignalHandlers()
{
    struct sigaction sa;
    std::memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handleSignal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;                       // fără SA_RESTART: poll() iese cu EINTR
    sigaction(SIGINT,  &sa, 0);
    sigaction(SIGTERM, &sa, 0);

    struct sigaction ign;
    std::memset(&ign, 0, sizeof(ign));
    ign.sa_handler = SIG_IGN;
    sigemptyset(&ign.sa_mask);
    sigaction(SIGPIPE, &ign, 0);           // vezi T4
}
```

**Normas:**

- `g_stop` se marca **sólo una vez por iteración**, al principio del ciclo; nunca en medio de una función que lógicamente debe ser atómica (por ejemplo, `removeClient` con todos sus pasos).
- `poll()` interrumpido por el retorno de señal `-1/EINTR`: **error no fatal**.
- **Carrera:** si la señal llega entre marcar `g_stop` y entrar en `poll(…, -1)`, el servidor se cuelga hasta el próximo evento. Utilice **tiempo de espera periódico** (por ejemplo, 1000 ms) o self-pipe/ `ppoll` / `eventfd`.
- Apagar en contexto normal: dejar de aceptar → (opcional) mensaje de apagado → cerrar clientes → destruir registros → cerrar oyente. Todos los objetos deben permanecer en un estado destructible e idempotente.

---

## Ciclo de vida y memoria

### L1. Propiedad de sockets y fds

`User` obtiene un `ISocket*` pero no lo elimina ⇒ pérdida de memoria **y** de fd en cada cliente. Por el contrario, si tanto `Server` como `User` cierran el mismo fd ⇒ doble cierre (puede cerrar el fd de otro cliente, ya reasignado por el kernel).

```cpp
class Fd
{
public:
    explicit Fd(int fd) : _fd(fd) {}
    ~Fd() { if (_fd >= 0) ::close(_fd); }
    int  get() const   { return _fd; }
    bool valid() const { return _fd >= 0; }
    int  release()     { int r = _fd; _fd = -1; return r; }   // transfer explicit
private:
    int _fd;
    Fd(const Fd&);                // non-copyable: un singur proprietar
    Fd& operator=(const Fd&);
};
```

- Si `bind()` / `listen()` falla después de `socket()`, el destructor cierra el fd automáticamente.
- `Socket` contiene un `Fd`, no un `int` copiable.
- La decisión ( `Server` → `User` → `Connection` → `Fd` ) está escrita en el encabezado; No mezcle modelos.
- En C++11, un tipo de solo movimiento / `unique_ptr` con eliminador tiene la misma función.

### L2. Eliminar de un contenedor mientras navega

```cpp
// GREȘIT
for (std::map<int, User*>::iterator it = _users.begin(); it != _users.end(); ++it)
    if (shouldDisconnect(it->second))
        _users.erase(it);          // `it` e mort; ++it din for = UB
```

Con `std::vector`, `erase` también **mueve** los elementos posteriores, por lo que los punteros guardados por separado terminan apuntando a otro cliente. **Solución: recopilar y luego eliminar.**

```cpp
void Server::reapDisconnectedUsers()
{
    std::vector<int> toRemove(_pendingRemoval.begin(), _pendingRemoval.end());
    _pendingRemoval.clear();              // _pendingRemoval: std::set<int> => idempotent
    for (std::vector<int>::const_iterator id = toRemove.begin();
         id != toRemove.end(); ++id)
        removeClient(*id);                // un singur punct de ștergere (L3)
}
```

La primera fase no modifica el contenedor; el segundo trabaja en una lista separada y
llama **solo** a la función de limpieza, por lo que la lógica no se duplica (fuente clásica de fugas).

### L3. Un `User` vive en tres estructuras: un único punto de "muerte"

Un `User*` aparece en `_users` (fd → Usuario), en `UserRegistry` (nick → Usuario), y en la lista de miembros de cada `Channel`. Si `removeClient()` solo lo elimina de `_users`, los otros dos quedan con punteros inactivos; el próximo `PRIVMSG` al antiguo nick o la próxima transmisión ⇒ use-after-free.

```cpp
void Server::removeClient(int fd)
{
    std::map<int, User*>::iterator it = _users.find(fd);
    if (it == _users.end())
        return;                                   // idempotent: apel dublu = nimic

    User* user = it->second;

    // 1) Din toate canalele. Copiem lista (removeMember o modifică pe parcurs).
    //    Aici se trimite și QUIT/PART către ceilalți membri: userul încă există.
    std::vector<Channel*> channels = user->channels();
    for (std::size_t i = 0; i < channels.size(); ++i)
        removeMember(*channels[i], *user);

    // 2) Din registry-ul de nickname-uri.
    _userRegistry.forget(*user);

    // 3) Din harta de fd-uri.
    _users.erase(it);

    // 4) Abia acum distrugem obiectul: Connection -> Fd -> ::close(fd).
    delete user;
}
```

**El pedido importa:** paso 1 antes de `delete`, porque `removeMember` más
leer el estado del usuario (apodo del mensaje `PART`). Lo contrario sería un
use-after-free directamente en la función de limpieza.  `delete` no aparece por ningún lado afuera
esta función.

### L4.  `QUIT` seguido de otros comandos en el mismo paquete TCP

El caso más sutil: el cliente envía en un solo paquete
`QUIT :bye\r\nPRIVMSG #c :mai vorbesc?\r\n`. Si el controlador `QUIT` elimina inmediatamente el `User`, la segunda iteración del bucle de línea utiliza un objeto inactivo.
Nunca aparece en las pruebas manuales (escribe los comandos uno a la vez), pero se garantiza que aparecerá con un script.

**Solución:** la desconexión no ocurre "en el acto"; está **marcado** y el bucle verifica la marca **antes de cada línea**.

```cpp
void Server::processLines(User& user)
{
    Connection& conn = user.connection();
    std::string line;
    for (int budget = MAX_LINES_PER_TURN; budget > 0; --budget)
    {
        if (isMarkedForRemoval(conn.fd()) || isMarkedForClosing(conn.fd()))
            return;                       // ce a mai rămas în pachet se ignoră
        Connection::FrameStatus st = conn.nextLine(line);
        if (st == Connection::FRAME_NEED_MORE) return;
        if (st == Connection::FRAME_TOO_LONG)  throw ProtocolError("line too long");
        if (line.empty()) continue;                       // linii goale: ignorate (RFC)
        ParseResult pr = parseLine(line);                 // P3
        if (!pr.ok) { continue; }                         // sau 421, după caz
        _dispatcher.dispatch(pr, user);
    }
    // Budget epuizat: mai pot fi linii în buffer. Ține un flag `hasBacklog`
    // și procesează-le și în iterația următoare, chiar dacă poll() nu mai
    // raportează POLLIN (altfel rămân blocate).
}

void Server::handleQuit(User& user, const std::string& reason)
{
    user.connection().queue(makeQuitNotice(reason));
    markForClosing(user.connection().fd());   // doar marchează; nu șterge nimic acum
}
```

El presupuesto `MAX_LINES_PER_TURN` evita la *hambruna*: sin él, un cliente que
enviar miles de comandos en un paquete monopoliza la iteración.

### L5. Difusión: uso después de la liberación, aislamiento de errores y amplificación de memoria

**(a) Uso después de la liberación.** La iteración directa sobre `members` es riesgosa si algún paso modifica la lista. Repetimos una **copia** de los punteros. Con la eliminación estrictamente diferida ( `L2` / `L4` ), no se produce ninguna eliminación durante la transmisión, por lo que la verificación se reduce a "no verificado"; la instantánea sigue siendo la segunda línea de defensa.

> No verifique la validez de un puntero comparándolo con el registro *por dirección*: después de un `delete`, un nuevo `User` puede obtener la misma dirección (problema ABA) y la verificación pasa al objeto incorrecto. Verifique con **fd/id** o con una marca de eliminación (Apéndice A, #7).

**(b) Aislamiento de errores.** Un `send()` fallido a un miembro no necesita detener la entrega a los demás 49. Por lo tanto, la transmisión simplemente coloca el mensaje en la cola de todos ( `queue()` ), y si la cola de uno está llena, solo ese se marca.

```cpp
void Channel::broadcast(const std::string& msg, const User* except)
{
    std::vector<User*> snapshot = _members;              // copie de pointeri
    for (std::size_t i = 0; i < snapshot.size(); ++i)
    {
        User* member = snapshot[i];
        if (member == except || _server.isMarked(member->connection().fd()))
            continue;
        if (!member->connection().queue(msg))            // T3: limită depășită
            _server.markForRemoval(member->connection().fd());
    }
}
```

**(c) Fan-out.** Un canal con 500 miembros y un mensaje de 400 B
significa 200.000 B asignados de un pedido; límite por cliente
( `MAX_OUTPUT_BYTES` ) se respeta y la memoria total sigue creciendo. Dos soluciones:

- **Simple (recomendado para proyectos escolares):** `MAX_CHANNEL_MEMBERS` + `MAX_CLIENTS`; es fácil de explicar y probar.
- **Avanzado:** mensaje compartido, con recuento de referencias (en C++98 no existe `shared_ptr`):

```cpp
class SharedMessage
{
public:
    explicit SharedMessage(const std::string& text)
        : _text(new std::string(text)), _refs(new long(1)) {}
    SharedMessage(const SharedMessage& o) : _text(o._text), _refs(o._refs) { ++*_refs; }
    ~SharedMessage() { if (--*_refs == 0) { delete _text; delete _refs; } }
    const std::string& text() const { return *_text; }
private:
    std::string* _text;
    long*        _refs;
    SharedMessage& operator=(const SharedMessage&);   // nedefinit
};
```

(La cola almacenaría `SharedMessage` y un desplazamiento por destinatario; no es necesario desde el principio).

### L6. Canales: `KICK` en canales masivos y vacíos

** `KICK #c n1,n2,n3` .** Si `removeMember()` elimina el canal cuando se queda sin miembros y el bucle todavía tiene nicks para procesar, la siguiente iteración utiliza un `Channel` eliminado.
**Soluciones** (una o ambas): (1) **aplazar la eliminación del canal vacío** en la fase de cosecha ( `pruneEmptyChannels()` ); (2) **investigue el canal por nombre** en cada paso:

```cpp
void Server::handleKick(const std::string& channelName,
                        const std::vector<std::string>& nicks)
{
    for (std::size_t i = 0; i < nicks.size(); ++i)
    {
        Channel* channel = findChannel(channelName);   // re-căutare, nu pointer păstrat
        if (!channel)
            break;                                     // "nu mai există" = caz normal
        User* target = findUser(nicks[i]);
        if (!target || !channel->hasMember(*target))
            continue;
        removeMember(*channel, *target);
    }
}
```

**Canales vacíos.** Al cerrar sesión, `demo_sources` elimina al miembro pero lo mantiene.
canal ⇒ la memoria aumenta con los canales temporales. La limpieza debe ser
en un solo lugar:

```cpp
void Server::removeMember(Channel& channel, User& user)
{
    channel.remove(user);
    user.leave(channel);          // invariantul bidirecțional, doar prin metode controlate
    if (channel.memberCount() == 0)
        _channelsToPrune.insert(normalizeIrc(channel.name()));   // ștergere fizică în reap
}
```

`User`/`Channel` no debe exponer a los miembros públicos (`members`, `channels`):
de lo contrario no se puede garantizar el invariante `user.channels ∋ channel ⇔ channel.members ∋ user`.

---

## Protocolo y entrada

### P1. Acceso a parámetros sin verificación

```cpp
// GREȘIT: JOIN\r\n fără canal => params e gol, operator[] nu verifică => UB
std::string channel = params[0];
```

Solución: un asistente usado **en todas partes** en los controladores, nunca `params[i]` directamente, y una respuesta IRC adecuada en lugar de fallar ( `461 ERR_NEEDMOREPARAMS` ).

```cpp
inline bool paramAt(const std::vector<std::string>& params,
                    std::size_t index, std::string& out)
{
    if (index >= params.size()) return false;
    out = params[index];
    return true;
}

void handleJoin(const std::vector<std::string>& params, User& user)
{
    std::string channel;
    if (!paramAt(params, 0, channel))
    {
        user.connection().queue(makeNumericError(461, "JOIN :Not enough parameters"));
        return;
    }
    joinChannel(user, channel);
}
```

El cliente descubre exactamente qué hizo mal y un solo cliente descuidado ya no puede
bloquear el servidor para todos.

### P2. Excepciones de STL no detectadas: `terminate()` mata a todos los clientes

`std::string::at()` y `substr(pos)` con `pos > size()` fundido `std::out_of_range`; enormes concatenaciones pueden arrojar `length_error` / `bad_alloc`. No detectado, desencadena `std::terminate()`: **todo el proceso muere**.
(Nota: `substr(0, 500)` en una cadena más corta **no** arroja; solo `pos > size()` lo hace. El ejemplo original en la fuente era incorrecto, consulte el Apéndice A, #5.)

**Dos capas, ambas requeridas:**

```cpp
// Stratul 1: verifici înainte, în loc să te bazezi pe excepție.
inline std::string safeSubstr(const std::string& s, std::string::size_type pos,
                              std::string::size_type len = std::string::npos)
{
    if (pos > s.size()) return std::string();
    return s.substr(pos, len);
}

// Stratul 2: plasa finală în bucla principală.
void Server::run()
{
    installSignalHandlers();
    while (!g_stop)                        // singurul loc unde se citește g_stop (T8)
    {
        try
        {
            pollOnce();                    // accept + recv + dispatch + reap
        }
        catch (const std::exception& e)
        {
            logSystemError(e.what());      // fără să oprești procesul
        }
    }
    shutdown();
}
```

La capa 1 envía un mensaje IRC amigable a la ubicación del problema; La capa 2 garantiza que
un cliente fallido nunca detiene todo el servidor. **Advertencia:** capa 2
no anula el manejo de límites del cliente ( `T2` ): si es el mismo estado
malo permanece, la misma excepción se repite en cada iteración.

### P3. Analizador y AST con resultado explícito

`ASTParser::peekToken()` solo debe llamarse cuando `_index < _tokens.size()` . Una línea en blanco, un comando faltante o parámetros no válidos no deberían producir un AST que *parezca* válido. Los comandos están normalizados a mayúsculas ( `nick` = `NICK` ).

```cpp
struct ParseResult
{
    bool ok;
    std::string prefix, command, error;
    std::vector<std::string> params;
    ParseResult() : ok(false) {}
};                                         // fără pointeri raw: nimic de eliberat

ParseResult parseLine(const std::string& line)
{
    ParseResult r;
    if (line.empty() || line.find_first_of(std::string("\r\n\0", 3)) != std::string::npos)
    { r.error = "empty or invalid characters"; return r; }

    std::string::size_type i = 0, n = line.size(), start;
    if (line[0] == ':')
    {
        std::string::size_type sp = line.find(' ');
        if (sp == std::string::npos || sp == 1) { r.error = "bad prefix"; return r; }
        r.prefix.assign(line, 1, sp - 1);
        i = sp;
    }
    while (i < n && line[i] == ' ') ++i;
    start = i;
    while (i < n && line[i] != ' ') ++i;
    if (start == i) { r.error = "missing command"; return r; }
    r.command.assign(line, start, i - start);
    for (std::size_t k = 0; k < r.command.size(); ++k)
        if (r.command[k] >= 'a' && r.command[k] <= 'z')
            r.command[k] = static_cast<char>(r.command[k] - 'a' + 'A');

    for (;;)
    {
        while (i < n && line[i] == ' ') ++i;
        if (i >= n) break;
        if (line[i] == ':') { r.params.push_back(line.substr(i + 1)); break; }  // trailing
        start = i;
        while (i < n && line[i] != ' ') ++i;
        r.params.push_back(line.substr(start, i - start));
        if (r.params.size() > 15) { r.error = "too many parameters"; return r; } // RFC 2812
    }
    r.ok = true;
    return r;
}
```

El despachador decide la respuesta: comando desconocido ⇒ `421 ERR_UNKNOWNCOMMAND`;
líneas vacías ⇒ ignoradas.  `PING :tok` responde a `PONG :tok` (no a `PONG: tok`).

### P4. Apodo: validación, unicidad, cambio de nombre atómico

`NICK` comprueba: nulo, longitud máxima (configurable: 9 en RFC 2812, 30 a menudo en la práctica), primer carácter permitido (letra o carácter especial `[]\` _^{|} `), restul (litere, cifre, special, ` -`), unicidad **no distingue entre mayúsculas y minúsculas**. El cambio es atómico a nivel de registro; en caso de falla, la vieja mella permanece intacta. El formulario original se conserva para visualización y el normalizado para búsqueda.

**IRC con mayúsculas y minúsculas** (el `tolower` genérico no lo permite; la dependencia regional tampoco): según RFC 1459/2812, `[ ] \ ~` es mayúscula de `{ } | ^`.

```cpp
std::string normalizeIrc(const std::string& s)
{
    std::string r(s);
    for (std::size_t i = 0; i < r.size(); ++i)
    {
        char c = r[i];
        if (c >= 'A' && c <= 'Z') r[i] = static_cast<char>(c - 'A' + 'a');
        else if (c == '[')  r[i] = '{';
        else if (c == ']')  r[i] = '}';
        else if (c == '\\') r[i] = '|';
        else if (c == '~')  r[i] = '^';
    }
    return r;
}

bool UserRegistry::rename(User& user, const std::string& requested)
{
    if (!isValidNick(requested))
        return false;
    const std::string key = normalizeIrc(requested);

    std::map<std::string, User*>::iterator it = _byNick.find(key);
    if (it != _byNick.end() && it->second != &user)
        return false;                        // ocupat de ALTCINEVA; "Ana" -> "ana" e permis

    const std::string oldKey = user.nick().empty() ? "" : normalizeIrc(user.nick());
    _byNick[key] = &user;                    // 1) rezervă cheia nouă (poate arunca; nimic schimbat încă)
    if (!oldKey.empty() && oldKey != key)
        _byNick.erase(oldKey);               // 2) eliberează cheia veche (nu aruncă)
    user.setNick(requested);                 // 3) actualizează obiectul
    return true;
}
```

El controlador `NICK` no modifica directamente los campos de `Socket`/`User` en algunos lugares
diferente: llama a un único servicio que valida, comprueba la unicidad y
actualizar todos los índices. Invariante: `_byNick[normalize(user.nick)] == &user`.
`ERR_NICKNAMEINUSE` es `433`, `ERR_ERRONEUSNICKNAME` es `432`.

### P5. Parámetros numéricos de comandos ( `MODE #c +l 100` )

`atoi("abc")` da `0` en silencio;  `atoi("999999999999999")` da desbordamiento (UB). Un `0` aceptado puede significar "canal con límite cero", es decir, un canal bloqueado o mal interpretado como "sin límite". Un único envoltorio estricto y reutilizable para límites de puertos, canales, etc.:

```cpp
inline bool parseStrictLong(const std::string& text, long minValue, long maxValue, long& out)
{
    if (text.empty()) return false;
    char* end = 0;
    errno = 0;
    long value = std::strtol(text.c_str(), &end, 10);
    if (*end != '\0')        return false;   // "100abc"
    if (errno == ERANGE)     return false;   // overflow în strtol însuși
    if (value < minValue || value > maxValue) return false;   // regula domeniului
    out = value;
    return true;
}
// MODE +l:  parseStrictLong(arg, 1, 100000, limit)  -> altfel numeric de eroare
//           (696 ERR_INVALIDMODEPARAM e extensie modernă, nu apare în RFC 2812)
```

### P6. Argumentos iniciales

Un servidor "defensor" debe **fallar temprano y claramente**, no iniciarse en un estado ambiguo (`atoi` da `0` ⇒ `bind()` puede elegir un puerto aleatorio).

```cpp
int main(int argc, char** argv)
{
    if (argc != 3)
    { std::cerr << "Utilizare: " << argv[0] << " <port> <parola>\n"; return 1; }

    long port = 0;
    if (!parseStrictLong(argv[1], 1, 65535, port))
    { std::cerr << "Port invalid: " << argv[1] << "\n"; return 1; }
    // (porturile < 1024 cer privilegii; bind() va raporta clar eroarea)

    const std::string password(argv[2]);
    if (password.empty())
    { std::cerr << "Parola nu poate fi goala\n"; return 1; }

    try
    {
        Server server(static_cast<int>(port), password);   // ctor aruncă la eșec socket/bind/listen
        server.run();
    }
    catch (const std::exception& e)
    { std::cerr << "Eroare fatala la pornire: " << e.what() << "\n"; return 1; }
    return 0;
}
```

---

## 7. El esqueleto integrado del bucle de eventos.

Todas las soluciones anteriores juntas (fragmento de referencia, no código final):

```cpp
void Server::pollOnce()
{
    std::vector<struct pollfd> fds = buildPollSet();   // copia locală = snapshot natural
    int rc = ::poll(&fds[0], fds.size(), 1000);        // timeout: T8
    if (rc < 0)
    {
        if (errno == EINTR) return;                    // semnal: nu e eroare
        throw std::runtime_error("poll failed");
    }
    for (std::size_t i = 0; rc > 0 && i < fds.size(); ++i)
    {
        const struct pollfd& ev = fds[i];
        if (ev.revents == 0) continue;

        if (ev.fd == _listenFd) { acceptPendingConnections(); continue; }

        User* user = findUserByFd(ev.fd);
        if (!user || isMarked(ev.fd)) continue;        // deja în ieșire

        if (ev.revents & (POLLNVAL | POLLERR)) { markForRemoval(ev.fd); continue; }
        if (ev.revents & POLLIN)                readClient(*user);       // drenează și la HUP
        else if (ev.revents & POLLHUP)        { markForRemoval(ev.fd); continue; }
        if ((ev.revents & POLLOUT) && !isMarked(ev.fd))
            writeClient(*user);                        // flush(); IO_ERROR => markForRemoval
    }
    reapDisconnectedUsers();      // L2/L3: singura ștergere fizică, la final
    pruneEmptyChannels();         // L6
    resumeListenerIfRoom();       // T6
}

// buildPollSet(): listener (POLLIN, dacă nu e în pauză) + pentru fiecare User:
//   POLLIN (dacă nu e Closing), POLLOUT doar dacă connection().wantsWrite().
```

---

## 8. Orden de implementación

La orden concilia las dos auditorías (primero la fundación; después la funcionalidad):

1. **Propiedad** explícita: `Fd` , `User` , `Socket` ; copia deshabilitada ( `L1` ).
2. **Búfer de entrada persistente** + límites ( `T1` , `T2` ).
3. **Cola de salida** + contrapresión ( `T3` ).
4. Errores explícitos **POSIX**: `EINTR`, `EAGAIN`, EOF, `POLL*`, `accept` (`T5`, `T6`).
5. **Bucle de eventos estable**: marcar + cosechar, ordenar `revents` ( `T7` , `L2` , `L4` ).
6. **Ciclo de vida explícito** y `removeClient` único e idempotente ( `L3` , FSM de §5.4).
7. **Señales** ( `T8` ) y `SIGPIPE` ( `T4` ).
8. **Analizador** con `ParseResult`, comandos que no distinguen entre mayúsculas y minúsculas, respuestas numéricas estándar (`P3`, `P1`).
9. **Registro de apodos** y cambio de nombre atómico (`P4`).
10. **Registro de canales**, transmisión segura, limpieza de canales vacíos ( `L5` , `L6` ).
11. **Números IRC completos** ( `001–004` , `353/366` , `421` , `432` , `433` , `461` ,…).
12. **Pruebas unitarias, de integración y de estrés** (§9), ejecutadas en el mismo PR que el cambio que protege.

---

## 9. Pruebas y herramientas

### 9.1 Matriz de pruebas obligatorias (antes de la integración)

**Encuadre y entrada**
- [ ] `PI` + `NG :abc\r\n` produce un único `PING` ( `T1` ).
- [ ] `PING :a\r\nPING :b\r\n` produce dos comandos ( `T1` ).
- [ ] Una línea incompleta permanece en el búfer, no se olvida en el envío ( `T1` ).
- [] Línea 512 B **con** CRLF compatible; 513 B rechazado;  `\r` al final de un `recv` y `\n` en los siguientes trabajos (`T1`).
- [ ] Bytes infinitos sin terminador: **sólo** ese cliente está desconectado, el resto funciona normalmente ( `T2` ).
- [] La entrada con NUL en el medio no se trunca silenciosamente/se rechaza controlada (`T1`, `P3`).

**Salida y contrapresión**
- [ ] `send()` descanso parcial en cola ( `T3` ).
- [ ] `EAGAIN` no cierra el cliente ( `T3` , `T5` ).
- [ ] Cola de salida excedida ⇒ desconectada controlada, otras no se ven afectadas ( `T3` ).
- [] La transmisión en un canal con un miembro muerto/lento no detiene la transmisión a otros (`L5`).
- [ ] `SIGPIPE` no finaliza el proceso (`T4`).

**Ciclo vital**
- [] EOF elimina el cliente de todos los índices y cierra el fd (`L1`, `L3`).
- [ ] `POLLHUP` con datos pendientes: los datos se procesan y luego se elimina el cliente (`T7`).
- [ ] `QUIT` + otro comando en el mismo paquete: el segundo no se ejecuta, no falla ( `L4` ).
- [ ] Dos clientes hacen `QUIT` en la misma iteración `poll()` (`L2`).
- [] Dos eventos de cierre para el mismo fd: sin doble eliminación ( `L3` ).
- [] Sobre `MAX_CLIENTS`: sin bucle ocupado (`top` muestra una CPU normal), rechazo limpio (`T6`).
- [ ] `ulimit -n` pequeño (ej. 20) + muchas conexiones: el servidor se bloquea (`T6`).
- [ ] `KICK` con lista de canales vaciados: sin acceso al canal eliminado ( `L6` ).
- [ ] Se elimina un canal vacío ( `L6` ).
- [] `PART` del último miembro mientras se transmite otro código (`L5`).

**Protocolo**
- [] `JOIN`, `PART`, `KICK`, `MODE` sin parámetros ⇒ `461`, sin bloqueo (`P1`).
- [ ] Comando en minúsculas ( `nick` ) reconocido; orden desconocido ⇒ `421` ( `P3` ).
- [ ] Línea vacía, solo prefijo, >15 parámetros: rechazado controlado ( `P3` ).
- [] Apodo duplicado rechazado **sin** cambiar el estado existente;  `Ana` → `ana` por el mismo usuario permitido ( `P4` ).
- [] Cambiar el nombre actualiza simultáneamente `User` y el registro (`P4`).
- [ ] `MODE #c +l abc` , `+l -5` , `+l 99999999999999999999` rechazados explícitamente ( `P5` ).
- [] Puerto `abc`, `0`, `70000`, `123abc`, sin contraseña ⇒ mensaje claro, sin socket abierto (`P6`).

**Señales**
- [] `SIGINT` en servidor inactivo (atascado en `poll`) ⇒ apagado en ≤ tiempo de espera (`T8`).
- [ ] `SIGINT` bajo carga ⇒ apagado ordenado, sin fugas ( `T8` ).

### 9.2 Comandos de prueba prácticos

```bash
# Fragmentare: "PI" .. pauză .. "NG :abc"
( printf 'PI'; sleep 1; printf 'NG :abc\r\n'; sleep 1 ) | nc 127.0.0.1 6667
# Două comenzi într-un singur pachet
printf 'PING :a\r\nPING :b\r\n' | nc 127.0.0.1 6667
# QUIT + comandă în același pachet (L4)
printf 'NICK a\r\nUSER a 0 * :A\r\nQUIT :bye\r\nPRIVMSG #c :x\r\n' | nc 127.0.0.1 6667
# Flux fără terminator (T2)
head -c 1000000 /dev/zero | tr '\0' 'A' | nc 127.0.0.1 6667
# Client care nu citește (T3): tine conexiunea deschisă fără să consume
( printf 'NICK slow\r\nUSER s 0 * :S\r\nJOIN #c\r\n'; sleep 600 ) | nc 127.0.0.1 6667
# Leak-uri de memorie ȘI de fd
valgrind --leak-check=full --track-fds=yes ./ircserv 6667 pass
# Ce fd-uri au rămas deschise
lsof -p $(pgrep ircserv)
# Ce syscall-uri se fac efectiv
strace -f -e trace=poll,accept,recv,send,close -p $(pgrep ircserv)
```

(Las opciones de `nc` difieren según las variantes: `-q`, `-N`, `-w`. Verifique `man nc`).

### 9.3 Construcción y análisis

- `-std=c++98 -Wall -Wextra -Werror` en **cada** compilación (para `another_demo`, los parámetros `server` no utilizados deben eliminarse o marcarse como `(void)server;`).
- AddressSanitizer + UBSan (obtenga exactamente uso después de la liberación de `L3` / `L4` / `L6`), Valgrind Memcheck, `cppcheck`, `clang-tidy`.
- Probar con `GoogleTest` / `Catch2` solo si se permiten dependencias; de lo contrario, una autoprueba `main` que utiliza un `ISocket` falso.

---

## 10. Trabajo en equipo: dos personas, un repositorio de GitHub

Propósito: que las dos personas no editen simultáneamente los mismos bloques de
`Server.cpp` , `Server.hpp` o `Makefile` sin coordinación.

### 10.1 División de Responsabilidades

| | Persona A: transporte y ciclo de vida | Persona B: protocolo y estado del IRC |
|---|---|---|
| Archivos |  `Server.cpp/hpp`, `Socket.cpp/hpp`, `Socket.cpp` |  `User`, `Channel`, `Lexer`, `ASTParser`, `CommandDispatcher` |
| Materias | enmarcado, `poll`, `accept`, `recv`, `send`, apagado, propiedad fd (`T*`, `L1` – `L4`) | Validación IRC, apodos, canales, registro, números ( `P*` , `L5` , `L6` ) |

La división no es absoluta. Si ambos necesitan cambiar `Server.cpp`, consulte
establecer el **primer contrato** (§5.3): A expone `queue()` y `nextLine()`, B
los usa sin reescribir el bucle de eventos.

### 10.2 Reglas de rama

```bash
git fetch origin
git switch main
git pull --ff-only origin main
git switch -c feature/transport-framing      # un branch = un scop clar
```

- Una sucursal no es compartida entre dos personas. Confirmaciones pequeñas y compilables:
`fix: preserve fragmented IRC lines`, `fix: bound per-client output queue`,
`test: cover partial non-blocking writes`. Sin `misc`, `changes`, `final`.
- Archivos comunes ( `Makefile` , `main.cpp` , documentación): se anuncia quién los modifica; no combine el reformateo con modificaciones funcionales; nunca `git checkout --` / `git reset --hard` sobre el trabajo del otro.

### 10.3 Orden de integración

1. Publicar el contrato de transporte ( `Connection` , buffer de entrada, cola de salida, señalización de errores) **y sus pruebas**.
2. B escribe el analizador/despachador en el contrato, sin escribir directamente en el socket.
3. Solicitudes de extracción separadas; Primero se integra el transporte y luego el protocolo.
4. Después de cada integración: `make re`.

Si dos cambios son lógicamente dependientes, la segunda persona rebase el PR
primero o inicia la rama desde su confirmación (no copia manualmente el código).
Cada persona sincroniza su rama con `main` **antes** del PR;
Las ramas largas aumentan la probabilidad de conflictos en `Server.cpp`.

### 10.4 Qué pasa en `git push` y cómo el otro toma el relevo

```bash
git push -u origin alex          # publică `alex`, setează tracking; NU modifică main
```

```bash
git fetch origin                                   # descarcă, nu schimbă branch-ul curent
git branch -a ; git log --oneline origin/alex ; git diff origin/main..origin/alex
git switch -c alex --track origin/alex             # branch local din cel remote
git switch alex && git pull --ff-only              # dacă branch-ul local există deja
git status ; git branch --show-current             # verifică mereu pe ce branch ești
```

Ambos **no** se comprometen con la misma rama. para seguir trabajando
al otro: `git switch -c feature/protocol origin/alex` (rama derivada) o
`git cherry-pick <hash>` para una única confirmación; revisar a través de relaciones públicas
(`alex → main`).  `git fetch` sólo trae información;  `git pull` e integrar.

### 10.5 Sincronización y conflictos posteriores a la fusión

```bash
git fetch origin && git switch main && git pull --ff-only origin main
git switch alex && git rebase origin/main          # rebase DOAR pe branch-ul propriu
# la conflict: rezolvi manual doar fișierele indicate, păstrând intenția ambelor părți
git add <fisiere-rezolvate> && git rebase --continue
git diff --check && make re
git push --force-with-lease origin alex            # nu --force: refuză dacă altcineva a publicat între timp
```

Las ramas garantizan **aislamiento** hasta que se fusionan (un `push` no cambia
la rama de la otra, `main` solo se cambia mediante fusión aprobada), pero **no
elimine los conflictos** si ambos modifican las mismas líneas. Reducción de riesgos:
Archivos y responsabilidades predivididos, pequeñas confirmaciones, sincronización.
frecuente, sin reformateo de archivos comunes, contratos comunes primero.
Al realizar la confirmación a partir de ejemplos, prefiera `git add <fișiere>` en lugar de `git add .`.

### 10.6 Revisión obligatoria y lista de verificación de relaciones públicas

Ningún PR se integra sin la verificación de la otra persona:

```text
[ ] Nu există raw pointer owning nou fără justificare scrisă.
[ ] Nu există send() direct din dispatcher.
[ ] Nu se modifică un vector/map în timpul parcurgerii lui.
[ ] Nu există delete de User/Channel în afara punctelor unice (removeClient/prune).
[ ] Erorile POSIX (EINTR, EAGAIN, EOF, POLLHUP, accept) sunt tratate explicit.
[ ] Orice limită nouă are un test.
[ ] Nu apare select()/FD_SET.
[ ] make re (-Wall -Wextra -Werror -std=c++98) și git diff --check trec.
```

---

## 11. Catálogo de algoritmos y patrones.

Es un **índice de reconocimiento**, no una lista de "cosas por hacer ahora": cuándo
Si encuentra un nuevo problema, busque el nombre que describe su forma.

### 11.1 Del problema a la técnica

| Forma del problema | C++98 | Equivalente moderno |
|---|---|---|
| Propiedad de recursos (fd) | RAII, no copiable, Regla de Tres, `auto_ptr` (obsoleto) |  `unique_ptr` con eliminador, solo movimiento |
| "Compruebe si el objeto todavía existe" | Registro + identificador estable |  `weak_ptr` |
| Copia segura para clases con recursos | Copiar e intercambiar | mover semántica |
| Demasiadas rutas de error al analizar |  `ParseResult` |  `std::expected` (C++23), `optional` / `variant` (C++17) |
| Fin del comando en una secuencia | Marco delimitador, ventana corredera |  `string_view` / `span` para procesamiento sin hijos |
| Mover/eliminar durante la iteración | Eliminación en dos fases, instantánea, borrar-eliminar | rangos (C++20) |
| Búsqueda rápida de nick/canal |  `std::map` (O(log n)) |  `unordered_map`, `flat_map` (C++23) |
| Colas de salida | productor-consumidor, `std::deque` | – |
| Limitar el ritmo de pedidos (inundación) | Balde de fichas/balde con fugas | – |
| Cierre de cooperativas |  `volatile sig_atomic_t` |  `std::atomic`, `jthread` + `stop_token` |
| Multiplexación de E/S |  `poll()` (proyecto), `epoll` / `kqueue` (escalado) | Reactor/Proactor, corrutinas, `io_uring` |

### 11.2 Modismos y patrones de C++98 (solo nombre y contexto)

- **Recursos y objetos:** RAII (fd, archivos, mutex) · Regla de tres · Modismo no copiable · Pimpl (recompilar aislamiento) · `std::auto_ptr` · Copiar e intercambiar · Constructor nombrado · Construir en el primer uso · Singleton de Meyers (con cuidado).
- **OOP/polimorfismo:** Interfaz no virtual · Método de plantilla · Estrategia · Comando (despachador) · Estado (grabación FSM) · Observador (difusión) · Visitante/doble despacho (AST transversal) · Adaptador (`ISocket`) · Fachada (`Server`) · Mediador · CRTP · Inversión/inyección de dependencia · Objeto nulo · Grupo de objetos.
- **STL:** borrar-eliminar · `find/find_if` · `sort` /introsort (ej. `NAMES` ) · `binary_search` / `lower_bound` · `transform` (normalización masiva) · `for_each` · functores ( `unary_function` , `mem_fun` , `bind1st/2nd` ) · `std::map/set` (árbol equilibrado) · `std::deque` (`OutputQueue`).
- **Red/sistema:** `select()` (prohibido: `FD_SETSIZE`) · `poll()` · `epoll` / `kqueue` · bucle sin bloqueo con reintento en `EAGAIN/EWOULDBLOCK/EINTR` · bucle de lectura/escritura parcial · bucle de drenaje seguro en los bordes (de `accept` a `EAGAIN`) · cola de productor-consumidor · marca de agua/contrapresión · token/depósito con fugas · retroceso exponencial (si el servidor inicia conexiones) · fd de respaldo para `EMFILE` · self-pipe · controlador seguro de señal asíncrona.
- **Análisis/validación:** FSM explícito (enumeración + modificador) · escaneo de índice/dos punteros · `strtol` estricto · validación a prueba de fallos · limpieza idempotente · gráfico de propiedad (documentado) · iteración de instantáneas · Responsabilidad única.

### 11.3 C++ moderno (C++11 → C++23), direcciones de evolución

El proyecto sigue siendo **C++98**; estas instalaciones no se introducen como dependencias.

- **C++11/14:** semántica de movimiento, `unique_ptr` / `make_unique`, `shared_ptr` / `weak_ptr`, lambdas (genéricas en 14), `std::function` / `std::bind`, plantillas variadas, `auto` / `decltype`, rango para, `thread` / `mutex` / `condition_variable` , `std::atomic` , `nullptr` , `enum class` , `constexpr` , `static_assert` , `std::array` , `std::chrono` , `unordered_map` , delegando constructores, `override` / `final`, literales binarios.
- **C++17:** `optional`, `variant` + `visit`, `string_view`, `any`, enlaces estructurados, `if constexpr`, `filesystem`, expresiones plegables, CTAD, `scoped_lock`, `shared_mutex`, `pmr`, `invoke`. .
- **C++20:** Conceptos, Rangos, Corrutinas, `span`, `jthread` / `stop_token`, `latch` / `barrier`, Módulos, `operator<=>`, calendario/zona horaria.
- **C++23:** `std::expected` , `print` / `format` , `mdspan` , `generator` , `stacktrace` , `flat_map` , `byteswap` , deduciendo `this` , operaciones monádicas.
- **Diseño de alto nivel:** Reactor (≈ bucle `poll()` de este proyecto), Proactor (`io_uring`, IOCP), diseño basado en políticas, borrado de tipos.

---

## 12. Fuentes de documentación

**Estándares y especificaciones**
- RFC 1459, RFC 2810/2811/2812 (protocolo IRC, canales, mensajes, límite 512, numérico), RFC 7194 (IRC sobre TLS). Para comportamiento numérico y de extensión moderno (por ejemplo, `417`, `696`), "Protocolo de cliente IRC moderno" (modern.ircdocs.horse).
- POSIX.1-2008 (Especificaciones básicas del grupo abierto); ISO/IEC 14882:1998 (C++98) y ediciones posteriores.
- *Pautas básicas de C++* (Stroustrup & Sutter), para propiedad, RAII, seguridad excepcional.

**Páginas del manual (lea los códigos de falla exactos, nunca de la memoria)**
, `errno(3)` , `getaddrinfo(3)` , `getrlimit(2)` , `tcp(7)` , `ip(7)` .

**Libros**
- *C++* efectivo, *C++ más efectivo*, *STL efectivo* (Meyers); *C++* excepcional, *Estilo C++ excepcional* (Sutter); *Estándares de codificación C++* (Sutter y Alexandrescu); *Patrones de diseño* (GoF); *Diseño moderno en C++* (Alexandrescu); *El lenguaje de programación C++* (Stroustrup).
- **Red/Sistemas:** *Unix Network Programming vol 1* (Stevens), fuente principal de `socket/poll/select/SIGPIPE` /framing; *Programación Avanzada en Entorno UNIX* (Stevens & Rago), para señales y fds; *TCP/IP Illustrated* (Stevens), por las garantías reales de TCP; *Guía de Beej para la programación de redes* (en línea, gratis; primer lugar para principiantes).

**En línea:** cppreference.com, isocpp.org (Preguntas frecuentes), *Gurú de la semana* (Sutter).

**Herramientas:** GCC/Clang ( `-Wall -Wextra -Werror -std=c++98` ), GDB, Valgrind, AddressSanitizer, UBSan, `strace` , `lsof` , `nc` , `socat` , `telnet` , `clang-tidy` , `cppcheck` , Wireshark/ `tcpdump` , Prueba de Google/Catch2.

**Códigos fuente para estudio de arquitectura (no para copiar):** ngIRCd, InspIRCd, UnrealIRCd. Estudia *cómo separar el transporte del protocolo*; las licencias difieren.

---

## Apéndice A. Errores y ambigüedades corregidos de los documentos fuente

En la fusión verifiqué los fragmentos de código y corregí lo siguiente:

| # | Dónde | Problema | Corrección en esta guía |
|---|---|---|---|
| 1 |  `LineFramer::popLine` (análisis `skettch`) |  `bool` devuelve `false` tanto para "aún no hay línea" como para "línea demasiado larga"; la persona que llama no puede distinguirlos |  `FrameStatus` de tres estados ( `T1` ) |
| 2 |  `readClient` (auditoría de casos límite, §4.3) |  `popLine()` (que puede lanzar `ProtocolError`) estaba fuera del bloque `try`; la excepción llegó a `run()`, el cliente permaneció, la misma línea reprodujo la excepción en cada | iteración de procesamiento de toda la línea en `try`, con `markForClosing` (`T2`) |
| 3|  `MAX_CLIENTS` + `break` (`C7`) | El oyente permanece `POLLIN` (activado por nivel) ⇒ bucle ocupado;  `MAX_CLIENTS=1024` = `ulimit -n`, entonces `EMFILE` viene antes | interrupción en el oyente, límite inferior a `RLIMIT_NOFILE`, fd de respaldo (`T6`) |
| 4|  `assertNoSelectAssumptions` (`C1`) | Función sin efecto, aunque reclama guarda | eliminado; la regla documentada permanece + `grep` en revisión ( `T7` ) |
| 5| Ejemplo `substr(0, 500)` ( `C5` ) |  `substr(0, n)` no lanza; sólo tiro `pos > size()`; el comentario fue confuso | ejemplo corregido y explicación ( `P2` ) |
| 6|  `SharedMessage` (`C9`) | El contador comenzó desde 0 con la prueba `< 0`: funciona pero es inusual | contador iniciado desde 1, `== 0` (`L5`) |
| 7|  `isStillMember(User*)` (`C3` / `C8`) | La validación del puntero de dirección sufre de ABA después de `delete` | comprobar por fd/mark + eliminación diferida ( `L5` ) |
| 8| Solicitar `POLLHUP` (auditoría + consulta) | Al eliminar `POLLHUP` antes de que `POLLIN` pierda los últimos datos (`QUIT`, `PRIVMSG`) | leer primero y luego eliminar (`T7`) |
| 9|  `flush()` con `throw SocketError` frente a `queue()` con `throw SlowClient` | Excepciones de la ruta activa, tratadas de manera diferente en tres lugares | códigos de estado + `markFor…` a la persona que llama (`T3`) |
| 10 |  `UserRegistry::rename` | Rechaza sólo el cambio de mayúsculas del propio nick ( `Ana` → `ana` ); eliminar la clave anterior antes de la nueva | comprobar `owner != &user`, insertar y luego borrar (`P4`) |
| 11 | Estuche plegable | La lista del análisis omitió el par `~` ↔ `^` | tabla completa RFC 1459 ( `P4` ) |
| 12 | Numérico `417` , `696` | Presentado como "estándar", pero no aparece en RFC 2812 | marcados como extensiones modernas |
| 13 |  `MSG_NOSIGNAL` | No portátil (macOS/BSD) |  `#ifndef` + `SIGPIPE` global ignorado ( `T4` ) |
| 14 |  `recv(fd, buf, room, 0)` | Si `room == 0` devuelve `0` = falso EOF | guardia explícita ( `T2` ) |
| 15 | Terminador de línea | Los documentos imponen estrictamente `\r\n`;  `nc` enviar `\n` | decisión a documentar ( `T1` ) |
| 16 |  `PONG: token` (`another_demo`) | Formato no estándar |  `PONG :token` (`P3`) |
| 17 | Enlaces | Referencias a rutas absolutas locales (`/home/alexkatz/...`) | eliminado; los archivos se nombran relativos |

## Apéndice B. Trazabilidad

Cada uno de los dos colegas produjo un conjunto de archivos superpuestos. Cómo se fusionaron:

| Documento original | Contenido | Dónde encontrar |
|---|---|---|
|  `AUDIT_ROBUSTETE_SI_AJUSTARI.md` | veredicto comparativo, ajustes 1 a 10 (RAII, marcos, cola de escritura, POSIX, bucle de eventos, apodo, canales, señales, analizador), flujo de trabajo de Git | §2, §5, `T1` – `T8`, `L1`, `L6`, `P3`, `P4`, §10 |
|  `ANALIZA_ROBUSTETE_SI_IMPLEMENTARE.md` (auditoría `skettch`) | modelo mental, mismos problemas, orden de implementación, pruebas | §3, §6, §8, §9 (se mantiene la variante más detallada, se elimina la redundante) |
|  `AUDIT_CAZURI_LIMITA_SI_IMPLEMENTARE.md` | casos `C1` – `C15`, implementación consolidada, pruebas adicionales |  `T2`, `T6` – `T8`, `L2` – `L6`, `P1`, `P2`, `P5`, `P6`, §6.1, §9 |
|  `CONSULTANTA_DESIGN_SI_ALGORITMI.md` | tabla de patrones → problema, fuentes, catálogo C++98 / moderno | §5.6, §11, §12 |
|  `CONSULTARE_DESIGN_ALGORITMI_CPP.md` | diseño de clases, gráfico de propiedad, ciclo de vida, algoritmos, trampas | §5, `T7`, `T8`, §11 |
|  `COMPARATIE_DEMO_IRC.md` | Comparación de `demo_sources` vs `another_demo`, recomendación final | §2 |

Correspondencia de identificación: `C1` → `T7`, `C2` → `L2`, `C3` → `L5`, `C4` → `P1`, `C5` → `P2`, `C6` → `T2`, `C7` → `T6`, `C8` → `L5`, `C9` → `L5`, `C10` → `L3`, `C11` → `L4`, `C12` → `P5`, `C13` → `L6`, `C14` → `P6`, `C15` → `T8`.
