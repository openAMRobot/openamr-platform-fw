/* SPDX-License-Identifier: MIT */
/* Size probe, NOT FIRMWARE: super-loop entry point. */
int probe_uros_session(void);
void probe_net_init(void);
int main(void)
{
#if defined(PROBE_WITH_LWIP)
    probe_net_init();
#endif
    for (;;) {
        (void)probe_uros_session();
    }
}
