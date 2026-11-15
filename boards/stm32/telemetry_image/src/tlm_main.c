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
#ifdef TLM_UROS
#include "tlm_uros.h"
#endif

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
static StaticTask_t s_tick_tcb, s_can1_tcb, s_aux_tcb, s_sensors_tcb, s_reporter_tcb, s_idle_tcb;
static StackType_t s_tick_stack[TLM_STACK_CAN1_TICK];
static StackType_t s_can1_stack[TLM_STACK_CAN1_READER];
static StackType_t s_aux_stack[TLM_STACK_CAN_AUX];
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

/* Idle: the processor sleeps until the next interrupt (SysTick, FDCAN, a UART) instead of
 * spinning. WFI only stops the core clock; no peripheral, clock or output is changed, and every
 * interrupt that wakes it is one that would have preempted the idle loop anyway. The DSB lets
 * outstanding memory accesses finish first (ARM Cortex-M7 programming manual, WFI). */
void vApplicationIdleHook(void)
{
    __DSB();
    __WFI();
}

void vApplicationStackOverflowHook(TaskHandle_t task, char *name)
{
    (void)task;
    (void)name;
    tlm_fatal("stack_overflow");
}

static TaskHandle_t s_task[TLM_N_TASKS];
static const char *const s_task_name[TLM_N_TASKS] = {"can1_tick", "can1_rx", "can_aux", "sensors",
                                                     "reporter", "idle"};

void tlm_stack_marks(tlm_stack_marks_t *out)
{
    for (unsigned i = 0u; i < TLM_N_TASKS; i++)
    {
        out->name[i] = s_task_name[i];
        const TaskHandle_t t = (i == TLM_N_TASKS - 1u) ? xTaskGetIdleTaskHandle() : s_task[i];
        out->free_words[i] = t != 0 ? (uint32_t)uxTaskGetStackHighWaterMark(t) : 0u;
    }
}

/*
 * Fatal stop of this diagnostic image (stack overflow, failed assert, the
 * scheduler returning). It prints one record and halts in a loop with
 * interrupts disabled, so the record stays the last thing on the console for
 * the person at the bench.
 *
 * The halt is a property of this diagnostic image only. It is NOT watchdog
 * behaviour and must not be read as one: no watchdog (independent, window or a
 * software one) is configured, fed or starved here, nothing resets the MCU,
 * and no output is switched; the outputs keep the levels they had. Any
 * reaction of the robot to a stopped controller belongs to the hardwired
 * safety chain, not to this image.
 *
 * The console wait is bounded for the whole record (TLM_FATAL_TX_SPIN_MAX
 * polls of the TX-ready flag in total), so a dead UART cannot hold the
 * record back from the halt loop.
 */
void tlm_fatal(const char *what)
{
    __disable_irq();
    uint32_t budget = TLM_FATAL_TX_SPIN_MAX;
    tlm_console_write_raw_bounded("\n{\"t\":\"fatal\",\"what\":\"", &budget);
    tlm_console_write_raw_bounded(what, &budget);
    tlm_console_write_raw_bounded("\"}\n", &budget);
    for (;;)
    {
        /* Halts here by design (see above). */
    }
}

int main(void)
{
    const uint32_t rcc_rsr = tlm_board_init();
    const tlm_boot_id_t boot = boot_record_update();

    (void)tlm_console_init();
    tlm_can1_init();
    tlm_can_aux_init();
    tlm_sensors_init();
    tlm_report_banner(rcc_rsr, &boot);
#ifdef TLM_UROS
    tlm_uros_init(boot.id);
#endif

    s_task[0] = xTaskCreateStatic(tlm_can1_tick_task, "can1_tick", TLM_STACK_CAN1_TICK, 0,
                                  TLM_PRIO_CAN1_TICK, s_tick_stack, &s_tick_tcb);
    s_task[1] = xTaskCreateStatic(tlm_can1_rx_task, "can1_rx", TLM_STACK_CAN1_READER, 0,
                                  TLM_PRIO_CAN1_READER, s_can1_stack, &s_can1_tcb);
    s_task[2] = xTaskCreateStatic(tlm_can_aux_rx_task, "can_aux", TLM_STACK_CAN_AUX, 0,
                                  TLM_PRIO_CAN_AUX, s_aux_stack, &s_aux_tcb);
    s_task[3] = xTaskCreateStatic(tlm_sensors_task, "sensors", TLM_STACK_SENSORS, 0,
                                  TLM_PRIO_SENSORS, s_sensors_stack, &s_sensors_tcb);
    s_task[4] = xTaskCreateStatic(tlm_report_task, "reporter", TLM_STACK_REPORTER, 0,
                                  TLM_PRIO_REPORTER, s_reporter_stack, &s_reporter_tcb);
    s_task[5] = 0; /* idle: its handle exists once the scheduler has started */
    vTaskStartScheduler();
    tlm_fatal("scheduler_returned");
}
