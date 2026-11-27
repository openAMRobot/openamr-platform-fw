/* Thin Cortex-M7 exception veneers. At entry, LR is the original EXC_RETURN;
 * bit 2 selects the interrupted stack. No frame data is changed here. */
    .syntax unified
    .cpu cortex-m7
    .fpu fpv5-d16
    .thumb

    .section .text.fault_capture_hard_fault,"ax",%progbits
    .global HardFault_Handler
    .type HardFault_Handler, %function
    .thumb_func
HardFault_Handler:
    tst lr, #4
    ite eq
    mrseq r0, msp
    mrsne r0, psp
    movs r2, #1
    b fault_capture_from_exception
    .size HardFault_Handler, .-HardFault_Handler

    .section .text.fault_capture_mem_manage,"ax",%progbits
    .global MemManage_Handler
    .type MemManage_Handler, %function
    .thumb_func
MemManage_Handler:
    tst lr, #4
    ite eq
    mrseq r0, msp
    mrsne r0, psp
    movs r2, #2
    b fault_capture_from_exception
    .size MemManage_Handler, .-MemManage_Handler

    .section .text.fault_capture_bus_fault,"ax",%progbits
    .global BusFault_Handler
    .type BusFault_Handler, %function
    .thumb_func
BusFault_Handler:
    tst lr, #4
    ite eq
    mrseq r0, msp
    mrsne r0, psp
    movs r2, #3
    b fault_capture_from_exception
    .size BusFault_Handler, .-BusFault_Handler

    .section .text.fault_capture_usage_fault,"ax",%progbits
    .global UsageFault_Handler
    .type UsageFault_Handler, %function
    .thumb_func
UsageFault_Handler:
    tst lr, #4
    ite eq
    mrseq r0, msp
    mrsne r0, psp
    movs r2, #4
    b fault_capture_from_exception
    .size UsageFault_Handler, .-UsageFault_Handler
