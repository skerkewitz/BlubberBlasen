;===================================================================================
; hdr.asm - ROM header and memory map of the SNES build (ROADMAP.md, stage 3).
; From PVSnesLib's template (snes_rules), with the game's name and a 512 KB ROM.
; Every assembly file of the build includes it (816-tcc writes .include "hdr.asm").
;===================================================================================

; HIROM / FASTROM definitions



.ifdef HIROM                    ;   ==HiRom==

.MEMORYMAP                      ; Begin describing the system architecture.
  SLOTSIZE $10000               ; The slot is $10000 bytes in size. More details on slots later.
  DEFAULTSLOT 0                 ; There's only 1 slot in SNES, there are more in other consoles.
  SLOT 0 $0000                  ; Defines Slot 0's starting address.
  SLOT 1 $0 $2000               ; Used for low RAM allocation
  SLOT 2 $2000 $E000            ; Used for RAM allocation
  SLOT 3 $0 $10000              ; Used for global RAM allocation
  SLOT 4 $6000                  ; Used for SRAM storage.
.ENDME                          ; End MemoryMap definition

.ROMBANKSIZE $10000             ; Every ROM bank is 64 KBytes in size

.else                           ;   ==LoRom==

.MEMORYMAP                      ; Begin describing the system architecture.
  SLOTSIZE $8000                ; The slot is $8000 bytes in size. More details on slots later.
  DEFAULTSLOT 0                 ; There's only 1 slot in SNES, there are more in other consoles.
  SLOT 0 $8000                  ; Defines Slot 0's starting address.
  SLOT 1 $0 $2000               ; Used for low RAM allocation
  SLOT 2 $2000 $E000            ; Used for RAM allocation
  SLOT 3 $0 $10000              ; Used for global RAM allocation and data storage
.ENDME                          ; End MemoryMap definition

.ROMBANKSIZE $8000              ; Every ROM bank is 32 KBytes in size

.endif

.ROMBANKS 16                    ; 16 x 32 KB = 512 KB (4 Mbit)

.SNESHEADER
  ID "SNES"
  NAME "BLUBBER BLASEN       "  ; 21 characters
  SLOWROM
  LOROM
  CARTRIDGETYPE $00
  ROMSIZE $09                   ; 512 KB
  SRAMSIZE $00
  COUNTRY $01
  LICENSEECODE $00
  VERSION $00
.ENDSNES

.SNESNATIVEVECTOR               ; Define Native Mode interrupt vector table
  COP EmptyHandler
  BRK EmptyHandler
  ABORT EmptyHandler
  NMI VBlank
  IRQ EmptyHandler
.ENDNATIVEVECTOR

.SNESEMUVECTOR                  ; Define Emulation Mode interrupt vector table
  COP EmptyHandler
  ABORT EmptyHandler
  NMI EmptyHandler
  RESET tcc__start
  IRQBRK EmptyHandler
.ENDEMUVECTOR


.ifdef FASTROM
.ifdef HIROM
    .BASE $C0
.else
    .BASE $80
.endif
.else
.ifdef HIROM
    .BASE $40
.endif
.endif