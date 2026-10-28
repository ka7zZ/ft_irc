// socket_api_demo.cpp
// Server TCP minimal, ROBUST, C++98, care foloseste toate apelurile din ghid:
//   socket, setsockopt, fcntl, inet_pton, bind, listen, getsockname,
//   accept, inet_ntop, poll, recv, send, shutdown, close, sigaction.
//
// Compilare:  g++ -std=c++98 -Wall -Wextra -Werror socket_api_demo.cpp -o demo
// Rulare:     ./demo <port|0> [ip-de-bind=127.0.0.1] [sndbuf-de-test=0]
// Protocol:   fiecare linie primita (terminata cu \n sau \r\n) este ecoata ca
//             "ECHO: <linie>\r\n". "QUIT" inchide ordonat. Linie > 512 => eroare.

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <poll.h>
#include <fcntl.h>
#include <unistd.h>

#include <csignal>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <map>
#include <new>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#ifndef MSG_NOSIGNAL          // macOS/BSD nu il definesc
# define MSG_NOSIGNAL 0
#endif

static const std::size_t MAX_LINE    = 512;     // inclusiv terminatorul
static const std::size_t MAX_IN      = 4096;
static const std::size_t MAX_OUT     = 65536;
static const std::size_t MAX_CLIENTS = 64;

static volatile std::sig_atomic_t g_stop = 0;
extern "C" void onSignal(int) { g_stop = 1; }   // singura operatie permisa

// ---------------------------------------------------------------- RAII fd
class Fd
{
public:
    explicit Fd(int fd) : _fd(fd) {}
    ~Fd() { if (_fd >= 0) ::close(_fd); }
    int get() const { return _fd; }
    int release() { int r = _fd; _fd = -1; return r; }
private:
    int _fd;
    Fd(const Fd&);
    Fd& operator=(const Fd&);
};

static bool setNonBlocking(int fd)
{
    int flags = ::fcntl(fd, F_GETFL, 0);            // citeste flag-urile existente
    if (flags < 0)
        return false;
    return ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;   // adauga, nu suprascrie
}

static bool parseStrictLong(const char* text, long lo, long hi, long& out)
{
    if (*text == '\0')
        return false;
    char* end = 0;
    errno = 0;
    long v = std::strtol(text, &end, 10);
    if (*end != '\0' || errno == ERANGE || v < lo || v > hi)
        return false;
    out = v;
    return true;
}

// ------------------------------------------------------------- listener
static int createListener(int port, const char* ip)
{
    int raw = ::socket(AF_INET, SOCK_STREAM, 0);
    if (raw < 0)
        throw std::runtime_error(std::string("socket: ") + std::strerror(errno));
    Fd guard(raw);                                   // inchis automat la orice throw

    int one = 1;
    if (::setsockopt(raw, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one)) < 0)
        throw std::runtime_error(std::string("setsockopt: ") + std::strerror(errno));

    struct sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));             // zeroizeaza si sin_zero
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(static_cast<unsigned short>(port));   // host -> network
    int pr = ::inet_pton(AF_INET, ip, &addr.sin_addr);
    if (pr != 1)                                     // 0 = format invalid, -1 = eroare
        throw std::runtime_error(std::string("adresa IP invalida: ") + ip);

    if (::bind(raw, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0)
        throw std::runtime_error(std::string("bind: ") + std::strerror(errno));
    if (::listen(raw, SOMAXCONN) < 0)
        throw std::runtime_error(std::string("listen: ") + std::strerror(errno));
    if (!setNonBlocking(raw))
        throw std::runtime_error(std::string("fcntl: ") + std::strerror(errno));

    return guard.release();                          // transfer explicit de ownership
}

static int boundPort(int fd)
{
    struct sockaddr_in bound;
    socklen_t len = sizeof(bound);                   // value-result: initializat!
    if (::getsockname(fd, reinterpret_cast<struct sockaddr*>(&bound), &len) < 0)
        return -1;
    return ntohs(bound.sin_port);
}

// --------------------------------------------------------------- client
struct Client
{
    Fd          fd;
    std::string in, out, peer;
    bool        closing;
    explicit Client(int f) : fd(f), closing(false) {}
private:
    Client(const Client&);
    Client& operator=(const Client&);
};

typedef std::map<int, Client*> ClientMap;

class Server
{
public:
    Server(int listenFd, int sndbuf) : _listen(listenFd), _sndbuf(sndbuf) {}
    ~Server()
    {
        for (ClientMap::iterator it = _clients.begin(); it != _clients.end(); ++it)
            delete it->second;
    }

    void run()
    {
        while (!g_stop)
        {
            std::vector<struct pollfd> fds;
            if (_clients.size() < MAX_CLIENTS)       // plin => listenerul iese din poll set
            {
                struct pollfd p;
                p.fd = _listen.get(); p.events = POLLIN; p.revents = 0;
                fds.push_back(p);
            }
            for (ClientMap::iterator it = _clients.begin(); it != _clients.end(); ++it)
            {
                struct pollfd p;
                p.fd = it->first;
                p.events = 0;
                if (!it->second->closing)      p.events |= POLLIN;
                if (!it->second->out.empty())  p.events |= POLLOUT;
                p.revents = 0;
                fds.push_back(p);
            }
            if (fds.empty()) { ::poll(0, 0, 1000); continue; }

            int rc = ::poll(&fds[0], fds.size(), 1000);      // timeout: nu ramane blocat
            if (rc < 0)
            {
                if (errno == EINTR) continue;                // semnal: verifica g_stop
                throw std::runtime_error(std::string("poll: ") + std::strerror(errno));
            }
            for (std::size_t i = 0; rc > 0 && i < fds.size(); ++i)
            {
                const struct pollfd& ev = fds[i];
                if (ev.revents == 0) continue;
                if (ev.fd == _listen.get()) { acceptAll(); continue; }

                ClientMap::iterator it = _clients.find(ev.fd);
                if (it == _clients.end() || _dead.count(ev.fd)) continue;
                Client* c = it->second;

                if (ev.revents & (POLLNVAL | POLLERR)) { _dead.insert(ev.fd); continue; }
                if ((ev.revents & POLLIN) && !c->closing) readClient(ev.fd, *c);
                else if ((ev.revents & POLLHUP) && !(ev.revents & POLLIN))
                { _dead.insert(ev.fd); continue; }
                if ((ev.revents & POLLOUT) && !_dead.count(ev.fd)) flushClient(ev.fd, *c);
            }
            reap();                                          // singura stergere fizica
        }
    }

private:
    Fd                 _listen;
    int                _sndbuf;
    ClientMap          _clients;
    std::set<int>      _dead;

    void acceptAll()
    {
        for (;;)
        {
            if (_clients.size() >= MAX_CLIENTS) return;
            struct sockaddr_in peer;
            socklen_t plen = sizeof(peer);
            int cfd = ::accept(_listen.get(), reinterpret_cast<struct sockaddr*>(&peer), &plen);
            if (cfd < 0)
            {
                if (errno == EINTR || errno == ECONNABORTED) continue;
                if (errno == EAGAIN || errno == EWOULDBLOCK) return;    // coada golita
                std::perror("accept");                                  // EMFILE, ENFILE, ...
                return;
            }
            Fd guard(cfd);
            if (!setNonBlocking(cfd)) continue;      // accept() NU garanteaza mostenirea O_NONBLOCK
            if (_sndbuf > 0)
                ::setsockopt(cfd, SOL_SOCKET, SO_SNDBUF, &_sndbuf, sizeof(_sndbuf));

            Client* c = 0;
            try { c = new Client(cfd); }
            catch (const std::bad_alloc&) { continue; }             // guard inchide cfd
            guard.release();                                        // acum il detine Client

            char ip[INET_ADDRSTRLEN] = "?";
            if (plen >= sizeof(struct sockaddr_in) && peer.sin_family == AF_INET)
                ::inet_ntop(AF_INET, &peer.sin_addr, ip, sizeof(ip));
            char label[64];
            std::snprintf(label, sizeof(label), "%s:%d", ip, ntohs(peer.sin_port));
            c->peer = label;
            _clients[cfd] = c;
            std::cerr << "connect " << c->peer << "\n";
        }
    }

    bool queue(Client& c, const std::string& msg)
    {
        if (msg.size() > MAX_OUT - c.out.size()) return false;      // backpressure
        c.out += msg;
        return true;
    }

    void readClient(int fd, Client& c)
    {
        std::size_t room = MAX_IN - c.in.size();
        if (room == 0) { _dead.insert(fd); return; }                // recv(len=0) ar da fals EOF
        char buf[1024];
        ssize_t n = ::recv(fd, buf, room < sizeof(buf) ? room : sizeof(buf), 0);
        if (n == 0) { _dead.insert(fd); return; }                   // EOF ordonat
        if (n < 0)
        {
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) return;
            _dead.insert(fd);                                       // ECONNRESET etc.
            return;
        }
        c.in.append(buf, static_cast<std::size_t>(n));              // exact n octeti (NUL-safe)

        for (;;)
        {
            std::string::size_type nl = c.in.find('\n');
            if (nl == std::string::npos)
            {
                if (c.in.size() >= MAX_LINE) tooLong(c);
                break;
            }
            if (nl + 1 > MAX_LINE) { tooLong(c); break; }
            std::string line = c.in.substr(0, nl);
            c.in.erase(0, nl + 1);
            if (!line.empty() && line[line.size() - 1] == '\r') line.erase(line.size() - 1);
            if (line == "QUIT") { queue(c, "BYE\r\n"); c.closing = true; break; }
            if (!queue(c, "ECHO: " + line + "\r\n")) { _dead.insert(fd); return; }
        }
        flushClient(fd, c);                          // trimitere optimista, fara sa asteptam POLLOUT
    }

    void tooLong(Client& c)
    {
        queue(c, "ERROR :line too long\r\n");
        c.closing = true;
        c.in.clear();
    }

    void flushClient(int fd, Client& c)
    {
        while (!c.out.empty())
        {
            ssize_t n = ::send(fd, c.out.data(), c.out.size(), MSG_NOSIGNAL);
            if (n > 0) { c.out.erase(0, static_cast<std::size_t>(n)); continue; }
            if (n < 0 && errno == EINTR) continue;
            if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return;   // asteapta POLLOUT
            _dead.insert(fd);                                                 // EPIPE, ECONNRESET...
            return;
        }
        if (c.closing)
        {
            ::shutdown(fd, SHUT_WR);                 // FIN dupa ce tot output-ul a plecat
            _dead.insert(fd);
        }
    }

    void reap()
    {
        for (std::set<int>::iterator id = _dead.begin(); id != _dead.end(); ++id)
        {
            ClientMap::iterator it = _clients.find(*id);
            if (it == _clients.end()) continue;
            std::cerr << "disconnect " << it->second->peer << "\n";
            delete it->second;                       // ~Fd => ::close(fd)
            _clients.erase(it);
        }
        _dead.clear();
    }
};

int main(int argc, char** argv)
{
    long port = 0, sndbuf = 0;
    if (argc < 2 || argc > 4
        || !parseStrictLong(argv[1], 0, 65535, port)
        || (argc == 4 && !parseStrictLong(argv[3], 0, 1 << 20, sndbuf)))
    {
        std::cerr << "Utilizare: " << argv[0] << " <port|0> [ip=127.0.0.1] [sndbuf=0]\n";
        return 1;
    }
    const char* ip = (argc >= 3) ? argv[2] : "127.0.0.1";

    struct sigaction sa;
    std::memset(&sa, 0, sizeof(sa));
    sa.sa_handler = onSignal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;                                 // fara SA_RESTART: poll() iese cu EINTR
    sigaction(SIGINT, &sa, 0);
    sigaction(SIGTERM, &sa, 0);
    struct sigaction ign;
    std::memset(&ign, 0, sizeof(ign));
    ign.sa_handler = SIG_IGN;
    sigemptyset(&ign.sa_mask);
    sigaction(SIGPIPE, &ign, 0);

    try
    {
        int lfd = createListener(static_cast<int>(port), ip);
        std::cout << "LISTENING " << boundPort(lfd) << std::endl;   // flush pt. scripturi de test
        Server server(lfd, static_cast<int>(sndbuf));
        server.run();
    }
    catch (const std::exception& e)
    {
        std::cerr << "Eroare fatala: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
