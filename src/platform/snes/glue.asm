;===================================================================================
; glue.asm - what the game needs on the SNES that PVSnesLib does not have
; (the extensions of src/snes/snes.h), and clearing RAM at start.
;
; 816-tcc calls with jsr.l and the arguments on the stack, first argument
; lowest, each at its size (u8: 1 byte, u16: 2, pointer: address word then
; bank word). After php, the first argument is at 5,s. A/X/Y may be changed;
; the processor flags are restored with plp.
;===================================================================================

.include "hdr.asm"

.SECTION ".bb_glue" SUPERFREE

.ACCU 16
.INDEX 16

;-----------------------------------------------------------------------------------
; void snes_clear_ram(void)
; Zeroes work RAM $7E:2000-$DFFF, where 816-tcc puts the variables without an
; initial value. PVSnesLib's start-up code only clears one file's share of it
; (each file has its own .bss section with the same section labels), but C
; promises zeroes for all of them. Call it first thing in main.
snes_clear_ram:
    php
    rep #$20
    lda.w #$2000
    sta.l $002181            ; WMADD low and middle: $2000
    sep #$20
    lda #$00
    sta.l $002183            ; WMADD high: bank $7E
    lda #$08
    sta.l $004300            ; DMA 0: fixed source, CPU to PPU, one register
    lda #$80
    sta.l $004301            ; to $2180, the work RAM data port
    rep #$20
    lda.w #bb_zero
    sta.l $004302
    lda.w #$C000             ; 48 KB: $2000..$DFFF
    sta.l $004305
    sep #$20
    lda #:bb_zero
    sta.l $004304
    lda #$01
    sta.l $00420B            ; start DMA channel 0
    plp
    rtl

bb_zero:
    .db $00

;-----------------------------------------------------------------------------------
; void snesRegWrite(u8 reg, u8 value): writes $2100 + reg.
snesRegWrite:
    php
    rep #$30
    lda 5,s
    and.w #$00FF
    tax
    sep #$20
    lda 6,s
    sta.l $002100,x
    plp
    rtl

;-----------------------------------------------------------------------------------
; void snesHdmaSet(u8 channel, u8 reg, u8 twoRegs, const u8 *table)
; HDMA in direct mode: channel writes table data to $2100 + reg (and the
; next register when twoRegs) on every line, from the next frame on.
snesHdmaSet:
    php
    rep #$30
    lda 5,s
    and.w #$0007
    asl a
    asl a
    asl a
    asl a
    tax                      ; X = channel * 16: $43n0
    sep #$20
    lda 7,s
    beq +
    lda #$01                 ; mode 1: two registers
+   sta.l $004300,x          ; DMAPn: direct, CPU to PPU
    lda 6,s
    sta.l $004301,x          ; BBADn: the register
    rep #$20
    lda 8,s
    sta.l $004302,x          ; A1Tn: the table's address
    sep #$20
    lda 10,s
    sta.l $004304,x          ; A1Bn: its bank
    plp
    rtl

;-----------------------------------------------------------------------------------
; u8 snesDmaFits(u16 bytes): 1 if a copy of that many bytes to VRAM / CGRAM
; still ends inside this vertical blank. Reads the vertical line counter:
; the vertical blank is lines 225..261, and a line moves about 170 bytes by
; DMA; this counts 128 a line and 3 lines for the code around the copy.
; Outside the vertical blank: 0. (The caller skips it with the screen off.)
snesDmaFits:
    php
    sep #$20
    lda.l $00213F            ; resets the low/high order of the counter reads
    lda.l $002137            ; latch the H/V counters
    lda.l $00213D            ; vertical counter, low byte
    xba
    lda.l $00213D            ; and its high bit
    and #$01
    xba
    rep #$30
    cmp.w #225
    bcc _bb_dmafits_no
    eor.w #$FFFF
    clc
    adc.w #263               ; 262 - V: lines left
    tax
    lda 5,s                  ; bytes
    lsr a
    lsr a
    lsr a
    lsr a
    lsr a
    lsr a
    lsr a
    clc
    adc.w #3                 ; lines needed
    sta.b tcc__r0
    txa
    cmp.b tcc__r0
    bcc _bb_dmafits_no
    lda.w #1
    sta.b tcc__r0
    plp
    rtl
_bb_dmafits_no:
    stz.b tcc__r0
    plp
    rtl

;-----------------------------------------------------------------------------------
; void snesHdmaEnable(u8 channels): HDMAEN.
snesHdmaEnable:
    php
    sep #$20
    lda 5,s
    sta.l $00420C
    plp
    rtl

;-----------------------------------------------------------------------------------
; void setPaletteColor(u16 paletteEntry, u16 paletteColor)
; A macro in PVSnesLib; the game calls it as a function.
setPaletteColor:
    php
    sep #$20
    lda 5,s
    sta.l $002121            ; CGADD
    lda 7,s
    sta.l $002122            ; CGDATA, low byte then high byte
    lda 8,s
    sta.l $002122
    plp
    rtl

.ENDS
