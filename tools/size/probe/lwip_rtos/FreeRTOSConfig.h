/* SPDX-License-Identifier: MIT */
/*
 * Size probe, NOT FIRMWARE. FreeRTOS configuration for the probe image.
 * Values are probe choices, not proposals: kernel heap 8 KiB (LwIP
 * mailboxes, semaphores, tcpip thread), micro-ROS heap 32 KiB as in the
 * super-loop probes, 1 kHz tick, stack overflow check 2,
 * static and dynamic allocation both on (the upstream micro-ROS memory
 * manager requires dynamic allocation).
 */
#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H
#include <stdint.h>
extern uint32_t SystemCoreClock;
#define configCPU_CLOCK_HZ                      (SystemCoreClock)
#define configTICK_RATE_HZ                      1000
#define configUSE_PREEMPTION                    1
#define configUSE_TIME_SLICING                  1
#define configUSE_PORT_OPTIMISED_TASK_SELECTION 1
#define configUSE_TICKLESS_IDLE                 0
#define configMAX_PRIORITIES                    8
#define configMINIMAL_STACK_SIZE                128
#define configMAX_TASK_NAME_LEN                 12
#define configTICK_TYPE_WIDTH_IN_BITS           TICK_TYPE_WIDTH_32_BITS
#define configIDLE_SHOULD_YIELD                 1
#define configTASK_NOTIFICATION_ARRAY_ENTRIES   1
#define configQUEUE_REGISTRY_SIZE               0
#define configUSE_MUTEXES                       1
#define configUSE_RECURSIVE_MUTEXES             1
#define configUSE_COUNTING_SEMAPHORES           1
#define configSUPPORT_STATIC_ALLOCATION         1
#define configSUPPORT_DYNAMIC_ALLOCATION        1
/* The upstream micro-ROS custom_memory_manager.c is a second heap_4 with its
 * own ucHeap[configTOTAL_HEAP_SIZE]. The probe compiles that file with
 * PROBE_UROS_HEAP_BYTES so the two heaps can be sized separately. */
#if defined(PROBE_UROS_HEAP_BYTES)
#define configTOTAL_HEAP_SIZE                   (PROBE_UROS_HEAP_BYTES)
#else
#define configTOTAL_HEAP_SIZE                   (8 * 1024)
#endif
#define configAPPLICATION_ALLOCATED_HEAP        0
#define configUSE_IDLE_HOOK                     0
#define configUSE_TICK_HOOK                     0
#define configCHECK_FOR_STACK_OVERFLOW          2
#define configUSE_MALLOC_FAILED_HOOK            1
#define configUSE_TRACE_FACILITY                0
#define configGENERATE_RUN_TIME_STATS           0
#define configUSE_TIMERS                        1
#define configTIMER_TASK_PRIORITY               (configMAX_PRIORITIES - 1)
#define configTIMER_QUEUE_LENGTH                8
#define configTIMER_TASK_STACK_DEPTH            256
#define configPRIO_BITS                         4
#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY         15
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY    5
#define configKERNEL_INTERRUPT_PRIORITY         (configLIBRARY_LOWEST_INTERRUPT_PRIORITY << (8 - configPRIO_BITS))
#define configMAX_SYSCALL_INTERRUPT_PRIORITY    (configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << (8 - configPRIO_BITS))
#define configASSERT(x) do { if ((x) == 0) { for (;;) { } } } while (0)
#define INCLUDE_vTaskDelay                      1
#define INCLUDE_vTaskSuspend                    1
#define INCLUDE_xTaskDelayUntil                 1
#define INCLUDE_vTaskDelete                     1
#define INCLUDE_xTaskGetSchedulerState          1
#define INCLUDE_xTaskGetCurrentTaskHandle       1
#define INCLUDE_uxTaskGetStackHighWaterMark     1
#define vPortSVCHandler    SVC_Handler
#define xPortPendSVHandler PendSV_Handler
#define xPortSysTickHandler SysTick_Handler
#endif
