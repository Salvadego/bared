/*
 * barenet.h -- TCP/UDP sockets
 * ==============================
 *
 *  USAGE
 *    #define BARENET_IMPLEMENTATION
 *    #include "barenet.h"
 *
 *  DEPENDS ON
 *    barestd.h
 *
 *  PLATFORM
 *    POSIX   -- BSD sockets (Linux, macOS)
 *    Windows -- Winsock2 (ws2_32.lib)
 *
 *  DESIGN
 *    Sock is a thin handle (int fd or SOCKET).  All functions that
 *    need strings use a caller-supplied arena for the result; internal
 *    scratch work uses local stack buffers (fixed-size, no VLA).
 *    Hot paths (send/recv) allocate nothing.
 *
 *    NetAddr packs address + port into 20 bytes on the stack.
 *    Convert to/from struct sockaddr_storage internally -- callers
 *    never touch OS address structs.
 *
 *    Error model: functions return SOCK_INVALID / false / -1 on failure.
 *    net_last_err(arena) returns a Str describing the last error.
 *
 *  EXAMPLE -- TCP echo server
 *
 *    net_init();  // Windows only: WSAStartup
 *    Sock srv = net_tcp_listen(str_lit("0.0.0.0"), 8080, 128);
 *    while (1) {
 *        NetAddr peer;
 *        Sock    cli = net_accept(srv, &peer);
 *        // ... handle cli in a thread ...
 *        net_close(cli);
 *    }
 *    net_close(srv);
 *    net_cleanup();
 *
 *  EXAMPLE -- UDP
 *
 *    net_init();
 *    Sock s = net_udp_bind(str_lit("0.0.0.0"), 9000);
 *    char buf[1024];
 *    NetAddr from;
 *    int n = net_recvfrom(s, buf, sizeof buf, &from);
 *    net_sendto(s, buf, n, from);
 *    net_close(s);
 */

/* Feature test macros -- must appear before any system header. */
#if !defined(_WIN32) && !defined(_WIN64)
#        if !defined(_POSIX_C_SOURCE) || _POSIX_C_SOURCE < 200809L
#                undef _POSIX_C_SOURCE
#                define _POSIX_C_SOURCE 200809L
#        endif
#        if defined(__linux__)
#                ifndef _DEFAULT_SOURCE
#                        define _DEFAULT_SOURCE 1
#                endif
#        endif
#endif
#ifndef BARENET_H
#        define BARENET_H

#        include <stdint.h>

#        include "barestd.h"

/* ================================================================
 *  Platform setup
 * ================================================================ */

#        if defined(_WIN32) || defined(_WIN64)
#                define _BN_WIN
#                ifndef WIN32_LEAN_AND_MEAN
#                        define WIN32_LEAN_AND_MEAN
#                endif
#                include <winsock2.h>
#                include <ws2tcpip.h>
typedef SOCKET _BnFd;
#                define SOCK_INVALID_FD INVALID_SOCKET
#        else
#                define _BN_POSIX
#                include <arpa/inet.h>
#                include <errno.h>
#                include <fcntl.h>
#                include <netdb.h>
#                include <netinet/in.h>
#                include <netinet/tcp.h>
#                include <sys/socket.h>
#                include <sys/types.h>
#                include <unistd.h>
typedef int _BnFd;
#                define SOCK_INVALID_FD (-1)
#        endif

/* ================================================================
 *  Types
 * ================================================================ */

typedef struct {
        _BnFd _fd;
} Sock;
#        define SOCK_INVALID ((Sock){SOCK_INVALID_FD})

static inline bool sock_valid(Sock s) {
        return s._fd != SOCK_INVALID_FD;
}

/* Compact address: IPv4 or IPv6 + port, no OS types exposed. */
typedef struct {
        uint8_t _sa[28]; /* enough for sockaddr_in6 (28 B) */
        int     _len;
} NetAddr;

/* Addr family */
typedef enum { NET_IPV4 = 4, NET_IPV6 = 6 } NetFamily;

/* ================================================================
 *  Lifecycle
 * ================================================================ */

/* Call once before using any net functions (no-op on POSIX). */
void net_init(void);
/* Call once at shutdown (no-op on POSIX). */
void net_cleanup(void);

/* ================================================================
 *  Address helpers -- no allocation on fast path
 *
 *  addr_buf is a caller-supplied char[NETADDR_STR_MAX] for the string.
 *  NETADDR_STR_MAX == 46 covers "ffff:ffff:...:ffff%scope" + port.
 * ================================================================ */

#        define NETADDR_STR_MAX 64

/*
 * Parse "host:port" or "host" + explicit port into a NetAddr.
 * Resolves DNS. Returns true on success.
 * Scratch used internally for getaddrinfo results -- freed before return.
 */
bool net_resolve(
    Arena* a, Str host, uint16_t port, NetFamily family, NetAddr* out);

/* Format addr:port into buf (must be NETADDR_STR_MAX bytes).
   Returns a Str pointing into buf -- no allocation. */
Str net_addr_str(const NetAddr* addr, char buf[NETADDR_STR_MAX]);

uint16_t net_addr_port(const NetAddr* addr);

/* ================================================================
 *  TCP
 * ================================================================ */

/* Connect to host:port (blocking). Returns SOCK_INVALID on failure. */
Sock net_tcp_connect(Arena* a, Str host, uint16_t port);

/* Create a listening socket bound to host:port with backlog. */
Sock net_tcp_listen(Str host, uint16_t port, int backlog);

/* Accept the next incoming connection. Blocks. peer may be NULL. */
Sock net_accept(Sock srv, NetAddr* peer);

/* Send exactly n bytes; retries on EINTR. Returns false on error. */
bool net_send(Sock s, const void* buf, size_t n);
/* Receive up to n bytes. Returns bytes read, 0 on EOF, -1 on error. */
int net_recv(Sock s, void* buf, size_t n);

/* Send / receive a framed message: 4-byte little-endian length prefix.
   net_send_msg allocates nothing.
   net_recv_msg allocates into arena (uses scratch internally for read
   then copies the final message -- one arena alloc for the result). */
bool net_send_msg(Sock s, const void* data, uint32_t len);
Str  net_recv_msg(Arena* a, Sock s); /* str_null on error/close */

/* ================================================================
 *  UDP
 * ================================================================ */

/* Bind a UDP socket to host:port. */
Sock net_udp_bind(Str host, uint16_t port);

/* Create an unbound UDP socket. */
Sock net_udp_socket(NetFamily family);

int net_sendto(Sock s, const void* buf, size_t n, NetAddr dst);
int net_recvfrom(Sock s, void* buf, size_t cap, NetAddr* src);

/* ================================================================
 *  Socket options
 * ================================================================ */

void net_set_nonblocking(Sock s, bool on);
void net_set_nodelay(Sock s, bool on);   /* TCP_NODELAY      */
void net_set_reuseaddr(Sock s, bool on); /* SO_REUSEADDR     */
void net_set_reuseport(Sock s, bool on); /* SO_REUSEPORT     */
void net_set_keepalive(Sock s, bool on); /* SO_KEEPALIVE     */
void net_set_recvtimeo(Sock s, int ms);  /* SO_RCVTIMEO      */
void net_set_sendtimeo(Sock s, int ms);  /* SO_SNDTIMEO      */

void net_close(Sock s);

/* Last error as a Str (uses a static 128-byte buffer, not arena). */
Str net_last_err(void);

/* ================================================================
 *  IMPLEMENTATION
 * ================================================================ */
#        ifdef BARENET_IMPLEMENTATION

#                include <stdio.h>
#                include <string.h>

/* Static error buffer -- 128 B, never more needed. */
static char _bn_errbuf[128];

Str net_last_err(void) {
#                ifdef _BN_WIN
        int e = WSAGetLastError();
        int n = snprintf(_bn_errbuf, sizeof _bn_errbuf, "WSA error %d", e);
#                else
        int n = snprintf(_bn_errbuf, sizeof _bn_errbuf, "%s", strerror(errno));
#                endif
        return str_buf(_bn_errbuf, (size_t)(n > 0 ? n : 0));
}

/* ---- Init / cleanup ------------------------------------------ */

void net_init(void) {
#                ifdef _BN_WIN
        WSADATA wd;
        WSAStartup(MAKEWORD(2, 2), &wd);
#                endif
}
void net_cleanup(void) {
#                ifdef _BN_WIN
        WSACleanup();
#                endif
}

/* ---- Address helpers ----------------------------------------- */

bool net_resolve(
    Arena* a, Str host, uint16_t port, NetFamily family, NetAddr* out) {
        /* Stack buffer for host string -- avoids arena cost for short names.
           Hostnames > 253 chars are invalid per DNS spec. */
        char   hostbuf[256];
        char   portbuf[8];
        size_t hl = host.len < 255 ? host.len : 255;
        memcpy(hostbuf, host.ptr, hl);
        hostbuf[hl] = '\0';
        snprintf(portbuf, sizeof portbuf, "%u", (unsigned)port);

        struct addrinfo hints, *res = NULL;
        memset(&hints, 0, sizeof hints);
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_family   = (family == NET_IPV6) ? AF_INET6 : AF_INET;

        /* getaddrinfo allocates from libc heap -- we free immediately. */
        if (getaddrinfo(hostbuf, portbuf, &hints, &res) != 0 || !res) {
                Unused(a);
                return false;
        }
        size_t slen = (size_t)res->ai_addrlen;
        if (slen > sizeof out->_sa) slen = sizeof out->_sa;
        memcpy(out->_sa, res->ai_addr, slen);
        out->_len = (int)slen;
        freeaddrinfo(res);
        return true;
}

Str net_addr_str(const NetAddr* addr, char buf[NETADDR_STR_MAX]) {
        const struct sockaddr* sa     = (const struct sockaddr*)addr->_sa;
        char                   ip[48] = {0};
        uint16_t               port   = 0;
        if (sa->sa_family == AF_INET) {
                const struct sockaddr_in* s4 = (const struct sockaddr_in*)sa;
                inet_ntop(AF_INET, &s4->sin_addr, ip, sizeof ip);
                port = ntohs(s4->sin_port);
        } else {
                const struct sockaddr_in6* s6 = (const struct sockaddr_in6*)sa;
                inet_ntop(AF_INET6, &s6->sin6_addr, ip, sizeof ip);
                port = ntohs(s6->sin6_port);
        }
        int n = snprintf(buf, NETADDR_STR_MAX, "%s:%u", ip, (unsigned)port);
        return str_buf(buf, (size_t)(n > 0 ? n : 0));
}

uint16_t net_addr_port(const NetAddr* addr) {
        const struct sockaddr* sa = (const struct sockaddr*)addr->_sa;
        if (sa->sa_family == AF_INET)
                return ntohs(((const struct sockaddr_in*)sa)->sin_port);
        return ntohs(((const struct sockaddr_in6*)sa)->sin6_port);
}

/* ---- Internal: bind to host:port string ---------------------- */

static Sock _bn_bind_str(Str host, uint16_t port, int type) {
        char   hostbuf[256];
        char   portbuf[8];
        size_t hl = host.len < 255 ? host.len : 255;
        memcpy(hostbuf, host.ptr, hl);
        hostbuf[hl] = '\0';
        snprintf(portbuf, sizeof portbuf, "%u", (unsigned)port);

        struct addrinfo hints, *res = NULL;
        memset(&hints, 0, sizeof hints);
        hints.ai_family   = AF_UNSPEC;
        hints.ai_socktype = type;
        hints.ai_flags    = AI_PASSIVE;
        if (getaddrinfo(hostbuf, portbuf, &hints, &res) != 0 || !res)
                return SOCK_INVALID;

        _BnFd fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
        if (fd == SOCK_INVALID_FD) {
                freeaddrinfo(res);
                return SOCK_INVALID;
        }

#                ifdef _BN_POSIX
        {
                int one = 1;
                setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
        }
#                else
        {
                char one = 1;
                setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
        }
#                endif

        if (bind(fd, res->ai_addr, (socklen_t)res->ai_addrlen) != 0) {
                freeaddrinfo(res);
#                ifdef _BN_WIN
                closesocket(fd);
#                else
                close(fd);
#                endif
                return SOCK_INVALID;
        }
        freeaddrinfo(res);
        return (Sock){fd};
}

/* ---- TCP ----------------------------------------------------- */

Sock net_tcp_connect(Arena* a, Str host, uint16_t port) {
        NetAddr addr;
        if (!net_resolve(a, host, port, NET_IPV4, &addr)) {
                /* Try IPv6 if v4 fails */
                if (!net_resolve(a, host, port, NET_IPV6, &addr))
                        return SOCK_INVALID;
        }
        const struct sockaddr* sa = (const struct sockaddr*)addr._sa;
        _BnFd                  fd = socket(sa->sa_family, SOCK_STREAM, 0);
        if (fd == SOCK_INVALID_FD) return SOCK_INVALID;
        if (connect(fd, sa, (socklen_t)addr._len) != 0) {
#                ifdef _BN_WIN
                closesocket(fd);
#                else
                close(fd);
#                endif
                return SOCK_INVALID;
        }
        return (Sock){fd};
}

Sock net_tcp_listen(Str host, uint16_t port, int backlog) {
        Sock s = _bn_bind_str(host, port, SOCK_STREAM);
        if (!sock_valid(s)) return SOCK_INVALID;
        if (listen(s._fd, backlog) != 0) {
                net_close(s);
                return SOCK_INVALID;
        }
        return s;
}

Sock net_accept(Sock srv, NetAddr* peer) {
        struct sockaddr_storage ss;
        socklen_t               len = sizeof ss;
        _BnFd fd = accept(srv._fd, (struct sockaddr*)&ss, &len);
        if (fd == SOCK_INVALID_FD) return SOCK_INVALID;
        if (peer) {
                size_t cp = len < sizeof peer->_sa ? len : sizeof peer->_sa;
                memcpy(peer->_sa, &ss, cp);
                peer->_len = (int)len;
        }
        return (Sock){fd};
}

bool net_send(Sock s, const void* buf, size_t n) {
        const char* p = (const char*)buf;
        while (n > 0) {
#                ifdef _BN_WIN
                int sent = send(s._fd, p, (int)n, 0);
                if (sent <= 0) return false;
#                else
                ssize_t sent;
                do {
                        sent = send(s._fd, p, n, MSG_NOSIGNAL);
                } while (sent < 0 && errno == EINTR);
                if (sent <= 0) return false;
#                endif
                p += sent;
                n -= (size_t)sent;
        }
        return true;
}

int net_recv(Sock s, void* buf, size_t n) {
#                ifdef _BN_WIN
        int r = recv(s._fd, (char*)buf, (int)n, 0);
        return (int)r;
#                else
        ssize_t r;
        do {
                r = recv(s._fd, buf, n, 0);
        } while (r < 0 && errno == EINTR);
        return (int)r;
#                endif
}

bool net_send_msg(Sock s, const void* data, uint32_t len) {
        /* 4-byte LE length header, no allocation */
        uint8_t hdr[4];
        hdr[0] = (uint8_t)(len);
        hdr[1] = (uint8_t)(len >> 8);
        hdr[2] = (uint8_t)(len >> 16);
        hdr[3] = (uint8_t)(len >> 24);
        return net_send(s, hdr, 4) && net_send(s, data, len);
}

Str net_recv_msg(Arena* a, Sock s) {
        uint8_t hdr[4];
        /* Read exactly 4 bytes for the header */
        size_t got = 0;
        while (got < 4) {
                int r = net_recv(s, hdr + got, 4 - got);
                if (r <= 0) return str_null();
                got += (size_t)r;
        }
        uint32_t len = (uint32_t)hdr[0] | ((uint32_t)hdr[1] << 8) |
                       ((uint32_t)hdr[2] << 16) | ((uint32_t)hdr[3] << 24);
        if (len == 0) return str_buf("", 0);

        /* Read body directly into arena -- one allocation, no copy. */
        char* buf = arena_push_array(a, char, len);
        got       = 0;
        while (got < len) {
                int r = net_recv(s, buf + got, len - got);
                if (r <= 0) return str_null();
                got += (size_t)r;
        }
        return str_buf(buf, len);
}

/* ---- UDP ----------------------------------------------------- */

Sock net_udp_bind(Str host, uint16_t port) {
        return _bn_bind_str(host, port, SOCK_DGRAM);
}

Sock net_udp_socket(NetFamily family) {
        int   af = (family == NET_IPV6) ? AF_INET6 : AF_INET;
        _BnFd fd = socket(af, SOCK_DGRAM, 0);
        return (fd == SOCK_INVALID_FD) ? SOCK_INVALID : (Sock){fd};
}

int net_sendto(Sock s, const void* buf, size_t n, NetAddr dst) {
#                ifdef _BN_WIN
        return (int)sendto(s._fd,
                           (const char*)buf,
                           (int)n,
                           0,
                           (const struct sockaddr*)dst._sa,
                           (socklen_t)dst._len);
#                else
        return (int)sendto(s._fd,
                           buf,
                           n,
                           0,
                           (const struct sockaddr*)dst._sa,
                           (socklen_t)dst._len);
#                endif
}

int net_recvfrom(Sock s, void* buf, size_t cap, NetAddr* src) {
        struct sockaddr_storage ss;
        socklen_t               len = sizeof ss;
#                ifdef _BN_WIN
        int r = (int)recvfrom(
            s._fd, (char*)buf, (int)cap, 0, (struct sockaddr*)&ss, &len);
#                else
        int r = (int)recvfrom(s._fd, buf, cap, 0, (struct sockaddr*)&ss, &len);
#                endif
        if (r > 0 && src) {
                size_t cp = (size_t)len < sizeof src->_sa ? (size_t)len
                                                          : sizeof src->_sa;
                memcpy(src->_sa, &ss, cp);
                src->_len = (int)len;
        }
        return r;
}

/* ---- Socket options ------------------------------------------ */

static void _bn_setsock(Sock s, int level, int opt, int val) {
#                ifdef _BN_WIN
        setsockopt(s._fd, level, opt, (const char*)&val, sizeof val);
#                else
        setsockopt(s._fd, level, opt, &val, sizeof val);
#                endif
}

static void _bn_timeo(Sock s, int opt, int ms) {
#                ifdef _BN_WIN
        DWORD t = (DWORD)ms;
        setsockopt(s._fd, SOL_SOCKET, opt, (const char*)&t, sizeof t);
#                else
        struct timeval tv;
        tv.tv_sec  = ms / 1000;
        tv.tv_usec = (ms % 1000) * 1000;
        setsockopt(s._fd, SOL_SOCKET, opt, &tv, sizeof tv);
#                endif
}

void net_set_nodelay(Sock s, bool on) {
        _bn_setsock(s, IPPROTO_TCP, TCP_NODELAY, on ? 1 : 0);
}
void net_set_reuseaddr(Sock s, bool on) {
        _bn_setsock(s, SOL_SOCKET, SO_REUSEADDR, on ? 1 : 0);
}
void net_set_keepalive(Sock s, bool on) {
        _bn_setsock(s, SOL_SOCKET, SO_KEEPALIVE, on ? 1 : 0);
}
void net_set_recvtimeo(Sock s, int ms) {
        _bn_timeo(s, SO_RCVTIMEO, ms);
}
void net_set_sendtimeo(Sock s, int ms) {
        _bn_timeo(s, SO_SNDTIMEO, ms);
}

void net_set_reuseport(Sock s, bool on) {
#                ifdef SO_REUSEPORT
        _bn_setsock(s, SOL_SOCKET, SO_REUSEPORT, on ? 1 : 0);
#                else
        Unused(s);
        Unused(on);
#                endif
}

void net_set_nonblocking(Sock s, bool on) {
#                ifdef _BN_WIN
        u_long mode = on ? 1 : 0;
        ioctlsocket(s._fd, FIONBIO, &mode);
#                else
        int flags = fcntl(s._fd, F_GETFL, 0);
        if (on)
                flags |= O_NONBLOCK;
        else
                flags &= ~O_NONBLOCK;
        fcntl(s._fd, F_SETFL, flags);
#                endif
}

void net_close(Sock s) {
        if (s._fd == SOCK_INVALID_FD) return;
#                ifdef _BN_WIN
        closesocket(s._fd);
#                else
        close(s._fd);
#                endif
}

#        endif /* BARENET_IMPLEMENTATION */
#endif         /* BARENET_H */
