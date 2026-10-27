#ifdef PLATFORM_MACOS
#include "PfBackend.h"
#include "core/PacketUtils.h"
#include "app/Logger.h"

#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/resource.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <unistd.h>

#include <QCoreApplication>
#include <QDir>
#include <QProcess>
#include <QRegularExpression>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <thread>

// -----------------------------------------------------------------------
// Shared state.  Held by the worker, the backend and every connection
// thread, so it outlives whichever of them finishes last.
// -----------------------------------------------------------------------
struct PfShared {
    BypassConfig cfg;
    std::atomic<bool>    stop{false};
    std::atomic<int>     active{0};
    std::atomic<quint64> conns{0};
    std::atomic<quint64> bytes{0};
    int pfFd      = -1;      // /dev/pf, used for DIOCNATLOOK
    int dnsFd     = -1;      // UDP listener; replies must leave from this socket
    int wakeFd[2] = {-1, -1}; // self-pipe to wake poll() on stop

    ~PfShared() {
        for (int fd : {pfFd, dnsFd, wakeFd[0], wakeFd[1]})
            if (fd >= 0) ::close(fd);
    }

    void requestStop() {
        stop = true;
        if (wakeFd[1] >= 0) {
            const char b = 1;
            const ssize_t w = ::write(wakeFd[1], &b, 1);
            (void)w;
        }
    }
};

namespace {

// Our pf rules live in "com.apple/250.PktSwerve.<pid>".  "com.apple/*" is
// already evaluated by the stock /etc/pf.conf, so the system ruleset is never
// edited; the pid suffix lets a new instance tell its rules from a dead one's.
const QString kAnchorParent = QStringLiteral("com.apple");
const QString kAnchorName   = QStringLiteral("250.PktSwerve");
const QString kPfctl        = QStringLiteral("/sbin/pfctl");

constexpr int kMaxConnections = 800;   // two threads and two fds each
constexpr int kConnectTimeoutMs = 10000;

// -----------------------------------------------------------------------
// pf: original destination lookup
// -----------------------------------------------------------------------
// net/pfvar.h is not part of the public macOS SDK, so the layout of
// struct pfioc_natlook (xnu bsd/net/pfvar.h) is mirrored here.
struct PfAddr {
    union {
        in_addr  v4;
        in6_addr v6;
        uint8_t  addr8[16];
        uint16_t addr16[8];
        uint32_t addr32[4];
    } pfa;
};
union PfXport {
    uint16_t port;
    uint16_t call_id;
    uint32_t spi;
};
struct PfNatLook {
    PfAddr    saddr, daddr, rsaddr, rdaddr;
    PfXport   sxport, dxport, rsxport, rdxport;
    sa_family_t af;
    uint8_t   proto;
    uint8_t   proto_variant;
    uint8_t   direction;
};
static_assert(sizeof(PfNatLook) == 84, "pfioc_natlook layout drifted; DIOCNATLOOK would be wrong");

constexpr uint8_t kPfOut = 2;                       // enum { PF_INOUT, PF_IN, PF_OUT }
#define PKT_DIOCNATLOOK _IOWR('D', 23, PfNatLook)

// `client` is the redirected peer, `local` the proxy socket it reached.
// On success `orig` holds the address the application really dialled.
bool natLookup(int pfFd, int proto, const sockaddr_in& client,
               const sockaddr_in& local, sockaddr_in& orig)
{
    PfNatLook nl{};
    nl.af        = AF_INET;
    nl.proto     = static_cast<uint8_t>(proto);
    nl.direction = kPfOut;
    nl.saddr.pfa.v4 = client.sin_addr;
    nl.sxport.port  = client.sin_port;
    nl.daddr.pfa.v4 = local.sin_addr;
    nl.dxport.port  = local.sin_port;

    if (::ioctl(pfFd, PKT_DIOCNATLOOK, &nl) != 0)
        return false;

    orig = {};
    orig.sin_len    = sizeof(orig);
    orig.sin_family = AF_INET;
    orig.sin_addr   = nl.rdaddr.pfa.v4;
    orig.sin_port   = nl.rdxport.port;
    return true;
}

// -----------------------------------------------------------------------
// Small socket helpers
// -----------------------------------------------------------------------
struct Fd {
    int fd;
    explicit Fd(int f = -1) : fd(f) {}
    ~Fd() { if (fd >= 0) ::close(fd); }
    Fd(const Fd&) = delete;
    Fd& operator=(const Fd&) = delete;
};

void setNoSigPipe(int fd) { int on = 1; ::setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof(on)); }
void setNoDelay(int fd)   { int on = 1; ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &on, sizeof(on)); }
void setCloexec(int fd)   { ::fcntl(fd, F_SETFD, FD_CLOEXEC); }
void setBlocking(int fd, bool blocking)
{
    const int fl = ::fcntl(fd, F_GETFL, 0);
    ::fcntl(fd, F_SETFL, blocking ? (fl & ~O_NONBLOCK) : (fl | O_NONBLOCK));
}

QString ipString(const sockaddr_in& a)
{
    char buf[INET_ADDRSTRLEN] = {};
    ::inet_ntop(AF_INET, &a.sin_addr, buf, sizeof(buf));
    return QString("%1:%2").arg(QString::fromLatin1(buf)).arg(ntohs(a.sin_port));
}

int makeListener(int type, int port, QString* error)
{
    const int fd = ::socket(AF_INET, type, 0);
    if (fd < 0) {
        *error = QString("socket() failed: %1").arg(QString::fromLocal8Bit(strerror(errno)));
        return -1;
    }
    int on = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

    sockaddr_in a{};
    a.sin_len         = sizeof(a);
    a.sin_family      = AF_INET;
    a.sin_port        = htons(static_cast<uint16_t>(port));
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    if (::bind(fd, reinterpret_cast<sockaddr*>(&a), sizeof(a)) < 0 ||
        (type == SOCK_STREAM && ::listen(fd, 256) < 0)) {
        *error = QString("Cannot listen on 127.0.0.1:%1 (%2)")
                     .arg(port).arg(QString::fromLocal8Bit(strerror(errno)));
        ::close(fd);
        return -1;
    }
    setBlocking(fd, false);
    setCloexec(fd);
    setNoSigPipe(fd);
    return fd;
}

// Connects to `dst` from a root-owned socket.  pf skips root's traffic
// (see buildRules), so the proxy's own connections are never redirected
// back into itself.
int connectTo(const sockaddr_in& dst, int timeoutMs)
{
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    setNoSigPipe(fd);
    setNoDelay(fd);
    setCloexec(fd);
    setBlocking(fd, false);

    int r = ::connect(fd, reinterpret_cast<const sockaddr*>(&dst), sizeof(dst));
    if (r < 0 && errno != EINPROGRESS) { ::close(fd); return -1; }
    if (r < 0) {
        pollfd p{fd, POLLOUT, 0};
        int pr;
        do { pr = ::poll(&p, 1, timeoutMs); } while (pr < 0 && errno == EINTR);
        int soErr = 0;
        socklen_t len = sizeof(soErr);
        if (pr <= 0 || ::getsockopt(fd, SOL_SOCKET, SO_ERROR, &soErr, &len) < 0 || soErr != 0) {
            ::close(fd);
            return -1;
        }
    }
    setBlocking(fd, true);
    return fd;
}

bool sendAll(int fd, const char* data, size_t len)
{
    while (len > 0) {
        const ssize_t w = ::send(fd, data, len, 0);
        if (w < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        data += w;
        len  -= static_cast<size_t>(w);
    }
    return true;
}

// Writes `data` as separate send() calls cut at the given offsets.  With
// TCP_NODELAY set every call leaves as its own segment.
bool sendChunks(int fd, const QByteArray& data, QList<int> cuts)
{
    std::sort(cuts.begin(), cuts.end());
    int start = 0;
    for (int cut : std::as_const(cuts)) {
        if (cut <= start || cut >= data.size()) continue;
        if (!sendAll(fd, data.constData() + start, static_cast<size_t>(cut - start)))
            return false;
        start = cut;
    }
    return sendAll(fd, data.constData() + start, static_cast<size_t>(data.size() - start));
}

// Copies one direction until EOF, error or shutdown.  A clean EOF is passed
// on as a half-close so the other direction can finish; anything else tears
// both sockets down so the sibling pump wakes up.
void pump(PfShared& sh, int from, int to)
{
    char buf[16384];
    bool clean = false;
    while (!sh.stop) {
        pollfd p{from, POLLIN, 0};
        const int r = ::poll(&p, 1, 500);
        if (r < 0) { if (errno == EINTR) continue; break; }
        if (r == 0) continue;
        const ssize_t n = ::recv(from, buf, sizeof(buf), 0);
        if (n == 0) { clean = true; break; }
        if (n < 0) { if (errno == EINTR || errno == EAGAIN) continue; break; }
        if (!sendAll(to, buf, static_cast<size_t>(n))) break;
        sh.bytes += static_cast<quint64>(n);
    }
    if (clean) {
        ::shutdown(to, SHUT_WR);
    } else {
        ::shutdown(from, SHUT_RDWR);
        ::shutdown(to, SHUT_RDWR);
    }
}

// -----------------------------------------------------------------------
// First flight handling
// -----------------------------------------------------------------------
// Waits for the client's first bytes.  For TLS keeps reading until the whole
// first record is in, so the SNI can be parsed even if the ClientHello
// arrived in several segments.  Returns false if there is nothing to forward.
bool readFirstFlight(PfShared& sh, int fd, bool tls, QByteArray& out)
{
    char buf[16384];
    // Browsers pre-open connections and may sit idle for a long time before
    // the first byte, so there is deliberately no timeout here.
    while (!sh.stop) {
        pollfd p{fd, POLLIN, 0};
        const int r = ::poll(&p, 1, 500);
        if (r < 0) { if (errno == EINTR) continue; return false; }
        if (r == 0) continue;
        const ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
        if (n < 0) { if (errno == EINTR || errno == EAGAIN) continue; return false; }
        if (n == 0) return !out.isEmpty();
        out.append(buf, static_cast<int>(n));

        if (!tls) return true;
        const auto* d = reinterpret_cast<const uint8_t*>(out.constData());
        if (d[0] != TLS_HANDSHAKE) return true;               // not TLS: pass through untouched
        const uint32_t total = tlsRecordTotalLen(d, static_cast<uint32_t>(out.size()));
        if (total != 0 && static_cast<uint32_t>(out.size()) >= total) return true;
        if (out.size() > 20000) return true;                  // implausible record; give up buffering
    }
    return false;
}

// Same rule as the Windows backend: a fragment size outside the payload
// falls back to a single byte.
int effectiveFragSize(int size, int payloadLen)
{
    return (size <= 0 || size >= payloadLen) ? 1 : size;
}

// Locates the host name inside a ClientHello (offset from the start of the
// record).  Mirrors extractSNI() but reports the position.
bool findSniHost(const uint8_t* p, uint32_t n, uint32_t& off, uint32_t& len)
{
    if (n < 5 || p[0] != TLS_HANDSHAKE) return false;
    uint32_t o = 5;
    if (o + 4 > n || p[o] != TLS_CLIENT_HELLO) return false;
    o += 4;
    if (o + 35 > n) return false;
    o += 34;
    const uint32_t sid = p[o++];
    if (o + sid + 2 > n) return false;
    o += sid;
    const uint32_t cs = (p[o] << 8) | p[o + 1];
    o += 2;
    if (o + cs + 1 > n) return false;
    o += cs;
    const uint32_t cm = p[o++];
    if (o + cm + 2 > n) return false;
    o += cm;
    const uint32_t extEnd = o + 2 + ((p[o] << 8) | p[o + 1]);
    o += 2;
    if (extEnd > n) return false;

    while (o + 4 <= extEnd) {
        const uint32_t type = (p[o] << 8) | p[o + 1];
        const uint32_t elen = (p[o + 2] << 8) | p[o + 3];
        o += 4;
        if (o + elen > extEnd) return false;
        if (type == TLS_EXTENSION_SNI) {
            if (elen < 5 || p[o + 2] != SNI_TYPE_HOST_NAME) return false;
            const uint32_t nameLen = (p[o + 3] << 8) | p[o + 4];
            if (5 + nameLen > elen || nameLen == 0) return false;
            off = o + 5;
            len = nameLen;
            return true;
        }
        o += elen;
    }
    return false;
}

// Re-frames the first TLS record as two records, cutting its body at `cut`
// (an offset into the whole record).  Returns where the second record
// starts, or -1 if the record cannot be split.
int splitTlsRecord(QByteArray& data, uint32_t cut)
{
    const auto* d = reinterpret_cast<const uint8_t*>(data.constData());
    const uint32_t total = tlsRecordTotalLen(d, static_cast<uint32_t>(data.size()));
    if (total == 0 || total > static_cast<uint32_t>(data.size()) || cut <= 5 || cut >= total)
        return -1;

    const QByteArray body = data.mid(5, static_cast<int>(total - 5));
    const QByteArray tail = data.mid(static_cast<int>(total));
    const uint32_t k = cut - 5;

    auto record = [&](const QByteArray& part) {
        QByteArray r = data.left(3);   // type + legacy version
        r.append(static_cast<char>((part.size() >> 8) & 0xFF));
        r.append(static_cast<char>(part.size() & 0xFF));
        r.append(part);
        return r;
    };
    const QByteArray r1 = record(body.left(static_cast<int>(k)));
    const QByteArray r2 = record(body.mid(static_cast<int>(k)));

    data = r1 + r2 + tail;
    return r1.size();
}

bool looksLikeHttp(const QByteArray& d)
{
    return d.size() >= 4 && (d.startsWith("GET ") || d.startsWith("POST") ||
                             d.startsWith("HEAD") || d.startsWith("CONN"));
}

// Decides whether to bypass and writes the first flight to the server
// (possibly split / re-framed).  Everything after this is a plain relay.
bool sendFirstFlight(const BypassConfig& cfg, bool tls, QByteArray data, int upstreamFd)
{
    const auto* p = reinterpret_cast<const uint8_t*>(data.constData());
    const uint32_t n = static_cast<uint32_t>(data.size());
    QList<int> cuts;

    if (tls) {
        const bool hello = cfg.fragmentHttpsPersist && n >= 3 &&
                           p[0] == TLS_HANDSHAKE && p[1] == 0x03 && p[2] <= 0x03;
        if (hello) {
            const QString sni = extractSNI(p, n);
            const bool bypass = !sni.isEmpty() && shouldBypass(sni, cfg);
            LOG_INFO("TLS ClientHello | SNI=%1 | bypass=%2 | pLen=%3",
                     sni.isEmpty() ? "(none)" : sni, bypass ? "yes" : "no", n);
            if (bypass) {
                cuts << effectiveFragSize(cfg.httpsFragmentSize, static_cast<int>(n));
                uint32_t off = 0, len = 0;
                if (cfg.tlsRecordSplit && findSniHost(p, n, off, len)) {
                    // Cut in the middle of the host name so neither record holds all of it.
                    const int boundary = splitTlsRecord(data, off + std::max<uint32_t>(1, len / 2));
                    if (boundary > 0) cuts << boundary;
                }
            }
        }
    } else if (cfg.fragmentHttpRequests && looksLikeHttp(data)) {
        const QString host = extractHttpHost(p, n);
        const bool bypass = shouldBypass(host, cfg);
        LOG_INFO("HTTP request | Host=%1 | bypass=%2 | pLen=%3",
                 host.isEmpty() ? "(none)" : host, bypass ? "yes" : "no", n);
        if (bypass)
            cuts << effectiveFragSize(cfg.httpFragmentSize, static_cast<int>(n));
    }

    return sendChunks(upstreamFd, data, cuts);
}

// -----------------------------------------------------------------------
// One redirected TCP connection
// -----------------------------------------------------------------------
void handleTcp(const std::shared_ptr<PfShared>& sh, int clientFd, int proxyPort)
{
    Fd client(clientFd);
    setNoSigPipe(clientFd);
    setNoDelay(clientFd);
    setBlocking(clientFd, true);   // BSD accept() inherits O_NONBLOCK from the listener

    sockaddr_in peer{}, local{};
    socklen_t pl = sizeof(peer), ll = sizeof(local);
    if (::getpeername(clientFd, reinterpret_cast<sockaddr*>(&peer), &pl) < 0 ||
        ::getsockname(clientFd, reinterpret_cast<sockaddr*>(&local), &ll) < 0)
        return;

    sockaddr_in dst{};
    if (!natLookup(sh->pfFd, IPPROTO_TCP, peer, local, dst)) {
        LOG_WARN("DIOCNATLOOK failed for %1 (%2)", ipString(peer),
                 QString::fromLocal8Bit(strerror(errno)));
        return;
    }
    const uint32_t ip = ntohl(dst.sin_addr.s_addr);
    if ((ip >> 24) == 127 || ip == 0)
        return;   // would loop straight back into the proxy

    Fd upstream(connectTo(dst, kConnectTimeoutMs));
    if (upstream.fd < 0) {
        LOG_DEBUG("Connect to %1 failed (%2)", ipString(dst), QString::fromLocal8Bit(strerror(errno)));
        return;
    }

    const bool tls = (proxyPort == PfPorts::Https);
    QByteArray first;
    if (!readFirstFlight(*sh, clientFd, tls, first))
        return;
    ++sh->conns;
    if (!sendFirstFlight(sh->cfg, tls, first, upstream.fd))
        return;
    sh->bytes += static_cast<quint64>(first.size());

    PfShared* state = sh.get();
    std::thread reverse([state, &upstream, clientFd] { pump(*state, upstream.fd, clientFd); });
    pump(*state, clientFd, upstream.fd);
    reverse.join();
}

// -----------------------------------------------------------------------
// One redirected DNS query
// -----------------------------------------------------------------------
void handleDns(const std::shared_ptr<PfShared>& sh, const QByteArray& query, const sockaddr_in& client)
{
    const BypassConfig& cfg = sh->cfg;
    const auto* q = reinterpret_cast<const uint8_t*>(query.constData());
    const uint32_t qlen = static_cast<uint32_t>(query.size());

    const QString domain = extractDnsQuery(q, qlen);
    const bool bypass = shouldBypass(domain, cfg);

    sockaddr_in target{};
    bool haveTarget = false;
    if (bypass) {
        in_addr a{};
        if (::inet_pton(AF_INET, cfg.dnsServer.toUtf8().constData(), &a) == 1) {
            target.sin_len         = sizeof(target);
            target.sin_family      = AF_INET;
            target.sin_addr        = a;
            target.sin_port        = htons(static_cast<uint16_t>(cfg.dnsPort));
            haveTarget = true;
        } else {
            LOG_WARN("DNS redirect: %1 is not an IPv4 address; forwarding to the original resolver",
                     cfg.dnsServer);
        }
    }
    if (!haveTarget) {
        sockaddr_in local{};
        socklen_t ll = sizeof(local);
        if (::getsockname(sh->dnsFd, reinterpret_cast<sockaddr*>(&local), &ll) < 0 ||
            !natLookup(sh->pfFd, IPPROTO_UDP, client, local, target))
            return;
    }
    const uint32_t tip = ntohl(target.sin_addr.s_addr);
    if ((tip >> 24) == 127 || tip == 0)
        return;

    LOG_DEBUG("DNS | domain=%1 | bypass=%2 | via %3", domain, bypass ? "yes" : "no", ipString(target));
    ++sh->conns;

    Fd s(::socket(AF_INET, SOCK_DGRAM, 0));
    if (s.fd < 0) return;
    setNoSigPipe(s.fd);
    setCloexec(s.fd);
    // connect() makes the kernel drop datagrams from anywhere but the resolver.
    if (::connect(s.fd, reinterpret_cast<const sockaddr*>(&target), sizeof(target)) < 0 ||
        ::send(s.fd, query.constData(), query.size(), 0) < 0)
        return;

    pollfd p{s.fd, POLLIN, 0};
    int r;
    do { r = ::poll(&p, 1, 3000); } while (r < 0 && errno == EINTR);
    if (r <= 0) return;

    QByteArray reply(65535, '\0');
    const ssize_t n = ::recv(s.fd, reply.data(), static_cast<size_t>(reply.size()), 0);
    if (n <= 0) return;

    // The reply must come from the socket pf redirected the query to, so
    // pf can rewrite the source back to the resolver the application used.
    ::sendto(sh->dnsFd, reply.constData(), static_cast<size_t>(n), 0,
             reinterpret_cast<const sockaddr*>(&client), sizeof(client));
    sh->bytes += static_cast<quint64>(n);
}

// Runs `fn` on a detached thread that keeps the shared state alive.
template <typename Fn>
bool spawn(const std::shared_ptr<PfShared>& sh, Fn fn)
{
    ++sh->active;
    try {
        std::thread([sh, fn = std::move(fn)]() mutable {
            fn(sh);
            --sh->active;
        }).detach();
        return true;
    } catch (...) {
        --sh->active;
        return false;
    }
}

void acceptConnections(const std::shared_ptr<PfShared>& sh, int listenFd, int proxyPort)
{
    for (;;) {
        const int fd = ::accept(listenFd, nullptr, nullptr);
        if (fd < 0) {
            if (errno == EMFILE || errno == ENFILE)
                std::this_thread::sleep_for(std::chrono::milliseconds(50));   // avoid spinning
            return;
        }
        if (sh->active >= kMaxConnections) {
            LOG_WARN0("Too many concurrent connections; dropping a new one");
            ::close(fd);
            continue;
        }
        if (!spawn(sh, [fd, proxyPort](const std::shared_ptr<PfShared>& s) { handleTcp(s, fd, proxyPort); }))
            ::close(fd);
    }
}

void receiveDnsQueries(const std::shared_ptr<PfShared>& sh)
{
    QByteArray buf(65535, '\0');
    for (;;) {
        sockaddr_in client{};
        socklen_t cl = sizeof(client);
        const ssize_t n = ::recvfrom(sh->dnsFd, buf.data(), static_cast<size_t>(buf.size()), 0,
                                     reinterpret_cast<sockaddr*>(&client), &cl);
        if (n < 0) return;   // EWOULDBLOCK: drained
        if (n < 12 || sh->active >= kMaxConnections) continue;
        const QByteArray query(buf.constData(), static_cast<int>(n));
        spawn(sh, [query, client](const std::shared_ptr<PfShared>& s) { handleDns(s, query, client); });
    }
}

// -----------------------------------------------------------------------
// pfctl
// -----------------------------------------------------------------------
struct PfctlResult {
    bool    ok;
    QString output;
};

PfctlResult runPfctl(const QStringList& args, const QByteArray& input = {})
{
    QProcess p;
    p.setProcessChannelMode(QProcess::MergedChannels);
    p.start(kPfctl, args);
    if (!p.waitForStarted(3000))
        return {false, "pfctl could not be started"};
    if (!input.isEmpty())
        p.write(input);
    p.closeWriteChannel();
    if (!p.waitForFinished(10000)) {
        p.kill();
        return {false, "pfctl timed out"};
    }
    return {p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0,
            QString::fromLocal8Bit(p.readAll()).trimmed()};
}

// Redirect rules mirror zapret's tpws setup for macOS:
//  * outgoing packets are routed onto lo0, where the rdr rule hands them to the proxy;
//  * `user { >root }` exempts root, which is what the proxy itself runs as,
//    so its upstream connections go out directly (and root's own traffic is not bypassed);
//  * `from !127.0.0.0/8` keeps genuine loopback traffic out of it.
QByteArray buildRules(const BypassConfig& c)
{
    QStringList trans, filter;

    auto redirect = [&](const QString& proto, int port, int proxyPort) {
        trans << QString("rdr pass on lo0 inet proto %1 from !127.0.0.0/8 to any port %2 -> 127.0.0.1 port %3")
                     .arg(proto).arg(port).arg(proxyPort);
        filter << QString("pass out route-to (lo0 127.0.0.1) inet proto %1 from any to any port %2 user { >root }")
                     .arg(proto).arg(port);
    };
    if (c.fragmentHttpRequests) redirect("tcp", 80,  PfPorts::Http);
    if (c.fragmentHttpsPersist) redirect("tcp", 443, PfPorts::Https);
    if (c.dnsRedirect)          redirect("udp", 53,  PfPorts::Dns);

    // The proxy is IPv4-only.  On IPv6 networks the same connections would
    // simply go around it, so refuse them with an immediate reset/unreachable;
    // browsers and mDNSResponder then fall back to IPv4 within milliseconds.
    auto refuseIpv6 = [&](const QString& proto, int port) {
        filter.prepend(QString("block return out quick inet6 proto %1 from any to any port %2 user { >root }")
                           .arg(proto).arg(port));
    };
    if (c.fragmentHttpRequests) refuseIpv6("tcp", 80);
    if (c.fragmentHttpsPersist) refuseIpv6("tcp", 443);
    if (c.dnsRedirect)          refuseIpv6("udp", 53);

    // Same policy as the Windows backend: QUIC hides the SNI, so in Global
    // mode it is dropped and browsers fall back to TCP.  Applies to both IPv4 and IPv6.
    if (c.mode == BypassMode::GlobalMode)
        filter.prepend("block drop out quick proto udp from any to any port 443");

    return (trans + filter).join('\n').toUtf8() + '\n';
}

} // namespace

// -----------------------------------------------------------------------
// PfBackend
// -----------------------------------------------------------------------
PfBackend::PfBackend(QObject* parent)
    : IPlatformBackend(parent)
    , m_anchor(QString("%1/%2.%3").arg(kAnchorParent, kAnchorName).arg(getpid()))
{
    if (geteuid() == 0)
        clearStaleAnchors();
}

PfBackend::~PfBackend() { stop(); }

bool PfBackend::hasRequiredPrivileges() const { return geteuid() == 0; }

bool PfBackend::requestPrivilegeElevation()
{
    auto shellQuote = [](QString s) { s.replace("'", "'\\''"); return "'" + s + "'"; };
    auto asQuote    = [](QString s) { s.replace("\\", "\\\\").replace("\"", "\\\""); return s; };

    // Relaunch as root.  HOME is pinned to the real user's home so the elevated
    // instance reads the same config/log folders; the trailing '&' lets
    // osascript return as soon as the app is launched.
    const QString command = QString("HOME=%1 %2 >/dev/null 2>&1 &")
                                .arg(shellQuote(QDir::homePath()),
                                     shellQuote(QCoreApplication::applicationFilePath()));
    const QString script = QString("do shell script \"%1\" with administrator privileges")
                               .arg(asQuote(command));

    // Wait for the password prompt: osascript exits non-zero when the user
    // cancels, and in that case this instance must keep running.
    QProcess p;
    p.start("/usr/bin/osascript", {"-e", script});
    if (!p.waitForStarted(5000))
        return false;
    p.waitForFinished(-1);
    const bool ok = p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0;
    if (!ok)
        LOG_WARN("Elevation not granted: %1", QString::fromLocal8Bit(p.readAllStandardError()).trimmed());
    return ok;
}

// Rules of an instance that died without stopping keep redirecting web
// traffic to a closed port.  Remove every PktSwerve anchor whose process is
// gone (including the fixed-name anchor used by earlier builds).
void PfBackend::clearStaleAnchors()
{
    static const QRegularExpression re(
        QString("(%1(?:\\.(\\d+))?)$").arg(QRegularExpression::escape(kAnchorName)));

    const PfctlResult list = runPfctl({"-a", kAnchorParent, "-s", "Anchors"});
    for (const QString& line : list.output.split('\n')) {
        const QRegularExpressionMatch m = re.match(line.trimmed());
        if (!m.hasMatch()) continue;
        const qint64 pid = m.captured(2).toLongLong();      // 0 for the old fixed name
        if (pid == getpid() || (pid > 0 && ::kill(static_cast<pid_t>(pid), 0) == 0))
            continue;                                       // ours, or still alive
        const QString anchor = kAnchorParent + "/" + m.captured(1);
        LOG_INFO("Removing stale pf anchor %1", anchor);
        runPfctl({"-a", anchor, "-F", "rules"});
        runPfctl({"-a", anchor, "-F", "nat"});
    }
}

// Detached root shell that waits for this process to exit and then removes
// our rules and pf reference.  Covers crashes and force-quits; a normal stop
// kills it first and cleans up itself.
void PfBackend::startWatchdog()
{
    QString script = QString("while kill -0 %1 2>/dev/null; do sleep 2; done; "
                             "%2 -a '%3' -F rules; %2 -a '%3' -F nat")
                         .arg(QString::number(getpid()), kPfctl, m_anchor);
    if (!m_pfToken.isEmpty())
        script += QString("; %1 -X %2").arg(kPfctl, m_pfToken);

    qint64 pid = 0;
    if (QProcess::startDetached("/bin/sh", {"-c", script}, QString(), &pid))
        m_watchdogPid = pid;
    else
        LOG_WARN0("Could not start the pf cleanup watchdog");
}

void PfBackend::stopWatchdog()
{
    if (m_watchdogPid > 0) {
        ::kill(static_cast<pid_t>(m_watchdogPid), SIGTERM);
        m_watchdogPid = 0;
    }
}

bool PfBackend::fail(const QString& message)
{
    m_lastError = message;
    LOG_ERROR("PfBackend: %1", message);
    emit errorOccurred(message);
    return false;
}

bool PfBackend::setupPf(const BypassConfig& config)
{
    // -E takes a reference on pf instead of just switching it on, so other
    // users of pf (and macOS itself) are left alone when we release it.
    const PfctlResult enable = runPfctl({"-E"});
    if (!enable.ok)
        return fail("Could not enable pf: " + enable.output);
    const QRegularExpressionMatch m = QRegularExpression("Token\\s*:\\s*(\\d+)").match(enable.output);
    m_pfToken = m.hasMatch() ? m.captured(1) : QString();

    const PfctlResult load = runPfctl({"-a", m_anchor, "-f", "-"}, buildRules(config));
    if (!load.ok) {
        clearPf();
        return fail("pf rejected the redirect rules: " + load.output);
    }
    startWatchdog();
    LOG_INFO("pf rules loaded into anchor %1", m_anchor);
    return true;
}

bool PfBackend::clearPf()
{
    stopWatchdog();
    runPfctl({"-a", m_anchor, "-F", "rules"});
    runPfctl({"-a", m_anchor, "-F", "nat"});
    if (!m_pfToken.isEmpty()) {
        runPfctl({"-X", m_pfToken});
        m_pfToken.clear();
    }
    return true;
}

bool PfBackend::start(const BypassConfig& config)
{
    if (m_running) {
        LOG_WARN0("start() called but already running");
        return true;
    }
    if (!hasRequiredPrivileges())
        return fail("Administrator privileges are required.");
    if (!config.fragmentHttpRequests && !config.fragmentHttpsPersist && !config.dnsRedirect)
        return fail("Nothing to do: enable HTTP/HTTPS fragmentation or DNS redirect.");

    auto* worker = new PfWorker();
    worker->setConfig(config);
    QString error;
    if (!worker->prepare(&error)) {
        delete worker;
        return fail(error);
    }
    // Listeners are up; only now is it safe to send traffic their way.
    if (!setupPf(config)) {
        delete worker;
        return false;
    }
    if (config.dnsRedirect) {
        // Answers cached before the redirect was active may be the poisoned ones.
        QProcess::startDetached("/usr/bin/dscacheutil", {"-flushcache"});
        QProcess::startDetached("/usr/bin/killall", {"-HUP", "mDNSResponder"});
    }

    m_state = worker->state();
    const quint64 generation = ++m_workerGeneration;

    QThread* thread = new QThread(this);
    m_thread = thread;
    worker->moveToThread(thread);

    connect(thread, &QThread::started,         worker, &PfWorker::run);
    connect(worker, &PfWorker::finished,       thread, &QThread::quit);
    connect(worker, &PfWorker::finished,       worker, &QObject::deleteLater);
    connect(thread, &QThread::finished,        thread, &QObject::deleteLater);
    connect(worker, &PfWorker::errorOccurred,  this,   &PfBackend::errorOccurred);
    connect(worker, &PfWorker::statsUpdated,   this,   &PfBackend::statsUpdated);
    connect(worker,   &PfWorker::finished, this, [this, generation]() {
        // A stop()/start() sequence can deliver the old worker's signal after
        // the new one is running; ignore it.
        if (generation != m_workerGeneration) return;
        LOG_INFO0("PfWorker finished - backend marked stopped");
        clearPf();
        m_state.reset();
        m_running = false;
        emit stopped();
    });

    m_running = true;
    thread->start();
    LOG_INFO0("PfBackend started");
    emit started();
    return true;
}

bool PfBackend::stop()
{
    if (!m_running) return true;

    LOG_INFO0("PfBackend stopping");
    clearPf();                                  // stop steering traffic at us first
    if (m_state) m_state->requestStop();
    if (m_thread && m_thread->isRunning() && !m_thread->wait(5000))
        LOG_WARN0("PfWorker thread did not stop within 5s");
    m_running = false;
    return true;
}

bool    PfBackend::isRunning() const { return m_running; }
QString PfBackend::lastError() const { return m_lastError; }

// -----------------------------------------------------------------------
// PfWorker
// -----------------------------------------------------------------------
PfWorker::PfWorker(QObject* parent) : QObject(parent), m_shared(std::make_shared<PfShared>()) {}

PfWorker::~PfWorker() { closeListeners(); }

void PfWorker::setConfig(const BypassConfig& cfg) { m_shared->cfg = cfg; }

void PfWorker::closeListeners()
{
    for (int* fd : {&m_httpFd, &m_httpsFd}) {
        if (*fd >= 0) { ::close(*fd); *fd = -1; }
    }
}

bool PfWorker::prepare(QString* error)
{
    PfShared& sh = *m_shared;

    // The default soft limit is 256 descriptors, which a handful of browser
    // tabs would exhaust (every connection needs two).
    rlimit rl{};
    if (::getrlimit(RLIMIT_NOFILE, &rl) == 0) {
        const rlim_t wanted = std::min<rlim_t>(rl.rlim_max, 10240);
        if (rl.rlim_cur < wanted) {
            rl.rlim_cur = wanted;
            ::setrlimit(RLIMIT_NOFILE, &rl);
        }
    }

    sh.pfFd = ::open("/dev/pf", O_RDWR);
    if (sh.pfFd < 0) sh.pfFd = ::open("/dev/pf", O_RDONLY);
    if (sh.pfFd < 0) {
        *error = QString("Cannot open /dev/pf: %1").arg(QString::fromLocal8Bit(strerror(errno)));
        return false;
    }
    setCloexec(sh.pfFd);

    if (::pipe(sh.wakeFd) < 0) {
        *error = "pipe() failed";
        return false;
    }
    setCloexec(sh.wakeFd[0]);
    setCloexec(sh.wakeFd[1]);

    if (sh.cfg.fragmentHttpRequests && (m_httpFd  = makeListener(SOCK_STREAM, PfPorts::Http,  error)) < 0) return false;
    if (sh.cfg.fragmentHttpsPersist && (m_httpsFd = makeListener(SOCK_STREAM, PfPorts::Https, error)) < 0) { closeListeners(); return false; }
    if (sh.cfg.dnsRedirect && (sh.dnsFd = makeListener(SOCK_DGRAM, PfPorts::Dns, error)) < 0)             { closeListeners(); return false; }
    return true;
}

void PfWorker::run()
{
    const std::shared_ptr<PfShared> sh = m_shared;
    LOG_INFO("PfWorker proxy loop started | http=%1 https=%2 dns=%3",
             m_httpFd >= 0 ? "on" : "off", m_httpsFd >= 0 ? "on" : "off", sh->dnsFd >= 0 ? "on" : "off");

    quint64 lastConns = ~0ULL, lastBytes = ~0ULL;
    while (!sh->stop) {
        // fds[0] is the stop pipe; the listeners that are enabled follow it.
        pollfd fds[4];
        nfds_t n = 0;
        auto add = [&](int fd) -> nfds_t {
            if (fd < 0) return 99;   // not enabled; 99 is never < n
            fds[n] = pollfd{fd, POLLIN, 0};
            return n++;
        };
        add(sh->wakeFd[0]);
        const nfds_t iHttp  = add(m_httpFd);
        const nfds_t iHttps = add(m_httpsFd);
        const nfds_t iDns   = add(sh->dnsFd);

        const int r = ::poll(fds, n, 1000);
        if (r < 0) {
            if (errno == EINTR) continue;
            emit errorOccurred(QString("poll() failed: %1").arg(QString::fromLocal8Bit(strerror(errno))));
            break;
        }
        if (fds[0].revents & POLLIN) break;

        if (iHttp  < n && (fds[iHttp].revents  & POLLIN)) acceptConnections(sh, m_httpFd,  PfPorts::Http);
        if (iHttps < n && (fds[iHttps].revents & POLLIN)) acceptConnections(sh, m_httpsFd, PfPorts::Https);
        if (iDns   < n && (fds[iDns].revents   & POLLIN)) receiveDnsQueries(sh);

        const quint64 conns = sh->conns, bytes = sh->bytes;
        if (conns != lastConns || bytes != lastBytes) {
            lastConns = conns;
            lastBytes = bytes;
            emit statsUpdated(conns, bytes);
        }
    }

    sh->stop = true;
    closeListeners();

    // Connection threads notice `stop` within ~500 ms.
    for (int i = 0; i < 40 && sh->active > 0; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    if (sh->active > 0)
        LOG_WARN("PfWorker leaving with %1 connection threads still winding down", sh->active.load());

    LOG_INFO("PfWorker proxy loop exited | connections=%1 | bytes=%2", sh->conns.load(), sh->bytes.load());
    emit finished();
}

#endif // PLATFORM_MACOS
