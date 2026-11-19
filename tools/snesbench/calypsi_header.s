;;; SNES cartridge header for the Calypsi benchmark ROM (HiROM, SlowROM).
              .section snesheader, root
              .ascii  "BLUBBER BENCH CALYPSI"   ; 21 bytes
              .byte   0x21                      ; HiROM, SlowROM
              .byte   0x00                      ; ROM only
              .byte   0x09                      ; 512 KB
              .byte   0x00                      ; no SRAM
              .byte   0x01                      ; country
              .byte   0x00                      ; developer
              .byte   0x00                      ; version
              .word   0xFFFF, 0x0000            ; checksum complement, checksum
