/*
 * UDP prototype streaming system
 * Copyright (c) 2000, 2001, 2002 Fabrice Bellard
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/** 
 * Adapted by Aviad Rozenhek for open-mtools
 */

#include "mtools.h"

#ifdef _MSC_VER
#ifndef snprintf
#define snprintf _snprintf
#endif
#endif

/* error handling */
#if EDOM > 0
#define AVERROR(e) (-(e))   ///< Returns a negative error code from a POSIX error code, to return from library functions.
#define AVUNERROR(e) (-(e)) ///< Returns a POSIX error code from a library function error return value.
#else
/* Some platforms have E* and errno already negated. */
#define AVERROR(e) (e)
#define AVUNERROR(e) (e)
#endif

//#define ff_neterrno() AVERROR(errno)

static int ff_neterrno(void)
{
    int err = WSAGetLastError();
    switch (err) {
    case WSAEWOULDBLOCK:
        return AVERROR(EAGAIN);
    case WSAEINTR:
        return AVERROR(EINTR);
    case WSAEPROTONOSUPPORT:
        return AVERROR(EPROTONOSUPPORT);
    case WSAETIMEDOUT:
        return AVERROR(ETIMEDOUT);
    case WSAECONNREFUSED:
        return AVERROR(ECONNREFUSED);
    case WSAEINPROGRESS:
        return AVERROR(EINPROGRESS);
    }
    return -err;
}

#define ff_neterrno() WSAGetLastError()
#define FF_NETERROR(err) WSA##err
#define WSAEAGAIN WSAEWOULDBLOCK

#ifdef _MSC_VER
#   define HAVE_WINSOCK2_H 1
#   define HAVE_STRUCT_IP_MREQ_SOURCE 1
#   define HAVE_STRUCT_SOCKADDR_IN6 1
#   define IPPROTO_IPV6 1
#endif

#define AV_LOG_ERROR    16
#define av_log(ctx, severity, format, ...) fprintf(stderr, format, __VA_ARGS__)

static void log_nameinfo(const struct sockaddr *sa, socklen_t salen)
{
    char host_buf[512], serv_buf[512];
    int err = getnameinfo(sa, salen, host_buf, sizeof(host_buf), serv_buf, sizeof(serv_buf), 0);
    if(err != 0) {
        printf("error in getnameinfo(): %d - %s", err, gai_strerror(err));
        return;
    }

    printf("host=%s, serv=%s", host_buf, serv_buf);
}


static void log_net_error(void *ctx, int level, const char* prefix)
{
    av_log(ctx, level, "%s: %d\n", prefix, ff_neterrno());
}

struct addrinfo* udp_resolve_host(const char *hostname, int port,
                                         int type, int family, int flags)
{
    struct addrinfo hints = { 0 }, *res = 0;
    int error;
    char sport[16];
    const char *node = 0, *service = "0";

    if (port > 0) {
        snprintf(sport, sizeof(sport), "%d", port);
        service = sport;
    }
    if ((hostname) && (hostname[0] != '\0') && (hostname[0] != '?')) {
        node = hostname;
    }
    hints.ai_socktype = type;
    hints.ai_family   = family;
    hints.ai_flags = flags;
    if ((error = getaddrinfo(node, service, &hints, &res))) {
        res = NULL;
        av_log(NULL, AV_LOG_ERROR, "udp_resolve_host: %s\n", gai_strerror(error));
    }

    return res;
}

static int udp_socket_create(int local_port, struct sockaddr_storage *addr,
                             socklen_t *addr_len, const char *localaddr)
{
    int udp_fd = -1;
    struct addrinfo *res0 = NULL, *res = NULL;
    int family = AF_UNSPEC;

    res0 = udp_resolve_host(localaddr[0] ? localaddr : NULL, local_port,
                            SOCK_DGRAM, family, AI_PASSIVE);
    if (res0 == 0)
        goto fail;
    for (res = res0; res; res=res->ai_next) {
        udp_fd = socket(res->ai_family, SOCK_DGRAM, 0);
        if (udp_fd != -1) break;
        log_net_error(NULL, AV_LOG_ERROR, "socket");
    }

    if (udp_fd < 0)
        goto fail;

    memcpy(addr, res->ai_addr, res->ai_addrlen);
    *addr_len = res->ai_addrlen;

    freeaddrinfo(res0);

    return udp_fd;

 fail:
    if (udp_fd >= 0)
        closesocket(udp_fd);
    if(res0)
        freeaddrinfo(res0);
    return -1;
}


int udp_join_multicast_group(int sockfd, struct sockaddr *addr)
{
#ifdef IP_ADD_MEMBERSHIP
    if (addr->sa_family == AF_INET) {
        struct ip_mreq mreq;

        mreq.imr_multiaddr.s_addr = ((struct sockaddr_in *)addr)->sin_addr.s_addr;
        mreq.imr_interface.s_addr= INADDR_ANY;
        if (setsockopt(sockfd, IPPROTO_IP, IP_ADD_MEMBERSHIP, (const void *)&mreq, sizeof(mreq)) < 0) {
            log_net_error(NULL, AV_LOG_ERROR, "setsockopt(IP_ADD_MEMBERSHIP)");
            return -1;
        }
    }
#endif
#if HAVE_STRUCT_IPV6_MREQ && defined(IPPROTO_IPV6)
    if (addr->sa_family == AF_INET6) {
        struct ipv6_mreq mreq6;

        memcpy(&mreq6.ipv6mr_multiaddr, &(((struct sockaddr_in6 *)addr)->sin6_addr), sizeof(struct in6_addr));
        mreq6.ipv6mr_interface= 0;
        if (setsockopt(sockfd, IPPROTO_IPV6, IPV6_ADD_MEMBERSHIP, &mreq6, sizeof(mreq6)) < 0) {
            log_net_error(NULL, AV_LOG_ERROR, "setsockopt(IPV6_ADD_MEMBERSHIP)");
            return -1;
        }
    }
#endif
    return 0;
}


int udp_set_multicast_sources(int sockfd, struct sockaddr *addr,
                                     int addr_len, char **sources,
                                     int nb_sources, int include)
{
#if HAVE_STRUCT_GROUP_SOURCE_REQ && defined(MCAST_BLOCK_SOURCE) && !defined(_WIN32)
    /* These ones are available in the microsoft SDK, but don't seem to work
     * as on linux, so just prefer the v4-only approach there for now. */
    int i;
    for (i = 0; i < nb_sources; i++) {
        struct group_source_req mreqs;
        int level = addr->sa_family == AF_INET ? IPPROTO_IP : IPPROTO_IPV6;
        struct addrinfo *sourceaddr = udp_resolve_host(sources[i], 0,
                                                       SOCK_DGRAM, AF_UNSPEC,
                                                       AI_NUMERICHOST);
        if (!sourceaddr)
            return AVERROR(ENOENT);

        mreqs.gsr_interface = 0;
        memcpy(&mreqs.gsr_group, addr, addr_len);
        memcpy(&mreqs.gsr_source, sourceaddr->ai_addr, sourceaddr->ai_addrlen);
        freeaddrinfo(sourceaddr);

        if (setsockopt(sockfd, level,
                       include ? MCAST_JOIN_SOURCE_GROUP : MCAST_BLOCK_SOURCE,
                       (const void *)&mreqs, sizeof(mreqs)) < 0) {
            if (include)
                log_net_error(NULL, AV_LOG_ERROR, "setsockopt(MCAST_JOIN_SOURCE_GROUP)");
            else
                log_net_error(NULL, AV_LOG_ERROR, "setsockopt(MCAST_BLOCK_SOURCE)");
            return ff_neterrno();
        }
    }
#elif HAVE_STRUCT_IP_MREQ_SOURCE && defined(IP_BLOCK_SOURCE)
    int i;
    if (addr->sa_family != AF_INET) {
        av_log(NULL, AV_LOG_ERROR,
               "Setting multicast sources only supported for IPv4\n");
        return AVERROR(EINVAL);
    }
    for (i = 0; i < nb_sources; i++) {
        struct ip_mreq_source mreqs;
        struct addrinfo *sourceaddr = udp_resolve_host(sources[i], 0,
                                                       SOCK_DGRAM, AF_UNSPEC,
                                                       AI_NUMERICHOST);
        if (!sourceaddr)
            return AVERROR(ENOENT);
        if (sourceaddr->ai_addr->sa_family != AF_INET) {
            freeaddrinfo(sourceaddr);
            av_log(NULL, AV_LOG_ERROR, "%s is of incorrect protocol family\n",
                   sources[i]);
            return AVERROR(EINVAL);
        }

        mreqs.imr_multiaddr.s_addr = ((struct sockaddr_in *)addr)->sin_addr.s_addr;
        mreqs.imr_interface.s_addr = INADDR_ANY;
        mreqs.imr_sourceaddr.s_addr = ((struct sockaddr_in *)sourceaddr->ai_addr)->sin_addr.s_addr;
        freeaddrinfo(sourceaddr);

        if (setsockopt(sockfd, IPPROTO_IP,
                       include ? IP_ADD_SOURCE_MEMBERSHIP : IP_BLOCK_SOURCE,
                       (const void *)&mreqs, sizeof(mreqs)) < 0) {
            if (include)
                log_net_error(NULL, AV_LOG_ERROR, "setsockopt(IP_ADD_SOURCE_MEMBERSHIP)");
            else
                log_net_error(NULL, AV_LOG_ERROR, "setsockopt(IP_BLOCK_SOURCE)");
            return ff_neterrno();
        }
    }
#else
    return AVERROR(ENOSYS);
#endif
    return 0;
}
