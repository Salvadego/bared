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

#if !defined(_GNU_SOURCE) && (defined(__linux__) || defined(__GLIBC__))
#        define _GNU_SOURCE
#endif
#ifndef BARENET_H
#        define BARENET_H
#        include <stdint.h>

#        include "barestd.h"
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
#                include <sys/time.h>
#                include <sys/types.h>
#                include <unistd.h>
typedef int _BnFd;
#                define SOCK_INVALID_FD (-1)
#        endif

typedef struct {
        _BnFd _fd;
} Sock;
#        define SOCK_INVALID ((Sock){SOCK_INVALID_FD})
static inline bool sock_valid(Sock s) {
        return s._fd != SOCK_INVALID_FD;
}

typedef struct {
        uint8_t _sa[28];
        int     _len;
} NetAddr;
typedef enum { NET_IPV4 = 4, NET_IPV6 = 6 } NetFamily;
#        define NETADDR_STR_MAX 64

void net_init(void);
void net_cleanup(void);
bool net_resolve(
    Arena* a, Str host, uint16_t port, NetFamily family, NetAddr* out);
Str      net_addr_str(const NetAddr* addr, char buf[NETADDR_STR_MAX]);
uint16_t net_addr_port(const NetAddr* addr);
Sock     net_tcp_connect(Arena* a, Str host, uint16_t port);
Sock     net_tcp_listen(Str host, uint16_t port, int backlog);
Sock     net_accept(Sock srv, NetAddr* peer);
bool     net_send(Sock s, const void* buf, size_t n);
int      net_recv(Sock s, void* buf, size_t n);
bool     net_send_msg(Sock s, const void* data, uint32_t len);
Str      net_recv_msg(Arena* a, Sock s);
Sock     net_udp_bind(Str host, uint16_t port);
Sock     net_udp_socket(NetFamily family);
int      net_sendto(Sock s, const void* buf, size_t n, NetAddr dst);
int      net_recvfrom(Sock s, void* buf, size_t cap, NetAddr* src);
void     net_set_nonblocking(Sock s, bool on);
void     net_set_nodelay(Sock s, bool on);
void     net_set_reuseaddr(Sock s, bool on);
void     net_set_reuseport(Sock s, bool on);
void     net_set_keepalive(Sock s, bool on);
void     net_set_recvtimeo(Sock s, int ms);
void     net_set_sendtimeo(Sock s, int ms);
void     net_close(Sock s);
Str      net_last_err(void);

/* ================================================================
 *  Multiplexing helpers
 * ================================================================ */

/* Event flags for net_poll */
#        define NET_POLL_IN  0x01 /* ready to read  */
#        define NET_POLL_OUT 0x02 /* ready to write */
#        define NET_POLL_ERR 0x04 /* error          */
#        define NET_POLL_HUP 0x08 /* hang up        */

typedef struct {
        Sock sock;
        int  events;  /* requested: NET_POLL_IN | NET_POLL_OUT */
        int  revents; /* returned:  what actually fired         */
} NetPollFd;

/*
 * Poll up to nfds sockets for events.
 * timeout_ms: -1 = block forever, 0 = non-blocking, >0 = ms to wait.
 * Returns number of fds with events, 0 on timeout, -1 on error.
 */
int net_poll(NetPollFd* fds, int nfds, int timeout_ms);

/*
 * Simple single-socket readiness check.
 * Returns true if the socket is ready for the given event within timeout_ms.
 */
bool net_wait_readable(Sock s, int timeout_ms);
bool net_wait_writable(Sock s, int timeout_ms);

/* ================================================================
 *  Connection pool (backed by barepool.h if available, else malloc)
 * ================================================================ */
typedef struct {
        Sock*    socks; /* array of pooled connections */
        bool*    in_use;
        int      cap;
        Str      host;
        uint16_t port;
        Arena*   arena;
} ConnPool;

ConnPool* conn_pool_new(Arena* a, Str host, uint16_t port, int cap);
Sock      conn_pool_get(ConnPool* p); /* borrow; SOCK_INVALID if all busy */
void      conn_pool_put(ConnPool* p, Sock s); /* return to pool */
void      conn_pool_close_all(ConnPool* p);

#        ifdef BARENET_IMPLEMENTATION
#                include <stdio.h>
#                include <string.h>

static char _bn_errbuf[128];
Str         net_last_err(void) {
#                ifdef _BN_WIN
        int e = WSAGetLastError();
        int n = snprintf(_bn_errbuf, sizeof _bn_errbuf, "WSA error %d", e);
#                else
        int n = snprintf(_bn_errbuf, sizeof _bn_errbuf, "%s", strerror(errno));
#                endif
        return str_buf(_bn_errbuf, (size_t)(n > 0 ? n : 0));
}
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
bool net_resolve(
    Arena* a, Str host, uint16_t port, NetFamily family, NetAddr* out) {
        char   hostbuf[256], portbuf[8];
        size_t hl = host.len < 255 ? host.len : 255;
        memcpy(hostbuf, host.ptr, hl);
        hostbuf[hl] = '\0';
        snprintf(portbuf, sizeof portbuf, "%u", (unsigned)port);
        struct addrinfo hints, *res = NULL;
        memset(&hints, 0, sizeof hints);
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_family   = (family == NET_IPV6) ? AF_INET6 : AF_INET;
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
static Sock _bn_bind_str(Str host, uint16_t port, int type) {
        char   hostbuf[256], portbuf[8];
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
Sock net_tcp_connect(Arena* a, Str host, uint16_t port) {
        NetAddr addr;
        if (!net_resolve(a, host, port, NET_IPV4, &addr))
                if (!net_resolve(a, host, port, NET_IPV6, &addr))
                        return SOCK_INVALID;
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
        _BnFd                   fd;
        do {
                fd = accept(srv._fd, (struct sockaddr*)&ss, &len);
        } while (fd == SOCK_INVALID_FD && errno == EINTR);
        if (fd == SOCK_INVALID_FD) return SOCK_INVALID;
        if (peer) {
                size_t cp = len < sizeof peer->_sa ? len : sizeof peer->_sa;
                memcpy(peer->_sa, &ss, cp);
                peer->_len = (int)len;
        }
        /* Disable Nagle so small responses aren't held waiting for more data */
        {
                int t = 1;
                setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (void*)&t, sizeof t);
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
        return (int)recv(s._fd, (char*)buf, (int)n, 0);
#                else
        ssize_t r;
        do {
                r = recv(s._fd, buf, n, 0);
        } while (r < 0 && errno == EINTR);
        return (int)r;
#                endif
}
bool net_send_msg(Sock s, const void* data, uint32_t len) {
        uint8_t hdr[4];
        hdr[0] = (uint8_t)len;
        hdr[1] = (uint8_t)(len >> 8);
        hdr[2] = (uint8_t)(len >> 16);
        hdr[3] = (uint8_t)(len >> 24);
        return net_send(s, hdr, 4) && net_send(s, data, len);
}
Str net_recv_msg(Arena* a, Sock s) {
        uint8_t hdr[4];
        size_t  got = 0;
        while (got < 4) {
                int r = net_recv(s, hdr + got, 4 - got);
                if (r <= 0) return str_null();
                got += (size_t)r;
        }
        uint32_t len = (uint32_t)hdr[0] | ((uint32_t)hdr[1] << 8) |
                       ((uint32_t)hdr[2] << 16) | ((uint32_t)hdr[3] << 24);
        if (len == 0) return str_buf("", 0);
        char* buf = arena_push_array(a, char, len);
        got       = 0;
        while (got < len) {
                int r = net_recv(s, buf + got, len - got);
                if (r <= 0) return str_null();
                got += (size_t)r;
        }
        return str_buf(buf, len);
}
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

/* ---- net_poll ------------------------------------------ */
#                if !defined(_WIN32) && !defined(_WIN64)
#                        include <poll.h>
#                endif

int net_poll(NetPollFd* fds, int nfds, int timeout_ms) {
#                ifdef _BN_WIN
        /* Windows: WSAPoll */
        WSAPOLLFD* wfds = (WSAPOLLFD*)malloc(sizeof(WSAPOLLFD) * (size_t)nfds);
        if (!wfds) return -1;
        int i;
        for (i = 0; i < nfds; i++) {
                wfds[i].fd     = fds[i].sock._fd;
                wfds[i].events = 0;
                if (fds[i].events & NET_POLL_IN) wfds[i].events |= POLLRDNORM;
                if (fds[i].events & NET_POLL_OUT) wfds[i].events |= POLLWRNORM;
        }
        int r = WSAPoll(wfds, (ULONG)nfds, timeout_ms);
        for (i = 0; i < nfds; i++) {
                fds[i].revents = 0;
                if (wfds[i].revents & POLLRDNORM) fds[i].revents |= NET_POLL_IN;
                if (wfds[i].revents & POLLWRNORM)
                        fds[i].revents |= NET_POLL_OUT;
                if (wfds[i].revents & POLLERR) fds[i].revents |= NET_POLL_ERR;
                if (wfds[i].revents & POLLHUP) fds[i].revents |= NET_POLL_HUP;
        }
        free(wfds);
        return r;
#                else
        struct pollfd* pfds =
            (struct pollfd*)malloc(sizeof(struct pollfd) * (size_t)nfds);
        if (!pfds) return -1;
        int i;
        for (i = 0; i < nfds; i++) {
                pfds[i].fd     = fds[i].sock._fd;
                pfds[i].events = 0;
                if (fds[i].events & NET_POLL_IN) pfds[i].events |= POLLIN;
                if (fds[i].events & NET_POLL_OUT) pfds[i].events |= POLLOUT;
        }
        int r;
        do {
                r = poll(pfds, (nfds_t)nfds, timeout_ms);
        } while (r < 0 && errno == EINTR);
        for (i = 0; i < nfds; i++) {
                fds[i].revents = 0;
                if (pfds[i].revents & POLLIN) fds[i].revents |= NET_POLL_IN;
                if (pfds[i].revents & POLLOUT) fds[i].revents |= NET_POLL_OUT;
                if (pfds[i].revents & POLLERR) fds[i].revents |= NET_POLL_ERR;
                if (pfds[i].revents & POLLHUP) fds[i].revents |= NET_POLL_HUP;
        }
        free(pfds);
        return r;
#                endif
}

bool net_wait_readable(Sock s, int timeout_ms) {
        NetPollFd pfd;
        pfd.sock    = s;
        pfd.events  = NET_POLL_IN;
        pfd.revents = 0;
        int r       = net_poll(&pfd, 1, timeout_ms);
        return r > 0 && (pfd.revents & NET_POLL_IN);
}
bool net_wait_writable(Sock s, int timeout_ms) {
        NetPollFd pfd;
        pfd.sock    = s;
        pfd.events  = NET_POLL_OUT;
        pfd.revents = 0;
        int r       = net_poll(&pfd, 1, timeout_ms);
        return r > 0 && (pfd.revents & NET_POLL_OUT);
}

/* ---- Connection pool ------------------------------------ */
ConnPool* conn_pool_new(Arena* a, Str host, uint16_t port, int cap) {
        ConnPool* p = arena_push_type(a, ConnPool);
        p->socks    = arena_push_array(a, Sock, (size_t)cap);
        p->in_use   = arena_push_array(a, bool, (size_t)cap);
        p->cap      = cap;
        p->host     = host;
        p->port     = port;
        p->arena    = a;
        int i;
        for (i = 0; i < cap; i++) {
                p->socks[i]  = SOCK_INVALID;
                p->in_use[i] = false;
        }
        return p;
}
Sock conn_pool_get(ConnPool* p) {
        int i;
        for (i = 0; i < p->cap; i++) {
                if (p->in_use[i]) continue;
                /* Lazy connect */
                if (!sock_valid(p->socks[i]))
                        p->socks[i] =
                            net_tcp_connect(p->arena, p->host, p->port);
                if (!sock_valid(p->socks[i])) continue;
                p->in_use[i] = true;
                return p->socks[i];
        }
        return SOCK_INVALID;
}
void conn_pool_put(ConnPool* p, Sock s) {
        int i;
        for (i = 0; i < p->cap; i++) {
                if (p->socks[i]._fd == s._fd) {
                        p->in_use[i] = false;
                        return;
                }
        }
}
void conn_pool_close_all(ConnPool* p) {
        int i;
        for (i = 0; i < p->cap; i++) {
                if (sock_valid(p->socks[i])) {
                        net_close(p->socks[i]);
                        p->socks[i] = SOCK_INVALID;
                }
                p->in_use[i] = false;
        }
}

#        endif /* BARENET_IMPLEMENTATION */
#endif         /* BARENET_H */
