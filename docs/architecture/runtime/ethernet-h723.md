# Ethernet on the STM32H723 (NUCLEO-H723ZG): memory, MPU, cache, PHY, MAC

Supports `docs/architecture/runtime-decision.md`. Status: requirements and
known facts only. No Ethernet code exists on this branch and none ran on a
board. Values not settled by a source are named parameters marked
`TODO(owner)`.

## 1. Where the Ethernet DMA can read and write

The Ethernet MAC DMA is a bus master in the D2 domain. ST states that D2
masters can reach almost all memories except ITCM and DTCM (ST knowledge
article "DMA is not working on STM32H7 devices"). On this board's linker
script all data, heap and stack sit in DTCM (see
`baseline-memory-map.md`), so anything the Ethernet DMA touches must be
placed by name in another region.

| Item | Must be in | Baseline today | Sizing |
|---|---|---|---|
| Rx DMA descriptors | RAM_D2 (0x30000000) | yes, `.RxDescripSection`, 4 x 24 bytes, 32-byte aligned | `ETH_RX_DESC_CNT` 4 in `stm32h7xx_hal_conf.h` |
| Tx DMA descriptors | RAM_D2 | yes, `.TxDescripSection`, 4 x 24 bytes | `ETH_TX_DESC_CNT` 4 |
| Rx data buffers | RAM_D2 or RAM_D1 (AXI SRAM, 0x24000000) | not allocated | `ETH_RX_BUFFER_COUNT` x 1536 (`RxBuffLen`) `TODO(owner)` |
| Tx data (LwIP pbufs from `ram_heap` and the pbuf pool) | RAM_D2 or RAM_D1, or copied into a Tx buffer there | not present | `MEM_SIZE`, `PBUF_POOL_SIZE` `TODO(owner)` |

RAM_D2 is 32,768 bytes on the H723. With 192 bytes of descriptors, about 32
KiB is left: room for, for example, 12 Rx buffers of 1536 bytes (18,432) plus
an 8 KiB LwIP heap, or fewer Rx buffers and a larger heap. RAM_D1 (320 KiB,
unused) takes what does not fit. ST's H7 Ethernet article
("How to create a project for STM32H7 with Ethernet and LwIP stack") does the
same on parts with small D2 RAM: descriptors and LwIP heap in D2, Rx buffers
in AXI SRAM. Its example addresses (0x30040000) are for H74x/H75x parts and do
not apply to the H723, whose RAM_D2 is 0x30000000 to 0x30007FFF.

## 2. MPU and D-cache rules

The baseline has the caches off and one MPU background region that forbids
access to unmapped address space. Before the D-cache is turned on, the
following must hold (ST article above; ARMv7-M MPU semantics):

1. One MPU region covers every byte the Ethernet DMA reads or writes:
   descriptors, Rx buffers and the Tx source memory. Simplest form: one
   region over all of RAM_D2 (32 KiB, base 0x30000000, size field 32KB),
   memory type Normal, not cacheable (TEX 1, C 0, B 0), or Device/shareable
   for the descriptors only. If the Rx buffers are in RAM_D1, a second region
   over exactly that buffer block with the same attributes. Region size is a
   power of two and the base is aligned to it.
2. The region number is higher than the background region 0 so that it wins.
3. The descriptors must never be cacheable: the DMA owns the OWN bit, and a
   cached copy makes the CPU and the DMA disagree.
4. If any DMA buffer stays cacheable for speed, every Tx needs a cache clean
   (`SCB_CleanDCache_by_Addr`) before handing it to the DMA, every Rx needs an
   invalidate before reading, and buffers must be 32-byte aligned and a
   multiple of 32 bytes so that a clean or invalidate never touches a
   neighbour. The non-cacheable region avoids all of that and is the
   recommended first step.
5. Nothing else may live in the non-cacheable region by accident: the linker
   script places only named sections there.
6. `MPU_Config()` runs before `SCB_EnableDCache()`; the cache is enabled
   only after the regions exist.

Parameters: `ETH_MPU_REGION_NUMBER`, `ETH_MPU_REGION_BASE`,
`ETH_MPU_REGION_SIZE`, `ETH_DMA_MEMORY_ATTRIBUTES`, all `TODO(owner)`. The
`.ioc` is the owner's file; these go there or into the board port, not into
generated code by hand.

## 3. LAN8742A link handling

The NUCLEO-H723ZG has an on-board LAN8742A on RMII (PA1 REF_CLK, PA2 MDIO, PA7
CRS_DV, PC1 MDC, PC4 RXD0, PC5 RXD1, PB13 TXD1, PG11 TX_EN, PG13 TXD0, all in
the `.ioc`). What the port must do, whatever the runtime:

1. Probe the PHY over MDIO at start-up. The PHY address is a board fact
   (`ETH_PHY_ADDRESS`, `TODO(owner)`: read it from the NUCLEO-H723ZG user
   manual or probe it).
2. Read the link state (basic status register link bit) periodically, not
   only at start-up. The MAC speed and duplex must be set from the
   auto-negotiation result each time the link comes up
   (`HAL_ETH_SetMACConfig` then `HAL_ETH_Start`).
3. On link loss: stop the MAC and DMA or leave them running with no traffic,
   mark the netif link down (`netif_set_link_down`), and report the link
   state in diagnostics. The micro-ROS session will time out on its own (ping
   failure); the link state is the earlier and clearer signal.
4. On link up: restart the MAC with the new speed and duplex, mark the netif
   link up, and let the micro-ROS reconnect state machine create a new
   session.
5. Link polling period: `ETH_LINK_POLL_MS`, `TODO(owner)`. It is a
   diagnostic; it does not stop anything.

ST's `lan8742` component driver (in STM32CubeH7 under
`Drivers/BSP/Components/lan8742`) gives `LAN8742_Init` and
`LAN8742_GetLinkState`; it is not in this repository yet. CubeMX adds it when
LwIP is enabled in the `.ioc`.

## 4. MAC address from the unique device ID

The baseline uses the fixed MAC 00:80:E1:00:00:00 (ST OUI, last three bytes
zero) for every board. Two boards on one network would collide. Proposal:

- Keep the ST OUI 00:80:E1 for bytes 0 to 2, or use a locally administered
  address (byte 0 = 0x02). Which one is an owner choice:
  `ETH_MAC_PREFIX`, `TODO(owner)`.
- Derive bytes 3 to 5 from the 96-bit unique device ID at `UID_BASE`
  (0x1FF1E800, from `stm32h723xx.h`), for example a fixed hash of the three
  32-bit words folded to 24 bits. The fold function is plain C and
  host-testable; reading `UID_BASE` is board port.
- Publish the MAC in a diagnostic message so a collision can be spotted.

A 24-bit fold can collide between two boards; the probability is low for a
small fleet but not zero, so the bench procedure should record each board's
MAC.

## 5. The open hard-fault report for micro-ROS over UDP on this board

What is known (from public sources):

- A public ST community thread ("Integrating micro-ROS over UDP using LwIP and
  FreeRTOS on NUCLEO-H723ZG") reports hard faults on exactly this board with
  micro-ROS, UDP, LwIP and FreeRTOS. The poster tied them to MPU regions that
  overlapped the Ethernet DMA descriptors and the LwIP heap in D2 SRAM at
  0x30000000. The poster later reported that basic UDP worked and that MPU
  setup and task stack sizing were the key factors. No confirmed fix with
  configuration was posted.
- The micro-ROS STM32CubeMX utilities README (jazzy) states that STM32H7
  boards need the MPU set up correctly for UDP, and that with FreeRTOS the
  micro-ROS task needs more than 10 kB of stack.

What is not known:

- Which MPU region layout the poster used and which one fixed it.
- Whether the fault was a bus fault promoted to hard fault (DMA access to
  DTCM, or a CPU access to an MPU no-access area), a stack overflow of the
  micro-ROS task, or a cache coherency failure that corrupted a descriptor.
- Whether it happens without FreeRTOS. No report was found either way.
- Whether it is specific to the CubeMX LwIP template of the time.

How to close it on the bench, in order, each step a pass or fail record:

1. Fault handler that saves the stacked PC, LR, xPSR and the CFSR, HFSR,
   MMFAR and BFAR registers to a no-init RAM area and reports them after the
   reset. Without that, a hard fault is just a hang.
2. Ethernet with descriptors and buffers placed as in section 1, MPU region as
   in section 2, D-cache off. LwIP ICMP ping from the host: 10 minutes, no
   fault, no lost replies beyond the link.
3. Same with the D-cache on.
4. LwIP UDP echo on the agent port with 512-byte datagrams at the expected
   micro-ROS rate, 30 minutes.
5. micro-ROS over UDP with the probe entity count, 30 minutes, then pull the
   cable and plug it back 10 times; the session must come back each time.

Each step isolates one cause: step 2 the placement and MPU, step 3 the cache,
step 4 LwIP buffer handling, step 5 micro-ROS stack depth and reconnection.
