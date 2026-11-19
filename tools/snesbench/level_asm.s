;;; Hand-written 65816 versions of level_solid_px and level_solid_tile
;;; (src/game/level.c), for the C-versus-assembly experiment of the compiler
;;; benchmark. Same behaviour, Calypsi's calling convention as its own output
;;; shows it: first argument in A, second in the pseudo-register _Dp, result
;;; in A; A, X, Y and _Dp.._Dp+5 may be changed; 16-bit A and X/Y on entry
;;; and exit. `cells` is level.c's pointer to the room's cells (far, 4 bytes),
;;; made public for this experiment.

              .rtmodel version,"1"
              .rtmodel codeModel,"large"
              .rtmodel dataModel,"large"
              .rtmodel core,"65816"
              .rtmodel huge,"0"
              .rtmodel target,"snes"
              .rtmodel ppu,"1"
              .extern _Dp
              .extern cells

;;; uint8_t level_solid_px(int16_t sx, int16_t sy)
;;;   sy < FIELD_Y (16)              -> 0
;;;   else level_solid_tile(sx >> 3, (sy - 16) >> 3), inlined
              .section farcode,text
              .public level_solid_px
level_solid_px:
              tax                         ; X = sx
              lda     dp:.tiny _Dp
              sec
              sbc     ##16                ; sy - FIELD_Y, signed
              bvc     px_nov
              eor     ##0x8000
px_nov:       bmi     ret0
              lsr     a                   ; >= 0 here: a plain shift
              lsr     a
              lsr     a
              sta     dp:.tiny _Dp        ; ty
              txa
              cmp     ##0x8000            ; sx >> 3, arithmetic: carry = sign
              ror     a
              cmp     ##0x8000
              ror     a
              cmp     ##0x8000
              ror     a
              ; fall into the tile check with A = tx, _Dp = ty

;;; uint8_t level_solid_tile(int16_t tx, int16_t ty)
;;;   tx < 0 || tx >= 32 -> 1;  ty < 0 || ty >= 26 -> 0;
;;;   else cells[ty * 32 + tx] has CELL_SOLID (bit 7)
              .public level_solid_tile
level_solid_tile:
              cmp     ##32                ; unsigned: negative counts as >= 32
              bcs     ret1
              tax                         ; X = tx
              lda     dp:.tiny _Dp
              cmp     ##26
              bcs     ret0
              asl     a                   ; ty * 32 (carry stays clear: ty < 26)
              asl     a
              asl     a
              asl     a
              asl     a
              stx     dp:.tiny _Dp
              adc     dp:.tiny _Dp
              tay                         ; Y = index
              lda     long:cells
              sta     dp:.tiny (_Dp+2)
              lda     long:cells+2
              sta     dp:.tiny (_Dp+4)
              sep     #0x20
              lda     [.tiny (_Dp+2)],y
              rep     #0x20               ; leaves N as the load set it
              bmi     ret1
ret0:         lda     ##0
              rtl
ret1:         lda     ##1
              rtl
