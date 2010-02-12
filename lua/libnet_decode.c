/*
Copyright (C) 2010 Wurldtech Security Technologies All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

1. Redistributions of source code must retain the above copyright
notice, this list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright
notice, this list of conditions and the following disclaimer in the
documentation and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
PURPOSE ARE DISCLAIMED.IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <libnet.h>

int libnet_decode_tcp(const uint8_t* pkt, size_t pkt_s, libnet_t *l)
{
    const struct libnet_tcp_hdr* tcp_hdr = (const struct libnet_tcp_hdr*) pkt;
    const uint8_t* payload = pkt + tcp_hdr->th_off * 4;
    size_t payload_s = pkt + pkt_s - payload;
    const uint8_t* options = pkt + LIBNET_TCP_H;
    size_t options_s = payload - options;
    int otag;
    int ptag;

    if(options_s == 0)
        options = 0;

    otag = libnet_build_tcp_options(options, options_s, l, 0);

    if(otag < 0)
        return otag;

    ptag = libnet_build_tcp(
            ntohs(tcp_hdr->th_sport),
            ntohs(tcp_hdr->th_dport),
            ntohl(tcp_hdr->th_seq),
            ntohl(tcp_hdr->th_ack),
            tcp_hdr->th_flags,
            ntohs(tcp_hdr->th_win),
            ntohs(tcp_hdr->th_sum),
            ntohs(tcp_hdr->th_urp),
            pkt_s,
            payload, payload_s,
            l, 0);

    if(ptag > 0)
        libnet_pblock_setflags(libnet_pblock_find(l, ptag), LIBNET_PBLOCK_DO_CHECKSUM);

    return ptag;
}

int libnet_decode_udp(const uint8_t* pkt, size_t pkt_s, libnet_t *l)
{
    const struct libnet_udp_hdr* udp_hdr = (const struct libnet_udp_hdr*) pkt;
    const uint8_t* payload = pkt + LIBNET_UDP_H;
    size_t payload_s = pkt + pkt_s - payload;
    int ptag;

    ptag = libnet_build_udp(
            ntohs(udp_hdr->uh_sport),
            ntohs(udp_hdr->uh_dport),
            ntohs(udp_hdr->uh_ulen),
            ntohs(udp_hdr->uh_sum),
            payload, payload_s,
            l, 0);

    if(ptag > 0)
        libnet_pblock_setflags(libnet_pblock_find(l, ptag), LIBNET_PBLOCK_DO_CHECKSUM);

    return ptag;
}

int libnet_decode_ipv4(const uint8_t* pkt, size_t pkt_s, libnet_t *l)
{
    const struct libnet_ipv4_hdr* ip_hdr = (const struct libnet_ipv4_hdr*) pkt;
    const uint8_t* payload = pkt + ip_hdr->ip_hl * 4;
    size_t payload_s = pkt + pkt_s - payload;
    int ptag = 0; /* payload tag */
    int otag = 0; /* options tag */
    int itag = 0; /* ip tag */

    /* This could be table-based */
    switch(ip_hdr->ip_p) {
        case IPPROTO_UDP:
            ptag = libnet_decode_udp(payload, payload_s, l);
            break;
        case IPPROTO_TCP:
            ptag = libnet_decode_tcp(payload, payload_s, l);
            break;
        default:
            ptag = libnet_build_data((void*)payload, payload_s, l, 0);
            break;
    }

    if(ptag < 0) return ptag;

    if(ip_hdr->ip_hl > 5) {
        payload = pkt + LIBNET_TCP_H;
        payload_s = ip_hdr->ip_hl * 4 - LIBNET_TCP_H;
        otag = libnet_build_ipv4_options((void*)payload, payload_s, l, 0);
        if(otag < 0) {
            return otag;
        }
    }

    itag = libnet_build_ipv4(
            ntohs(ip_hdr->ip_len),
            ip_hdr->ip_tos,
            ntohs(ip_hdr->ip_id),
            ntohs(ip_hdr->ip_off),
            ip_hdr->ip_ttl,
            ip_hdr->ip_p,
            ntohs(ip_hdr->ip_sum),
            ip_hdr->ip_src.s_addr,
            ip_hdr->ip_dst.s_addr,
            NULL, 0, /* payload already pushed */
            l, 0
            );

    if(itag > 0)
        libnet_pblock_setflags(libnet_pblock_find(l, itag), LIBNET_PBLOCK_DO_CHECKSUM);

    return itag;
}

int libnet_decode_ip(const uint8_t* pkt, size_t pkt_s, libnet_t *l)
{
    const struct libnet_ipv4_hdr* ip_hdr = (const struct libnet_ipv4_hdr*) pkt;

    switch(ip_hdr->ip_v) {
        case 4:
            return libnet_decode_ipv4(pkt, pkt_s, l);
        /* TODO - IPv6 */
    }

    return libnet_build_data((void*)pkt, pkt_s, l, 0);
}

int libnet_decode_eth(const uint8_t* pkt, size_t pkt_s, libnet_t *l)
{
    const struct libnet_ethernet_hdr* hdr = (const struct libnet_ethernet_hdr*) pkt;
    const uint8_t* payload = pkt + LIBNET_ETH_H;
    size_t payload_s = pkt + pkt_s - payload;
    int ptag = 0; /* payload tag */
    int etag = 0; /* eth tag */

    switch(ntohs(hdr->ether_type)) {
        case ETHERTYPE_IP:
            ptag = libnet_decode_ipv4(payload, payload_s, l);
            payload_s = 0;
            break;
    }

    if(ptag < 0) return ptag;

    etag = libnet_build_ethernet(
            hdr->ether_dhost,
            hdr->ether_shost,
            ntohs(hdr->ether_type),
            payload, payload_s,
            l, 0
            );

    return etag;
}


