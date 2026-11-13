/* SPDX-License-Identifier: GPL-2.0-only
 * Change the stack before entering a normal C frame. Calling pivot_stack()
 * from a C function with live locals invalidates that function's frame.
 *
 * hw_init runs on the initial IRAM stack. Then reserve IRAM
 * 0x40031000..0x4003EFFF (56 KiB) for a downward-growing SYS stack, using the
 * same top as Nyx. This is above SDRAM_PARAMS (ending 0x40030838), above the
 * payload/BSS budget (ending before 0x4002A400), and below the IRQ/FIQ stack
 * region. Keeping the stack in IRAM is safe across DRAM/Minerva transitions.
 * No NAND or storage I/O occurs here.
 */
.section .text.ipl_main
.arm
.align 2
.global ipl_main
.type ipl_main, %function
ipl_main:
    BL guardian_hw_init
    LDR SP, =0x4003F000
    BL guardian_main
1:  B 1b
    .ltorg
.size ipl_main, .-ipl_main
