#define BARESTD_IMPLEMENTATION
#define BARENET_IMPLEMENTATION
#include "barenet.h"

int main(void) {
        net_init();
        Arena* a = arena_new(KB(16));

        /* --- 1. net_resolve --- */
        NetAddr addr;
        bool ok = net_resolve(a, str_lit("127.0.0.1"), 12345, NET_IPV4, &addr);
        printf("resolve 127.0.0.1:12345 = %s\n", ok ? "ok" : "fail");
        if (ok) {
                char abuf[NETADDR_STR_MAX];
                Str  as = net_addr_str(&addr, abuf);
                printf("addr_str = \"" StrFmt "\"\n", StrArgs(as));
                printf("port     = %u\n", net_addr_port(&addr));
        }

        /* --- 2. TCP loopback: listen + connect + accept --- */
        Sock srv = net_tcp_listen(str_lit("127.0.0.1"), 19876, 1);
        if (!sock_valid(srv)) {
                printf("listen failed (port 19876 in use?)\n");
                goto udp;
        }
        printf("listening on 127.0.0.1:19876\n");

        Sock cli = net_tcp_connect(a, str_lit("127.0.0.1"), 19876);
        printf("connect: %s\n", sock_valid(cli) ? "ok" : "fail");

        NetAddr peer;
        Sock    conn = net_accept(srv, &peer);
        printf("accept: %s\n", sock_valid(conn) ? "ok" : "fail");
        if (sock_valid(conn)) {
                char pbuf[NETADDR_STR_MAX];
                Str  ps = net_addr_str(&peer, pbuf);
                printf("peer addr: \"" StrFmt "\"\n", StrArgs(ps));
        }

        /* --- 3. net_send / net_recv --- */
        const char ping[] = "ping";
        bool       sent   = net_send(conn, ping, 4);
        printf("send 'ping': %s\n", sent ? "ok" : "fail");

        char rbuf[8];
        memset(rbuf, 0, sizeof rbuf);
        int nr = net_recv(cli, rbuf, 4);
        printf("recv %d bytes: \"%.4s\"\n", nr, rbuf);

        /* --- 4. net_send_msg / net_recv_msg (framed, length-prefixed) --- */
        net_send_msg(conn, "frame!", 6);
        Str framed = net_recv_msg(a, cli);
        printf("framed msg: \"" StrFmt "\"\n", StrArgs(framed));

        /* Empty framed message */
        net_send_msg(conn, "", 0);
        Str empty_frame = net_recv_msg(a, cli);
        printf("empty frame: len=%zu\n", empty_frame.len);

        /* --- 5. sock_valid predicate --- */
        printf("sock_valid(cli)     = %d\n", sock_valid(cli));
        printf("sock_valid(INVALID) = %d\n", sock_valid(SOCK_INVALID));

        /* --- 6. Socket options --- */
        net_set_nodelay(cli, true);
        net_set_keepalive(cli, true);
        net_set_nonblocking(cli, false);
        net_set_reuseaddr(srv, true);
        net_set_recvtimeo(conn, 50);
        net_set_sendtimeo(conn, 50);
        printf("socket options applied\n");

        net_close(cli);
        net_close(conn);
        net_close(srv);

udp:
        /* --- 7. UDP: bind + socket + sendto + recvfrom --- */
        Sock us = net_udp_bind(str_lit("127.0.0.1"), 19877);
        Sock uc = net_udp_socket(NET_IPV4);
        printf("UDP bind: %s  socket: %s\n",
               sock_valid(us) ? "ok" : "fail",
               sock_valid(uc) ? "ok" : "fail");

        if (sock_valid(us) && sock_valid(uc)) {
                NetAddr udp_dst;
                net_resolve(a, str_lit("127.0.0.1"), 19877, NET_IPV4, &udp_dst);
                int sent_n = net_sendto(uc, "udp!", 4, udp_dst);
                printf("sendto: %d bytes\n", sent_n);

                char ubuf[8];
                memset(ubuf, 0, sizeof ubuf);
                net_set_recvtimeo(us, 100);
                NetAddr from;
                int     recv_n = net_recvfrom(us, ubuf, sizeof ubuf - 1, &from);
                if (recv_n > 0) {
                        char fb[NETADDR_STR_MAX];
                        Str  fs = net_addr_str(&from, fb);
                        printf("recvfrom %d bytes: \"%.*s\"  from=\"" StrFmt
                               "\"\n",
                               recv_n,
                               recv_n,
                               ubuf,
                               StrArgs(fs));
                } else {
                        printf("recvfrom: no data (timeout)\n");
                }
        }

        if (sock_valid(us)) net_close(us);
        if (sock_valid(uc)) net_close(uc);

        /* --- 8. net_last_err (call after a failure) --- */
        Sock bad = net_tcp_connect(a, str_lit("127.0.0.1"), 1); /* refused */
        if (!sock_valid(bad)) {
                Str err = net_last_err();
                printf("last_err after refused connect: \"" StrFmt "\"\n",
                       StrArgs(err));
        }

        /* --- 9. net_set_reuseport --- */
        Sock rp = net_tcp_listen(str_lit("127.0.0.1"), 19878, 1);
        if (sock_valid(rp)) {
                net_set_reuseport(rp, true);
                printf("set_reuseport: ok\n");
                net_close(rp);
        }

        net_cleanup();
        arena_free(a);
        printf("done.\n");
        return 0;
}
