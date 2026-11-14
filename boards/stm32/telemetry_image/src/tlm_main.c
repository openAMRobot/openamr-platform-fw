/* SPDX-License-Identifier: MIT */
/*
 * Telemetry image (proposal): read-only CAN1 and sensor telemetry on the
 * STM32H723ZG. Three FreeRTOS tasks with fixed priorities, all memory
 * static. The image commands nothing: it sends SDO upload requests and
 * console text, and drives no output other than the static levels of the
 * generated GPIO initialisation.
 */
#include "main.h"

#include "FreeRTOS.h"
#include "task.h"

#include "tlm.h"

/* ---- boot record: survives a reset while the RAM stays powered ---- */
/* RAM_D3 (0x38000000, 16 KiB) is declared by the generated linker script
 * and has no output section placed in it, and the start-up code does not
 * clear it, so the first words are free for a record that outlives a reset.
 * After a power cycle the record is invalid and the count restarts at 1:
 * a boot id is unique between power cycles only (see README). */
#define TLM_BOOT_RECORD_ADDR 0x38000000u
#define TLM_BOOT_RECORD_MAGIC 0x544C4D31u /* "TLM1" */

typedef struct {
    uint32_t magic;
    uint32_t count;
    uint32_t count_inverted;
} tlm_boot_record_t;

static tlm_boot_id_t boot_record_update(void)
{
    volatile tlm_boot_record_t *r = (volatile tlm_boot_record_t *)TLM_BOOT_RECORD_ADDR;
    tlm_boot_id_t id;
    const uint32_t count = r->count;
    if (r->magic == TLM_BOOT_RECORD_MAGIC && r->count_inverted == ~count && count != UINT32_MAX) {
        id.id = count + 1u;
        id.retained = true;
    } else {
        id.id = 1u;
        id.retained = false;
    }
    r->magic = TLM_BOOT_RECORD_MAGIC;
    r->count = id.id;
    r->count_inverted = ~id.id;
    return id;
}

/* ---- static task memory ---- */
static StaticTask_t s_can1_tcb, s_sensors_tcb, s_reporter_tcb, s_idle_tcb;
static StackType_t s_can1_stack[TLM_STACK_CAN1_READER];
static StackType_t s_sensors_stack[TLM_STACK_SENSORS];
static StackType_t s_reporter_stack[TLM_STACK_REPORTER];
static StackType_t s_idle_stack[configMINIMAL_STACK_SIZE];

void vApplicationGetIdleTaskMemory(StaticTask_t **tcb, StackType_t **stack,
                                   configSTACK_DEPTH_TYPE *depth)
{
    *tcb = &s_idle_tcb;
    *stack = s_idle_stack;
    *depth = configMINIMAL_STACK_SIZE;
}

void vApplicationStackOverflowHook(TaskHandle_t task, char *name)
{
    (void)task;
    (void)name;
    tlm_fatal("stack_overflow");
}

void tlm_fatal(const char *what)
{
    __disable_irq();
    tlm_console_write_raw("\n{\"t\":\"fatal\",\"what\":\"");
    tlm_console_write_raw(what);
    tlm_console_write_raw("\"}\n");
    for (;;) {
        /* Stops here by design. This image has no restart mechanism. */
    }
}

int main(void)
{
    const uint32_t rcc_rsr = tlm_board_init();
    const tlm_boot_id_t boot = boot_record_update();

    (void)tlm_console_init();
    tlm_can1_init();
    tlm_sensors_init();
    tlm_report_banner(rcc_rsr, &boot);

    (void)xTaskCreateStatic(tlm_can1_task, "can1_reader", TLM_STACK_CAN1_READER, 0,
                            TLM_PRIO_CAN1_READER, s_can1_stack, &s_can1_tcb);
    (void)xTaskCreateStatic(tlm_sensors_task, "sensors", TLM_STACK_SENSORS, 0, TLM_PRIO_SENSORS,
                            s_sensors_stack, &s_sensors_tcb);
    (void)xTaskCreateStatic(tlm_report_task, "reporter", TLM_STACK_REPORTER, 0, TLM_PRIO_REPORTER,
                            s_reporter_stack, &s_reporter_tcb);
    vTaskStartScheduler();
    tlm_fatal("scheduler_returned");
}
