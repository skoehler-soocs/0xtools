// SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause)
// Copyright 2024-2038 Tanel Poder [0x.tools]

#include <stdio.h>
#include <stdbool.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <linux/types.h>
#include "xcapture.h"
#include "xcapture_user.h"

// socket inet connection endpoints
const char *get_connection_state(const struct socket_info *si)
{
    if (si->family == AF_UNIX) {
        return "";
    }

    if (si->protocol == IPPROTO_TCP) {
        switch (si->state) {
            case 10: return "LISTEN";      // TCP_LISTEN
            case 1:  return "ESTABLISHED"; // TCP_ESTABLISHED
            case 2:  return "SYN_SENT";    // TCP_SYN_SENT
            case 3:  return "SYN_RECV";    // TCP_SYN_RECV
            case 4:  return "FIN_WAIT1";   // TCP_FIN_WAIT1
            case 5:  return "FIN_WAIT2";   // TCP_FIN_WAIT2
            case 6:  return "TIME_WAIT";   // TCP_TIME_WAIT
            case 7:  return "CLOSE";       // TCP_CLOSE
            case 8:  return "CLOSE_WAIT";  // TCP_CLOSE_WAIT
            case 9:  return "LAST_ACK";    // TCP_LAST_ACK
            case 11: return "CLOSING";     // TCP_CLOSING
            default: return "";
        }
    }
    return "";
}

const char *format_connection(const struct socket_info *si, char *buf, size_t buflen)
{
    if (si->family == AF_UNIX) {
        const char *type;
        switch (si->socket_type) {
            case SOCK_DGRAM:     type = "UNIX-DGRAM"; break;
            case SOCK_SEQPACKET: type = "UNIX-SEQ"; break;
            case SOCK_STREAM:
            default:
                type = "UNIX-STREAM";
                break;
        }

        char self_desc[160];
        if (si->unix_inode) {
            snprintf(self_desc, sizeof(self_desc), "inode=%llu", (unsigned long long)si->unix_inode);
        } else if (si->unix_path_len > 0) {
            if (si->unix_is_abstract) {
                snprintf(self_desc, sizeof(self_desc), "@%.*s", si->unix_path_len, si->unix_path);
            } else {
                snprintf(self_desc, sizeof(self_desc), "%.*s", si->unix_path_len, si->unix_path);
            }
        } else {
            snprintf(self_desc, sizeof(self_desc), "anonymous");
        }

        char peer_desc[128];
        if (si->unix_peer_inode) {
            snprintf(peer_desc, sizeof(peer_desc), "inode=%llu",
                     (unsigned long long)si->unix_peer_inode);
        } else {
            snprintf(peer_desc, sizeof(peer_desc), "peer");
        }

        if (si->unix_peer_pid) {
            snprintf(buf, buflen, "%s %s->%s peerpid=%u", type, self_desc, peer_desc, si->unix_peer_pid);
        } else {
            snprintf(buf, buflen, "%s %s->%s", type, self_desc, peer_desc);
        }

        return buf;
    }

    char src[INET6_ADDRSTRLEN], dst[INET6_ADDRSTRLEN];
    const char *proto = (si->protocol == IPPROTO_TCP) ? "TCP" :
                        (si->protocol == IPPROTO_UDP) ? "UDP" : "[unknown]";

    if (si->family == AF_INET) {
        struct in_addr src_addr = { .s_addr = si->saddr_v4 };
        struct in_addr dst_addr = { .s_addr = si->daddr_v4 };
        inet_ntop(AF_INET, &src_addr, src, sizeof(src));
        inet_ntop(AF_INET, &dst_addr, dst, sizeof(dst));
    } else {
        inet_ntop(AF_INET6, &si->saddr_v6, src, sizeof(src));
        inet_ntop(AF_INET6, &si->daddr_v6, dst, sizeof(dst));
    }

    snprintf(buf, buflen, "%s %s:%u->%s:%u",
             proto, src, ntohs(si->sport), dst, ntohs(si->dport));

    return buf;
}
