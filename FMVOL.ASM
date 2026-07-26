;--- FMVOL.ASM - an FM volume control for OPL cards that haven't got one.
;--- A Jemm loadable module (JLM) for the Panasonic CF-VEW211 / CF-VEW212.
;---
;--- WHY: on the CF-VEW211 the FM synth (YMF262 -> YAC512 DAC) is summed
;--- into the output amp AFTER the codec, and neither chip has an
;--- attenuator - an exhaustive register hunt found no hardware FM volume
;--- anywhere on the card, and the vendor's own driver is just as loud.
;--- The only level control the OPL programming model offers is each
;--- operator's Total Level, and that belongs to whatever program is
;--- playing: a game rewrites TLs on every patch change, so setting them
;--- beforehand achieves nothing.
;---
;--- So FMVOL becomes the program in the middle. Running at ring 0 under
;--- Jemm, it traps the four OPL ports and rescales every Total Level
;--- write aimed at a CARRIER operator before it reaches the chip.
;--- Modulator writes are passed through untouched: attenuating a
;--- modulator changes the TIMBRE, not the volume. Everything that is not
;--- a Total Level goes straight to the chip, so games sound exactly as
;--- they should - just quieter, by a number you choose.
;---
;--- Which operators are carriers depends on each channel's algorithm, so
;--- FMVOL shadows the connection bits (C0h-C8h), rhythm mode (BDh) and
;--- OPL3 4-operator mode (104h):
;---
;---   2-op : the second operator is always heard; the first only when the
;---          channel is additive (CNT=1).
;---   rhythm: the hi-hat, snare, tom and cymbal are single-operator
;---          sounds, so every one of their operators is heard.
;---   4-op : op4 always; op1 with CNT1, op2 with CNT2, op3 only when both.
;---
;--- LIMITATION: a JLM traps V86 and real-mode guests. Protected-mode
;--- games (DOS/4GW and friends) bypass the trap completely - the same
;--- limitation our CDXMIR module documents.
;---
;--- Usage:  JLOAD FMVOL.DLL [steps]    steps = 0..63, 0.75 dB each
;---                                    (default 16, about -12 dB; 0 traps
;---                                     but leaves the level alone)
;---         JLOAD -u FMVOL.DLL         unload
;--- To change the level, unload and load again with a new value.
;---
;--- Build (on the DOS box):  MIRBLD.BAT  ->  JWASMD + WLINK
;---
;--- (C) 2026 zikolas, MIT. Written for this project; the JLM entry
;--- points and I/O-trap interface follow Jemm's documented JLM.INC, and
;--- the OPL2/OPL3 register model is the public Yamaha programming model.
;--- Patterned on our own CDXMIR module.

    .386
    .model flat, stdcall

    .nolist
    include jlm.inc
    .list

DEVICE_ID   equ 6672h            ; must not clash with CDXMIR's 6671h

DLL_PROCESS_ATTACH  equ 1
DLL_PROCESS_DETACH  equ 0

FMBASE      equ 388h             ; the card's OPL3 is always here
DEFATTEN    equ 16               ; default attenuation in TL steps

    .data

;--- the DDB must be public; the linker exports it so JLoad can find the
;--- device id (needed to unload).

    public ddb

ddb VxD_Desc_Block <0,0,DEVICE_ID,1,0,0,"FMVOL",0,0,0>

ports label word
    dw FMBASE+0, FMBASE+1, FMBASE+2, FMBASE+3
NPORTS equ 4

atten   dd DEFATTEN             ; TL steps added to every carrier

;--- live shadow of the chip, so we know which operators are heard

curreg  db 0,0                  ; register latched by the guest, per bank
curbank db 0                    ; which half of the chip we are serving
savelvl db 0                    ; the level as written, across iscarrier
rhythm  db 0                    ; BDh bit 5
fourop  db 0                    ; 104h bits 0..5
cnt     db 18 dup (0)           ; connection bit, 9 channels x 2 banks
origtl  db 44 dup (0)           ; the level the guest asked for
havetl  db 44 dup (0)           ; 1 once we have seen a level here

;--- operator offset (00h-15h) -> channel, and whether it is that
;--- channel's second operator. 0FFh marks the holes in the OPL map.

opch    db 0,1,2, 0,1,2, 0FFh,0FFh
        db 3,4,5, 3,4,5, 0FFh,0FFh
        db 6,7,8, 6,7,8
opis2   db 0,0,0, 1,1,1, 0,0
        db 0,0,0, 1,1,1, 0,0
        db 0,0,0, 1,1,1

;--- first operator of each channel, for putting levels back

chop1   db 000h,001h,002h, 008h,009h,00Ah, 010h,011h,012h

    .code

;=========================================================================
;--- iscarrier: is operator BL of bank BH heard directly, so that its
;--- level is the volume? Returns AL = 1 if so. Trashes eax, ecx, edx.
;=========================================================================

iscarrier proc uses esi edi ebx

    movzx esi, bl                   ; operator offset
    movzx edi, byte ptr curbank     ; bank
    cmp esi, 16h
    jae ic_no
    movzx ecx, byte ptr opch[esi]   ; channel
    cmp cl, 0FFh
    je  ic_no
    movzx edx, byte ptr opis2[esi]  ; 0 = first operator, 1 = second

;--- rhythm mode, bank 0 only: the four one-operator drums are all heard

    test edi, edi
    jnz ic_norhy
    cmp rhythm, 0
    je  ic_norhy
    cmp cl, 7
    jae ic_yes                      ; hi-hat / snare / tom / cymbal
                                    ; the bass drum (ch6) is an ordinary
                                    ; pair: the 2-op rule below is right
ic_norhy:

;--- part of a 4-operator pair? channels 6..8 never are

    cmp cl, 6
    jae ic_2op
    mov eax, ecx                    ; eax = pair index 0..2
    xor ebx, ebx                    ; ebx = 0 leading channel, 1 tail
    cmp al, 3
    jb  @F
    sub al, 3
    mov ebx, 1
@@:
    push ecx
    mov ecx, eax
    mov ah, 1
    shl ah, cl                      ; bit for this pair
    pop ecx
    test edi, edi
    jz  @F
    shl ah, 3                       ; bank 1 lives in bits 3..5
@@:
    test fourop, ah
    jz  ic_2op

;--- in a 4-op pair: op4 is always heard, op1 with CNT1, op2 with CNT2,
;--- op3 only when both are set.

    and eax, 0FFh                   ; drop the mask AH left in EAX: only
                                    ; the pair index belongs in the sum
    push edx
    mov edx, edi                    ; cnt slot of the leading channel
    imul edx, 9
    add edx, eax
    mov cl, cnt[edx]                ; CNT1
    mov ch, cnt[edx+3]              ; CNT2 (the tail channel)
    pop edx
    test ebx, ebx
    jnz ic_tail
    test edx, edx                   ; leading channel: op1 or op2
    jnz @F
    test cl, cl                     ; op1 is heard when CNT1 = 1
    jnz ic_yes
    jmp ic_no
@@:
    test ch, ch                     ; op2 is heard when CNT2 = 1
    jnz ic_yes
    jmp ic_no
ic_tail:
    test edx, edx
    jnz ic_yes                      ; op4: always heard
    test cl, cl                     ; op3: only when both bits are set
    jz  ic_no
    test ch, ch
    jnz ic_yes
    jmp ic_no

;--- an ordinary 2-operator channel

ic_2op:
    test edx, edx
    jnz ic_yes                      ; the second operator is the carrier
    mov eax, edi
    imul eax, 9
    add eax, ecx
    cmp cnt[eax], 0                 ; the first only when additive
    jne ic_yes
ic_no:
    xor al, al
    ret
ic_yes:
    mov al, 1
    ret
    align 4

iscarrier endp

;=========================================================================
;--- scale: AL = the level byte the guest wrote, BL = operator, BH = bank.
;--- Returns AL with the level raised by [atten] if the operator is heard.
;=========================================================================

scale proc uses ebx ecx edx

    ;--- keep the guest's byte in memory, NOT in a register: iscarrier
    ;--- loads the channel into ECX and would destroy it.
    mov savelvl, al
    cmp atten, 0
    je  sc_out
    call iscarrier
    test al, al
    mov al, savelvl
    jz  sc_out                      ; a modulator: leave the timbre alone
    mov dl, al
    and dl, 0C0h                    ; keep the key-scale-level bits
    and al, 3Fh
    movzx ecx, al
    add ecx, atten
    cmp ecx, 3Fh
    jbe @F
    mov ecx, 3Fh                    ; 63 is silent; never wrap round
@@:
    mov al, cl
    or  al, dl
    ret
sc_out:
    mov al, savelvl
    ret
    align 4

scale endp

;=========================================================================
;--- putlevel: write operator BL of bank BH back to the chip with the
;--- current scaling. Only called for levels we have already seen.
;=========================================================================

putlevel proc uses eax ebx ecx edx

    movzx ecx, byte ptr curbank     ; slot = bank*22 + operator
    imul ecx, 22
    movzx eax, bl
    add ecx, eax
    cmp havetl[ecx], 0
    je  pl_done
    mov al, origtl[ecx]
    call scale                      ; AL = the level to send

    movzx edx, byte ptr curbank     ; the bank's address port
    shl edx, 1
    add edx, FMBASE
    push eax
    movzx eax, bl
    add al, 40h                     ; the Total Level register
    out dx, al
    in  al, 80h                     ; a moment for the chip to latch
    in  al, 80h
    inc edx
    pop eax
    out dx, al
    in  al, 80h
    in  al, 80h
pl_done:
    ret
    align 4

putlevel endp

;=========================================================================
;--- relatch: put the guest's own register selection back, so the extra
;--- writes above are invisible to it.
;=========================================================================

relatch proc uses eax edx

    mov edx, FMBASE
    mov al, curreg[0]
    out dx, al
    in  al, 80h
    mov edx, FMBASE+2
    mov al, curreg[1]
    out dx, al
    in  al, 80h
    ret
    align 4

relatch endp

;=========================================================================
;--- io handler: ecx = type of io, edx = trapped port, eax = data (for
;--- output), ebp = client regs. Input result is returned in eax.
;--- Ring 0, interrupts disabled - so this stays short. Only a change of
;--- connection re-sends anything, and then just that channel's two
;--- operators.
;=========================================================================

iocb proc uses ebx esi edi

    test cl, OUTPUT
    jz  io_in

;--- an OUT. Work out which half of the chip, and whether this is the
;--- address latch or the data port.

    mov ebx, edx
    sub ebx, FMBASE                 ; 0..3
    mov esi, ebx
    shr esi, 1                      ; bank: 0/1 -> 0, 2/3 -> 1
    mov ecx, esi                    ; CL is free now: the OUT test is done,
    mov curbank, cl                 ; and EAX must keep the data byte
    test bl, 1
    jnz io_data

    mov curreg[esi], al             ; address latch: just remember it
    jmp io_raw

io_data:
    movzx edi, curreg[esi]          ; the register being written

    cmp edi, 40h                    ; 40h..55h: Total Level
    jb  io_ncnt
    cmp edi, 55h
    ja  io_ncnt
    push eax
    mov ebx, esi                    ; slot = bank*22 + operator
    imul ebx, 22
    lea ecx, [edi-40h]
    add ebx, ecx
    mov origtl[ebx], al             ; remember what the guest wanted -
                                    ; the WHOLE byte: the KSL bits must
                                    ; survive a putlevel resend too
    mov byte ptr havetl[ebx], 1
    pop eax
    mov ebx, edi
    sub ebx, 40h                    ; BL = operator (bank is in curbank)
    call scale
    jmp io_raw

io_ncnt:
    cmp edi, 0C0h                   ; C0h..C8h: feedback / connection
    jb  io_nbd
    cmp edi, 0C8h
    ja  io_nbd
    push eax
    mov ebx, esi
    imul ebx, 9
    lea ecx, [edi-0C0h]
    add ebx, ecx                    ; cnt slot
    and al, 1
    cmp cnt[ebx], al
    je  io_cnt_same                 ; unchanged: nothing to redo
    mov cnt[ebx], al
    pop eax
    push eax
    out dx, al                      ; send the guest's write first
    ;--- the carriers of this channel may have just moved: put its two
    ;--- levels back with the right scaling, then restore the latch
    mov ebx, ecx
    mov bl, byte ptr chop1[ebx]
    call putlevel
    add bl, 3
    call putlevel
    ;--- half of an active 4-op pair? the algorithm mixes BOTH channels'
    ;--- connection bits, so the partner's carriers just moved as well
    cmp cl, 6
    jae io_cnt_done                 ; channels 6..8 never pair
    mov eax, ecx                    ; AL -> pair index 0..2
    mov ebx, ecx                    ; EBX -> the partner channel
    cmp al, 3
    jb  @F
    sub al, 3
    sub ebx, 3
    jmp io_cnt_bit
@@:
    add ebx, 3
io_cnt_bit:
    push ecx
    mov cl, al
    mov ah, 1
    shl ah, cl                      ; the pair's bit in 104h
    pop ecx
    test esi, esi
    jz  @F
    shl ah, 3                       ; bank 1 pairs live in bits 3..5
@@:
    test fourop, ah
    jz  io_cnt_done
    mov bl, byte ptr chop1[ebx]     ; EBX is 0..8: safe to index with
    call putlevel
    add bl, 3
    call putlevel
io_cnt_done:
    call relatch
    pop eax
    ret
io_cnt_same:
    pop eax
    jmp io_raw

;--- BDh and 104h changes also move carriers, but no resend here on
;--- purpose: programs set the mode at init and write the levels after,
;--- and any stale level heals on the very next TL write anyway.

io_nbd:
    cmp edi, 0BDh                   ; rhythm mode (bank 0 only)
    jne io_n104
    test esi, esi
    jnz io_raw
    push eax
    and al, 20h
    mov rhythm, al
    pop eax
    jmp io_raw

io_n104:
    cmp edi, 04h                    ; 104h: 4-operator enable (bank 1)
    jne io_raw
    test esi, esi
    jz  io_raw
    push eax
    and al, 3Fh
    mov fourop, al
    pop eax

io_raw:
    out dx, al
    ret

io_in:
    in  al, dx
    cmp cl, BYTE_INPUT
    jz  @F
    mov ah, 0FFh                    ; word read: the high byte floats
@@:
    ret
    align 4

iocb endp

;=========================================================================
;--- Parse one decimal token from the JLOAD command line: the attenuation
;--- in TL steps. A token only counts if it is 1-2 pure decimal digits, so
;--- the module name never matches. No printing - JLOAD reports the result.
;=========================================================================

parse_args proc uses esi ebx ecx pcomm:ptr JLCOMM

    mov esi, pcomm
    mov esi, [esi].JLCOMM.lpCmdLine
    test esi, esi
    jz  pa_done
pa_tok:
    xor ebx, ebx                    ; value
    xor ecx, ecx                    ; digit count
pa_char:
    lodsb
    cmp al, 0
    jz  pa_end
    cmp al, 13
    jz  pa_end
    cmp al, 10
    jz  pa_end
    cmp al, ' '
    jz  pa_sep
    cmp al, ','
    jz  pa_sep
    cmp al, 9
    jz  pa_sep
    sub al, '0'
    cmp al, 9
    ja  pa_bad                      ; not a decimal digit
    movzx eax, al
    imul ebx, 10
    add ebx, eax
    inc ecx
    cmp ecx, 3
    jb  pa_char
pa_bad:                             ; not a number: skip the rest of it
    lodsb
    cmp al, 0
    jz  pa_done
    cmp al, 13
    jz  pa_done
    cmp al, 10
    jz  pa_done
    cmp al, ' '
    jz  pa_tok
    cmp al, ','
    jz  pa_tok
    cmp al, 9
    jz  pa_tok
    jmp pa_bad
pa_sep:
    jecxz pa_tok
    jmp pa_set
pa_end:
    jecxz pa_done
pa_set:
    cmp ebx, 3Fh
    jbe @F
    mov ebx, 3Fh
@@:
    mov atten, ebx
pa_done:
    ret
    align 4

parse_args endp

;=========================================================================
;--- install: trap all four ports; on any failure undo and refuse to load
;=========================================================================

install proc uses esi edi pcomm:ptr JLCOMM

    invoke parse_args, pcomm
    xor edi, edi
    .while edi < NPORTS
        movzx edx, ports[edi*2]
        mov esi, offset iocb
        @VMMCall Install_IO_Handler
        .if ( CARRY? )              ; someone else already owns that port
            .while edi
                dec edi
                movzx edx, ports[edi*2]
                @VMMCall Remove_IO_Handler
            .endw
            xor eax, eax
            ret
        .endif
        inc edi
    .endw
    mov eax, 1
    ret
    align 4

install endp

deinstall proc uses edi pcomm:ptr JLCOMM

    xor edi, edi
    .while edi < NPORTS
        movzx edx, ports[edi*2]
        @VMMCall Remove_IO_Handler
        inc edi
    .endw
    mov eax, 1
    ret
    align 4

deinstall endp

DllMain proc stdcall public hModule:dword, dwReason:dword, dwRes:dword

    mov eax, dwReason
    cmp eax, DLL_PROCESS_ATTACH
    jnz @F
    invoke install, dwRes
    jmp exit
@@:
    cmp eax, DLL_PROCESS_DETACH
    jnz @F
    invoke deinstall, dwRes
@@:
exit:
    ret
    align 4

DllMain endp

    end DllMain
