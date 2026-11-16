/* SPDX-License-Identifier: MIT */
/*
 * Size probe, NOT FIRMWARE. LwIP raw API (NO_SYS = 1) netif and UDP custom
 * transport for a super-loop. The netif output is a stub: in a real port it
 * hands the frame to the HAL ETH driver (already in the baseline image) with
 * Tx buffers in RAM_D2. Receive is polled from the loop.
 */
#include <string.h>

#include "lwip/etharp.h"
#include "lwip/init.h"
#include "lwip/netif.h"
#include "lwip/pbuf.h"
#include "lwip/timeouts.h"
#include "lwip/udp.h"
#include "netif/ethernet.h"

#include "probe_transport.h"

#define PROBE_AGENT_PORT 8888u          /* upstream default; the owner sets the real port */
#define RXQ 4u

static struct netif s_netif;
static struct udp_pcb *s_pcb;
static struct pbuf *s_rxq[RXQ];
static uint32_t s_rx_head, s_rx_tail;
static ip_addr_t s_agent;
volatile uint32_t g_probe_ms;
volatile uint32_t g_probe_tx;

u32_t sys_now(void) { return g_probe_ms; }

static err_t probe_linkoutput(struct netif *n, struct pbuf *p)
{
    (void)n;
    for (struct pbuf *q = p; q != NULL; q = q->next) {
        g_probe_tx += q->len;
    }
    return ERR_OK;
}

static err_t probe_netif_init(struct netif *n)
{
    n->name[0] = 'e';
    n->name[1] = '0';
    n->output = etharp_output;
    n->linkoutput = probe_linkoutput;
    n->mtu = 1500;
    n->hwaddr_len = 6;
    n->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_ETHERNET;
    return ERR_OK;
}

void probe_net_init(void)
{
    ip4_addr_t ip, mask, gw;
    IP4_ADDR(&ip, 192, 0, 2, 10);    /* documentation range; the owner sets the real address */
    IP4_ADDR(&mask, 255, 255, 255, 0);
    IP4_ADDR(&gw, 0, 0, 0, 0);
    lwip_init();
    netif_add(&s_netif, &ip, &mask, &gw, NULL, probe_netif_init, ethernet_input);
    netif_set_default(&s_netif);
    netif_set_up(&s_netif);
    netif_set_link_up(&s_netif);
    IP_ADDR4(&s_agent, 192, 0, 2, 1);
}

static void on_udp(void *arg, struct udp_pcb *pcb, struct pbuf *p, const ip_addr_t *addr, u16_t port)
{
    (void)arg; (void)pcb; (void)addr; (void)port;
    if (s_rx_head - s_rx_tail >= RXQ) {
        pbuf_free(p);                  /* queue full: drop, never block */
        return;
    }
    s_rxq[s_rx_head % RXQ] = p;
    s_rx_head++;
}

bool probe_transport_open(struct uxrCustomTransport *t)
{
    (void)t;
    s_pcb = udp_new();
    if (s_pcb == NULL || udp_bind(s_pcb, IP_ADDR_ANY, PROBE_AGENT_PORT) != ERR_OK) {
        return false;
    }
    udp_recv(s_pcb, on_udp, NULL);
    return true;
}

bool probe_transport_close(struct uxrCustomTransport *t)
{
    (void)t;
    if (s_pcb != NULL) {
        udp_remove(s_pcb);
        s_pcb = NULL;
    }
    while (s_rx_tail != s_rx_head) {
        pbuf_free(s_rxq[s_rx_tail % RXQ]);
        s_rx_tail++;
    }
    return true;
}

size_t probe_transport_write(struct uxrCustomTransport *t, const uint8_t *buf, size_t len, uint8_t *err)
{
    (void)t;
    struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, (u16_t)len, PBUF_RAM);
    if (p == NULL) {
        *err = 1;
        return 0;
    }
    memcpy(p->payload, buf, len);
    err_t e = udp_sendto(s_pcb, p, &s_agent, PROBE_AGENT_PORT);
    pbuf_free(p);
    return e == ERR_OK ? len : 0;
}

void probe_idle(void)
{
    sys_check_timeouts();              /* ARP and IP timers; the loop also polls ETH Rx */
}

size_t probe_transport_read(struct uxrCustomTransport *t, uint8_t *buf, size_t len, int timeout, uint8_t *err)
{
    (void)t; (void)err;
    uint32_t start = g_probe_ms;
    while (s_rx_tail == s_rx_head) {
        probe_idle();
        if ((int)(g_probe_ms - start) >= timeout) {
            return 0;
        }
    }
    struct pbuf *p = s_rxq[s_rx_tail % RXQ];
    s_rx_tail++;
    size_t n = pbuf_copy_partial(p, buf, (u16_t)(len < p->tot_len ? len : p->tot_len), 0);
    pbuf_free(p);
    return n;
}

void probe_set_allocator(void) {}
