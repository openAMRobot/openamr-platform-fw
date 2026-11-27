/*
******************************************************************************
**

**  File        : LinkerScript.ld
**
**  Author		: STM32CubeMX
**
**  Abstract    : Linker script for STM32H723ZGTx series
**                1024Kbytes FLASH and 560Kbytes RAM
**
**                Set heap size, stack size and stack location according
**                to application requirements.
**
**                Set memory bank area and size if external memory is used.
**
**  Target      : STMicroelectronics STM32
**
**  Distribution: The file is distributed “as is,” without any warranty
**                of any kind.
**
*****************************************************************************
** @attention
**
** <h2><center>&copy; COPYRIGHT(c) 2025 STMicroelectronics</center></h2>
**
** Redistribution and use in source and binary forms, with or without modification,
** are permitted provided that the following conditions are met:
**   1. Redistributions of source code must retain the above copyright notice,
**      this list of conditions and the following disclaimer.
**   2. Redistributions in binary form must reproduce the above copyright notice,
**      this list of conditions and the following disclaimer in the documentation
**      and/or other materials provided with the distribution.
**   3. Neither the name of STMicroelectronics nor the names of its contributors
**      may be used to endorse or promote products derived from this software
**      without specific prior written permission.
**
** THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
** AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
** IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
** DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
** FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
** DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
** SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
** CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
** OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
** OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
**
*****************************************************************************
*/

/* Entry Point */
ENTRY(Reset_Handler)

/* Specify the memory areas */
MEMORY
{
DTCMRAM (xrw)      : ORIGIN = 0x20000000, LENGTH = 128K
RAM_D1 (xrw)      : ORIGIN = 0x24000000, LENGTH = 320K
RAM_D2_SRAM1 (xrw) : ORIGIN = 0x30000000, LENGTH = 16K
RAM_D2_SRAM2 (xrw) : ORIGIN = 0x30004000, LENGTH = 16K
RAM_D3 (xrw)      : ORIGIN = 0x38000000, LENGTH = 16K
BACKUP_SRAM (xrw) : ORIGIN = 0x38800000, LENGTH = 4K
ITCMRAM (xrw)      : ORIGIN = 0x00000000, LENGTH = 64K
FLASH (rx)      : ORIGIN = 0x8000000, LENGTH = 1024K
}

/* Generate a link error if heap and stack don't fit into RAM */
_Min_Heap_Size = 0x200;      /* required amount of heap  */
_Min_Stack_Size = 0x1000; /* required amount of stack */

/* Reserved main-stack bounds at the top of DTCMRAM */
_estack = ORIGIN(DTCMRAM) + LENGTH(DTCMRAM);
_sstack = _estack - _Min_Stack_Size;

/* Define output sections */
SECTIONS
{
  /* The startup code goes first into FLASH */
  .isr_vector :
  {
    . = ALIGN(4);
    KEEP(*(.isr_vector)) /* Startup code */
    . = ALIGN(4);
  } >FLASH

  /* The program code and other data goes into FLASH */
  .text :
  {
    . = ALIGN(4);
    *(.text)           /* .text sections (code) */
    *(.text*)          /* .text* sections (code) */
    *(.glue_7)         /* glue arm to thumb code */
    *(.glue_7t)        /* glue thumb to arm code */
    *(.eh_frame)

    KEEP (*(.init))
    KEEP (*(.fini))

    . = ALIGN(4);
    _etext = .;        /* define a global symbols at end of code */
  } >FLASH

  /* Constant data goes into FLASH */
  .rodata :
  {
    . = ALIGN(4);
    *(.rodata)         /* .rodata sections (constants, strings, etc.) */
    *(.rodata*)        /* .rodata* sections (constants, strings, etc.) */
    . = ALIGN(4);
  } >FLASH

  .ARM.extab (READONLY) : /* The "READONLY" keyword is only supported in GCC11 and later, remove it if using GCC10 or earlier. */
  {
    . = ALIGN(4);
    *(.ARM.extab* .gnu.linkonce.armextab.*)
    . = ALIGN(4);
  } >FLASH

  .ARM (READONLY) : /* The "READONLY" keyword is only supported in GCC11 and later, remove it if using GCC10 or earlier. */
  {
    . = ALIGN(4);
    __exidx_start = .;
    *(.ARM.exidx*)
    __exidx_end = .;
    . = ALIGN(4);
  } >FLASH

  .preinit_array (READONLY) : /* The "READONLY" keyword is only supported in GCC11 and later, remove it if using GCC10 or earlier. */
  {
    . = ALIGN(4);
    PROVIDE_HIDDEN (__preinit_array_start = .);
    KEEP (*(.preinit_array*))
    PROVIDE_HIDDEN (__preinit_array_end = .);
    . = ALIGN(4);
  } >FLASH

  .init_array (READONLY) : /* The "READONLY" keyword is only supported in GCC11 and later, remove it if using GCC10 or earlier. */
  {
    . = ALIGN(4);
    PROVIDE_HIDDEN (__init_array_start = .);
    KEEP (*(SORT(.init_array.*)))
    KEEP (*(.init_array*))
    PROVIDE_HIDDEN (__init_array_end = .);
    . = ALIGN(4);
  } >FLASH

  .fini_array (READONLY) : /* The "READONLY" keyword is only supported in GCC11 and later, remove it if using GCC10 or earlier. */
  {
    . = ALIGN(4);
    PROVIDE_HIDDEN (__fini_array_start = .);
    KEEP (*(SORT(.fini_array.*)))
    KEEP (*(.fini_array*))
    PROVIDE_HIDDEN (__fini_array_end = .);
    . = ALIGN(4);
  } >FLASH

  /* used by the startup to initialize data */
  _sidata = LOADADDR(.data);

  /* Ethernet descriptors occupy the first 192 bytes of D2 SRAM1. */
  .RxDescripSection (NOLOAD) :
  {
    . = ALIGN(32);
    __eth_rx_descriptors_start = .;
    KEEP(*(.RxDescripSection))
    . = ALIGN(32);
    __eth_rx_descriptors_end = .;
  } >RAM_D2_SRAM1
  __eth_rx_descriptors_size = __eth_rx_descriptors_end - __eth_rx_descriptors_start;

  .TxDescripSection (NOLOAD) :
  {
    . = ALIGN(32);
    __eth_tx_descriptors_start = .;
    KEEP(*(.TxDescripSection))
    . = ALIGN(32);
    __eth_tx_descriptors_end = .;
  } >RAM_D2_SRAM1
  __eth_tx_descriptors_size = __eth_tx_descriptors_end - __eth_tx_descriptors_start;

  __eth_descriptors_start = __eth_rx_descriptors_start;
  __eth_descriptors_end = __eth_tx_descriptors_end;
  __eth_descriptors_size = __eth_descriptors_end - __eth_descriptors_start;

  ASSERT(__eth_rx_descriptors_start == ORIGIN(RAM_D2_SRAM1), "Ethernet RX descriptors must start at SRAM1 base")
  ASSERT(__eth_rx_descriptors_end <= ORIGIN(RAM_D2_SRAM1) + 0x60, "Ethernet RX descriptors overlap TX descriptors")
  ASSERT(__eth_tx_descriptors_start == ORIGIN(RAM_D2_SRAM1) + 0x60, "Ethernet TX descriptors must start at SRAM1 + 0x60")
  ASSERT(__eth_descriptors_end <= ORIGIN(RAM_D2_SRAM1) + LENGTH(RAM_D2_SRAM1), "Ethernet descriptors exceed SRAM1")

  /* Two independent 256-byte circular UART RX buffers, after Ethernet. */
  .sensor_dma (NOLOAD) :
  {
    . = ALIGN(32);
    __sensor_dma_start = .;
    KEEP(*(.sensor_dma.usart2_rx))
    KEEP(*(.sensor_dma.usart6_rx))
    . = ALIGN(32);
    __sensor_dma_end = .;
  } >RAM_D2_SRAM1
  __sensor_dma_size = __sensor_dma_end - __sensor_dma_start;

  ASSERT(__sensor_dma_start == ORIGIN(RAM_D2_SRAM1) + 0xC0, "Sensor DMA buffers must start at SRAM1 + 0xC0")
  ASSERT(__sensor_dma_size == 0x200, "Sensor DMA buffers must occupy exactly 512 bytes")
  ASSERT(__sensor_dma_end <= ORIGIN(RAM_D2_SRAM1) + LENGTH(RAM_D2_SRAM1), "Sensor DMA buffers exceed SRAM1")

  /* Reset-retained diagnostic records are outside startup .data/.bss handling.
   * Retention through actual power loss depends on the board's backup supply. */
  .fault_retained (NOLOAD) :
  {
    . = ALIGN(8);
    __fault_retained_start = .;
    KEEP(*(.fault_retained))
    . = ALIGN(8);
    __fault_retained_end = .;
  } >BACKUP_SRAM
  __fault_retained_size = __fault_retained_end - __fault_retained_start;
  ASSERT(__fault_retained_start == ORIGIN(BACKUP_SRAM), "Fault retention must start at Backup SRAM base")
  ASSERT(__fault_retained_size > 0, "Fault retention storage must be linked")
  ASSERT(__fault_retained_end <= ORIGIN(BACKUP_SRAM) + LENGTH(BACKUP_SRAM), "Fault retention exceeds Backup SRAM")

  /* Initialized data sections goes into RAM, load LMA copy after code */
  .data :
  {
    . = ALIGN(4);
    _sdata = .;        /* create a global symbol at data start */
    *(.data)           /* .data sections */
    *(.data*)          /* .data* sections */
    *(.RamFunc)        /* .RamFunc sections */
    *(.RamFunc*)       /* .RamFunc* sections */

    . = ALIGN(4);
  } >DTCMRAM AT> FLASH

 /* Initialized TLS data section */
  .tdata : ALIGN(4)
  {
    *(.tdata .tdata.* .gnu.linkonce.td.*)
    . = ALIGN(4);
    _edata = .;        /* define a global symbol at data end */
    PROVIDE(__data_end = .);
    PROVIDE(__tdata_end = .);
  } >DTCMRAM AT> FLASH

  PROVIDE( __tdata_start = ADDR(.tdata) );
  PROVIDE( __tdata_size = __tdata_end - __tdata_start );

  PROVIDE( __data_start = ADDR(.data) );
  PROVIDE( __data_size = __data_end - __data_start );

  PROVIDE( __tdata_source = LOADADDR(.tdata) );
  PROVIDE( __tdata_source_end = LOADADDR(.tdata) + SIZEOF(.tdata) );
  PROVIDE( __tdata_source_size = __tdata_source_end - __tdata_source );

  PROVIDE( __data_source = LOADADDR(.data) );
  PROVIDE( __data_source_end = __tdata_source_end );
  PROVIDE( __data_source_size = __data_source_end - __data_source );
  /* Uninitialized data section */
  .tbss (NOLOAD) : ALIGN(4)
  {
     /* This is used by the startup in order to initialize the .bss secion */
    _sbss = .;         /* define a global symbol at bss start */
    __bss_start__ = _sbss;
    *(.tbss .tbss.*)
    . = ALIGN(4);
    PROVIDE( __tbss_end = . );
  } >DTCMRAM

  PROVIDE( __tbss_start = ADDR(.tbss) );
  PROVIDE( __tbss_size = __tbss_end - __tbss_start );
  PROVIDE( __tbss_offset = ADDR(.tbss) - ADDR(.tdata) );

  PROVIDE( __tls_base = __tdata_start );
  PROVIDE( __tls_end = __tbss_end );
  PROVIDE( __tls_size = __tls_end - __tls_base );
  PROVIDE( __tls_align = MAX(ALIGNOF(.tdata), ALIGNOF(.tbss)) );
  PROVIDE( __tls_size_align = (__tls_size + __tls_align - 1) & ~(__tls_align - 1) );
  PROVIDE( __arm32_tls_tcb_offset = MAX(8, __tls_align) );
  PROVIDE( __arm64_tls_tcb_offset = MAX(16, __tls_align) );

  .bss (NOLOAD) : ALIGN(4)
  {
    *(.bss)
    *(.bss*)
    *(COMMON)

      . = ALIGN(4);
    _ebss = .;         /* define a global symbol at bss end */
    __bss_end__ = _ebss;
      PROVIDE( __bss_end = .);
  } >DTCMRAM
  PROVIDE( __non_tls_bss_start = ADDR(.bss) );

  PROVIDE( __bss_start = __tbss_start );
  PROVIDE( __bss_size = __bss_end - __bss_start );

  /* User_heap_stack section, used to check that there is enough RAM left */
  ._user_heap_stack (NOLOAD) :
  {
    . = ALIGN(8);
    PROVIDE ( end = . );
    PROVIDE ( _end = . );
    . = . + _Min_Heap_Size;
    . = . + _Min_Stack_Size;
    . = ALIGN(8);
  } >DTCMRAM



  /* Remove information from the standard libraries */
  /DISCARD/ :
  {
    libc.a:* ( * )
    libm.a:* ( * )
    libgcc.a:* ( * )
  }

}
