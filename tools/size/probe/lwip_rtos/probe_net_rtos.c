/* SPDX-License-Identifier: MIT */
/*
 * Size probe, NOT FIRMWARE. FreeRTOS variant: LwIP tcpip thread plus BSD
 * sockets UDP transport (the pattern of the upstream micro-ROS UDP
 * transport), the upstream micro-ROS heap4-based allocator, and three tasks:
 * control (highest), micro-ROS, and the tcpip thread created by LwIP.
 */
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"

#include "lwip/etharp.h"
#include "lwip/netif.h"
#include "lwip/netifapi.h"
#include "lwip/sockets.h"
#include "lwip/tcpip.h"
#include "netif/ethernet.h"

#include <rcl/allocator.h>
#include <rcutils/allocator.h>

#include "probe_transport.h"

#define PROBE_AGENT_PORT 8888

static struct netif s_netif;
static int s_sock = -1;
volatile uint32_t g_probe_tx;
volatile uint32_t g_probe_ticks;

void *microros_allocate(size_t size, void *state);
void microros_deallocate(void *pointer, void *state);
void *microros_reallocate(void *pointer, size_t size, void *state);
void *microros_zero_allocate(size_t n, size_t size, void *state);

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
    IP4_ADDR(&ip, 192, 0, 2, 10);
    IP4_ADDR(&mask, 255, 255, 255, 0);
    IP4_ADDR(&gw, 0, 0, 0, 0);
    tcpip_init(NULL, NULL);
    netifapi_netif_add(&s_netif, &ip, &mask, &gw, NULL, probe_netif_init, tcpip_input);
    netifapi_netif_set_default(&s_netif);
    netifapi_netif_set_up(&s_netif);
    netifapi_netif_set_link_up(&s_netif);
}

bool probe_transport_open(struct uxrCustomTransport *t)
{
    (void)t;
    s_sock = lwip_socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_port = lwip_htons(PROBE_AGENT_PORT);
    a.sin_addr.s_addr = lwip_htonl(INADDR_ANY);
    return s_sock >= 0 && lwip_bind(s_sock, (struct sockaddr *)&a, sizeof a) == 0;
}

bool probe_transport_close(struct uxrCustomTransport *t)
{
    (void)t;
    if (s_sock >= 0) {
        lwip_close(s_sock);
        s_sock = -1;
    }
    return true;
}

size_t probe_transport_write(struct uxrCustomTransport *t, const uint8_t *buf, size_t len, uint8_t *err)
{
    (void)t; (void)err;
    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_port = lwip_htons(PROBE_AGENT_PORT);
    a.sin_addr.s_addr = lwip_htonl(0xC0000201u);
    int r = lwip_sendto(s_sock, buf, len, 0, (struct sockaddr *)&a, sizeof a);
    return r > 0 ? (size_t)r : 0;
}

size_t probe_transport_read(struct uxrCustomTransport *t, uint8_t *buf, size_t len, int timeout, uint8_t *err)
{
    (void)t; (void)err;
    struct timeval tv = { .tv_sec = timeout / 1000, .tv_usec = (timeout % 1000) * 1000 };
    lwip_setsockopt(s_sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    int r = lwip_recv(s_sock, buf, len, 0);
    return r > 0 ? (size_t)r : 0;
}

void probe_set_allocator(void)
{
    rcl_allocator_t a = rcutils_get_zero_initialized_allocator();
    a.allocate = microros_allocate;
    a.deallocate = microros_deallocate;
    a.reallocate = microros_reallocate;
    a.zero_allocate = microros_zero_allocate;
    (void)rcutils_set_default_allocator(&a);
}

void probe_idle(void) { vTaskDelay(1); }

int probe_uros_session(void);

static void control_task(void *arg)
{
    (void)arg;
    TickType_t last = xTaskGetTickCount();
    for (;;) {
        (void)xTaskDelayUntil(&last, 1);     /* a real control task waits on a timer notification */
        g_probe_ticks++;
    }
}

static void uros_task(void *arg)
{
    (void)arg;
    probe_net_init();
    for (;;) {
        (void)probe_uros_session();
        vTaskDelay(500);
    }
}

void vApplicationStackOverflowHook(TaskHandle_t t, char *name) { (void)t; (void)name; for (;;) { } }
void vApplicationMallocFailedHook(void) { for (;;) { } }

static StaticTask_t s_idle_tcb;
static StackType_t s_idle_stack[configMINIMAL_STACK_SIZE];
void vApplicationGetIdleTaskMemory(StaticTask_t **tcb, StackType_t **stk, configSTACK_DEPTH_TYPE *n)
{
    *tcb = &s_idle_tcb; *stk = s_idle_stack; *n = configMINIMAL_STACK_SIZE;
}
static StaticTask_t s_timer_tcb;
static StackType_t s_timer_stack[configTIMER_TASK_STACK_DEPTH];
void vApplicationGetTimerTaskMemory(StaticTask_t **tcb, StackType_t **stk, configSTACK_DEPTH_TYPE *n)
{
    *tcb = &s_timer_tcb; *stk = s_timer_stack; *n = configTIMER_TASK_STACK_DEPTH;
}

/* Control task 2 KiB and micro-ROS task 16 KiB as static stacks, so they
 * show in .bss; the upstream minimum for the micro-ROS task is 10 kB. */
static StaticTask_t s_ctl_tcb, s_uros_tcb;
static StackType_t s_ctl_stack[512];
static StackType_t s_uros_stack[4096];

int main(void)
{
    (void)xTaskCreateStatic(control_task, "ctl", 512, NULL, configMAX_PRIORITIES - 2, s_ctl_stack, &s_ctl_tcb);
    (void)xTaskCreateStatic(uros_task, "uros", 4096, NULL, 2, s_uros_stack, &s_uros_tcb);
    vTaskStartScheduler();
    for (;;) { }
}
