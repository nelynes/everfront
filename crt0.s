@ Minimal multiboot crt0 for EverFront (from the launch test).
@ The whole program lives in EWRAM (0x02000000). When the EverDrive's menu
@ starts us from cart space (0x08000000) we copy ourselves to EWRAM first,
@ because loading a game into cart PSRAM overwrites cart space.

    .section .crt0, "ax"
    .arm
    .global _start
_start:
    b       start                   @ 0x00 cart entry
    .fill   156, 1, 0               @ 0x04 logo (filled by fix_header.py)
    .fill   32, 1, 0                @ 0xA0 title/code/maker/checksum (filled)
    b       start                   @ 0xC0 multiboot entry
    .byte   0                       @ 0xC4 boot method
    .byte   0                       @ 0xC5 slave number
    .fill   26, 1, 0                @ 0xC6..0xDF
    b       start                   @ 0xE0 joybus entry

start:
    mov     r0, #0x04000000
    str     r0, [r0, #0x208]        @ IME = 0
    mov     r0, #0x12               @ IRQ mode
    msr     cpsr_fc, r0
    ldr     sp, =0x03007FA0
    mov     r0, #0x1F               @ System mode
    msr     cpsr_fc, r0
    ldr     sp, =0x03007F00

    adr     r1, start
    and     r1, r1, #0xFF000000
    cmp     r1, #0x08000000
    bne     in_ewram

    @ running from cart: copy image to EWRAM
    mov     r0, #0x08000000
    mov     r1, #0x02000000
    ldr     r2, =__image_end
    sub     r2, r2, r1
    add     r2, r2, #3
    bic     r2, r2, #3
copy:
    ldr     r3, [r0], #4
    str     r3, [r1], #4
    subs    r2, r2, #4
    bne     copy
    ldr     r0, =in_ewram
    bx      r0

in_ewram:
    ldr     r0, =__bss_start
    ldr     r1, =__bss_end
    mov     r2, #0
clear:
    cmp     r0, r1
    strlo   r2, [r0], #4
    blo     clear

    ldr     r0, =__iwram_lma
    ldr     r1, =__iwram_start
    ldr     r2, =__iwram_end
icopy:
    cmp     r1, r2
    ldrlo   r3, [r0], #4
    strlo   r3, [r1], #4
    blo     icopy

    ldr     r0, =main
    mov     lr, pc
    bx      r0
hang:
    b       hang

@ void boot_game(void): reset RAM/IO (keeping EWRAM, where we run) and
@ soft-reset into cart space. Call only after the cart is in game mode.
    .global boot_game
    .type   boot_game, %function
boot_game:
    mov     r0, #0x04000000
    str     r0, [r0, #0x208]        @ IME = 0
    mov     r0, #0xFE               @ clear all but EWRAM
    swi     0x010000                @ RegisterRamReset
    mov     r0, #0x03000000
    add     r0, r0, #0x7F00
    add     r0, r0, #0xFA           @ 0x03007FFA: 0 = reset to cart
    mov     r1, #0
    strb    r1, [r0]
    swi     0x000000                @ SoftReset
    b       .

@ void boot_manual(u32 irq_vector): reset the GBA the way the stock menu
@ does before a game, keeping 0x03007FFC (set to irq_vector, which the
@ in-game menu needs), then run a small stub from VRAM that clears EWRAM
@ (where we live) and jumps to the game. Uses no stack.
    .global boot_manual
    .type   boot_manual, %function
boot_manual:
    mov     r12, r0
    mov     r0, #0x04000000
    mov     r1, #0
    str     r1, [r0, #0x208]        @ IME = 0

    add     r2, r0, #0x02           @ 0x002..0x057: display regs
    add     r3, r0, #0x58
    bl      zero16
    mov     r1, #0x80
    strh    r1, [r0]                @ DISPCNT: forced blank
    mov     r1, #0x100
    strh    r1, [r0, #0x20]         @ BG2PA
    strh    r1, [r0, #0x26]         @ BG2PD
    strh    r1, [r0, #0x30]         @ BG3PA
    strh    r1, [r0, #0x36]         @ BG3PD
    mov     r1, #0
    add     r2, r0, #0x60           @ sound
    add     r3, r0, #0x88
    bl      zero16
    add     r2, r0, #0x8A
    add     r3, r0, #0xA0
    bl      zero16
    mov     r1, #0x0E
    strh    r1, [r0, #0x82]         @ SOUNDCNT_H
    mov     r1, #0x200
    strh    r1, [r0, #0x88]         @ SOUNDBIAS
    mov     r1, #0
    add     r2, r0, #0xB0           @ DMA
    add     r3, r0, #0xE0
    bl      zero16
    add     r2, r0, #0x100          @ timers
    add     r3, r0, #0x110
    bl      zero16
    add     r2, r0, #0x128          @ SIOCNT, SIODATA8
    add     r3, r0, #0x12C
    bl      zero16
    add     r2, r0, #0x130
    strh    r1, [r2, #2]            @ KEYCNT
    mov     r1, #0x8000
    strh    r1, [r2, #4]            @ RCNT
    mov     r1, #0
    ldr     r2, =0x04000136
    ldr     r3, =0x0400015A
    bl      zero16
    add     r2, r0, #0x200
    strh    r1, [r2]                @ IE
    ldr     r1, =0xFFFF
    strh    r1, [r2, #2]            @ IF (ack all)
    mov     r1, #0
    strh    r1, [r2, #4]            @ WAITCNT
    add     r2, r0, #0x300
    mov     r1, #1
    strb    r1, [r2]                @ POSTFLG
    mov     r1, #0

    mov     r2, #0x03000000         @ IWRAM up to the IRQ vector
    ldr     r3, =0x03007FFC
    bl      zero32
    str     r12, [r3]               @ IRQ vector
    mov     r2, #0x05000000         @ palette
    add     r3, r2, #0x400
    bl      zero32
    mov     r2, #0x06000000         @ VRAM
    add     r3, r2, #0x18000
    bl      zero32
    mov     r2, #0x07000000         @ OAM
    add     r3, r2, #0x400
    bl      zero32

    adr     r2, stub                @ stub -> top of OBJ VRAM
    adr     r3, stub_end
    ldr     r4, =0x06017F00
copy_stub:
    ldr     r5, [r2], #4
    str     r5, [r4], #4
    cmp     r2, r3
    bne     copy_stub

    mov     r0, #0xD3               @ SVC
    msr     cpsr_fc, r0
    ldr     sp, =0x03007FE0
    mov     lr, #0
    msr     spsr_fc, r1
    mov     r0, #0xD2               @ IRQ
    msr     cpsr_fc, r0
    ldr     sp, =0x03007FA0
    mov     lr, #0
    msr     spsr_fc, r1
    mov     r0, #0x1F               @ System
    msr     cpsr_fc, r0
    ldr     sp, =0x03007F00
    ldr     r0, =0x06017F00
    bx      r0

@ zero halfwords [r2, r3) with r1 = 0; returns via lr
zero16:
    strh    r1, [r2], #2
    cmp     r2, r3
    blo     zero16
    bx      lr
@ zero words [r2, r3) with r1 = 0
zero32:
    str     r1, [r2], #4
    cmp     r2, r3
    blo     zero32
    bx      lr

@ Runs from VRAM: clear EWRAM, clear registers, enter the game.
    .balign 4
stub:
    mov     r0, #0x02000000
    mov     r1, #0x02040000
    mov     r2, #0
stub_clear:
    str     r2, [r0], #4
    cmp     r0, r1
    bne     stub_clear
    mov     r0, #0
    mov     r1, #0
    mov     r3, #0
    mov     r4, #0
    mov     r5, #0
    mov     r6, #0
    mov     r7, #0
    mov     r8, #0
    mov     r9, #0
    mov     r10, #0
    mov     r11, #0
    mov     r12, #0
    mov     lr, #0x08000000
    bx      lr
    .balign 4
stub_end:

    .pool
