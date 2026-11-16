/* SPDX-License-Identifier: MIT */
/*
 * Size probe, NOT FIRMWARE. LwIP options for FreeRTOS (NO_SYS = 0, sockets),
 * matching the upstream micro-ROS UDP transport requirements: UDP enabled,
 * MEMP_NUM_UDP_PCB 15, LWIP_SO_RCVTIMEO on, static IPv4, no DHCP. TCP off.
 * Pool sizes equal the super-loop probe so the two can be compared.
 */
#ifndef LWIPOPTS_H
#define LWIPOPTS_H
#define NO_SYS                      0
#define SYS_LIGHTWEIGHT_PROT        1
#define LWIP_NETCONN                1
#define LWIP_SOCKET                 1
#define LWIP_SO_RCVTIMEO            1
#define LWIP_NETIF_API              1
#define LWIP_COMPAT_SOCKETS         1
#define LWIP_ERRNO_STDINCLUDE       1
#define LWIP_PROVIDE_ERRNO          0
#define LWIP_POSIX_SOCKETS_IO_NAMES 0
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
#define LWIP_FREERTOS_CHECK_CORE_LOCKING 0
#define LWIP_TCPIP_CORE_LOCKING     1
#define TCPIP_THREAD_STACKSIZE      1024   /* words in the FreeRTOS port */
#define TCPIP_THREAD_PRIO           3
#define TCPIP_MBOX_SIZE             8
#define DEFAULT_UDP_RECVMBOX_SIZE   8
#define DEFAULT_THREAD_STACKSIZE    1024
#define MEM_ALIGNMENT               4
#define MEM_SIZE                    (8 * 1024)
#define MEMP_NUM_PBUF               8
#define MEMP_NUM_UDP_PCB            15
#define MEMP_NUM_NETCONN            4
#define PBUF_POOL_SIZE              8
#define PBUF_POOL_BUFSIZE           1536
#define ETH_PAD_SIZE                0
#define CHECKSUM_GEN_IP             0
#define CHECKSUM_GEN_UDP            0
#define CHECKSUM_GEN_ICMP           0
#define CHECKSUM_CHECK_IP           0
#define CHECKSUM_CHECK_UDP          0
#define CHECKSUM_CHECK_ICMP         0
#define LWIP_TIMERS                 1
#endif
