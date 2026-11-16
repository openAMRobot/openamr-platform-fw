/* SPDX-License-Identifier: MIT */
/*
 * Size probe, NOT FIRMWARE. LwIP options for a super-loop (NO_SYS = 1, raw
 * API, UDP only, static IPv4, no DHCP, no TCP). Pool sizes are probe values
 * chosen to hold a few 512-byte XRCE datagrams; the owner sizes the real ones.
 */
#ifndef LWIPOPTS_H
#define LWIPOPTS_H
#define NO_SYS                      1
#define SYS_LIGHTWEIGHT_PROT        0
#define LWIP_NETCONN                0
#define LWIP_SOCKET                 0
#define LWIP_IPV4                   1
#define LWIP_IPV6                   0
#define LWIP_TCP                    0
#define LWIP_UDP                    1
#define LWIP_ICMP                   1
#define LWIP_DHCP                   0
#define LWIP_ARP                    1
#define LWIP_RAW                    0
#define LWIP_STATS                  0
#define LWIP_NETIF_LINK_CALLBACK    1
#define LWIP_NETIF_STATUS_CALLBACK  1
#define MEM_ALIGNMENT               4
#define MEM_SIZE                    (8 * 1024)
#define MEMP_NUM_PBUF               8
#define MEMP_NUM_UDP_PCB            4
#define PBUF_POOL_SIZE              8
#define PBUF_POOL_BUFSIZE           1536
#define ETH_PAD_SIZE                0
#define CHECKSUM_GEN_IP             0  /* the H7 MAC inserts checksums */
#define CHECKSUM_GEN_UDP            0
#define CHECKSUM_GEN_ICMP           0
#define CHECKSUM_CHECK_IP           0
#define CHECKSUM_CHECK_UDP          0
#define CHECKSUM_CHECK_ICMP         0
#define LWIP_TIMERS                 1
#endif
