;--- VEWSPY.ASM - a ring-0 I/O tracer for the CF-VEW212 bring-up.
;--- A Jemm loadable module (JLM), patterned on our own FMVOL.
;---
;--- WHY: the 212's analog output has two stages. Stage 1 (a small pop)
;--- fires by itself when the socket leaves reset - we get that for free.
;--- Stage 2, the one that actually connects the synth to the jack (a
;--- much bigger pop), happens only while the period vendor enabler
;--- SCPRODOS.COM runs, and it leaves NO trace in any register we can
;--- read: our replicated configuration is byte-identical to the vendor's
;--- captured state - same COR, same CCSR, same PCIC windows, same MPU
;--- and OPL4 register reads - and yet ours is silent.  So the switch is
;--- a WRITE we have never made, not a value we have failed to hold.
;---
;--- VEWSPY logs every I/O access to the card's whole footprint while the
;--- vendor enabler runs: MPU 330-333, OPL4 FM+wave 388-38F, and the PCIC
;--- itself at 3E0/3E1 (which also captures the exact power/window
;--- programming order).  Read the log back afterwards and the vendor's
;--- enable sequence is simply there, byte for byte.  Pure observation -
;--- the module never changes what it sees, it passes every access
;--- through unmodified.
;---
;--- NOTE: attribute-MEMORY writes (the COR, for instance) are memory
;--- cycles, not I/O, so they are NOT captured here.  We already know the
;--- vendor's attribute state from doc/DUMP212-VND-IDLE.TXT, and the
;--- passive sweep proved nothing else in that space even decodes.
;---
;--- Usage:  JLOAD VEWSPY.DLL          load (logging starts DISARMED)
;---         VEWSPYD /ARM              arm, then run SCPRODOS.COM
;---         VEWSPYD /OFF              disarm
;---         VEWSPYD                   dump the capture
;---         JLOAD -u VEWSPY.DLL       unload
;---
;--- The dump path is a magic port pair (2A2h control, 2A3h data) that
;--- this module traps like any other: writing 2A2h arms/disarms/rewinds,
;--- and reading 2A3h streams the log out a byte at a time - four bytes
;--- of entry count first, then four bytes per entry:
;---     +0 port low   +1 port high   +2 data   +3 flags
;--- flags: bit0 = 1 an OUT (0 an IN), bit1 = 1 a 16-bit access.
;---
;--- Build:  ./spybuild.sh   (host-side JWasm + wlink, as for FMVOL)
;---
;--- (C) 2026 zikolas, GPL v2.  JLM entry points and the I/O-trap interface
;--- follow Jemm's documented JLM.INC.

    .386
    .model flat, stdcall

    .nolist
    include jlm.inc
    .list

DEVICE_ID   equ 6673h            ; must not clash with CDXMIR 6671h / FMVOL 6672h

DLL_PROCESS_ATTACH  equ 1
DLL_PROCESS_DETACH  equ 0

MAGCTL      equ 2A2h             ; control port (unused on the PC110)
MAGDAT      equ 2A3h             ; data port: streams the log out
LOGMAX      equ 600              ; entries; 4 bytes each.  Deliberately
                                 ; small: if the guest wedges, the whole
                                 ; log can be drained through the magic
                                 ; port over the serial link, one byte at
                                 ; a time, without a working DOS prompt.
PCICIDX     equ 3E0h
PCICDAT     equ 3E1h

    .data

    public ddb

ddb VxD_Desc_Block <0,0,DEVICE_ID,1,0,0,"VEWSPY",0,0,0>

;--- the card's whole I/O footprint, plus the controller and our own
;--- readout pair.  Trapping 3E0/3E1 is what captures the vendor's power
;--- and window programming (and its ORDER, which a state dump loses).
;---
;--- THE PCIC (3E0/3E1) IS NO LONGER TRAPPED.  Every attempt that
;--- included it either wedged the box or made the vendor enabler fail to
;--- find the card, while a clean-bench self-test with the same module
;--- passed - intercepting the controller evidently disturbs the polling
;--- that Card Services and the enabler do through it.  We already have
;--- the controller picture from doc/ - what is missing is the card side.
;---
;--- 530-539 IS THE IMPORTANT ONE, and the first version of this tool
;--- missed it: the vendor enabler programs a window onto that block and
;--- then re-maps the window away, so every write it makes there was
;--- invisible, and the trace merely proved the enabler writes nothing to
;--- the ports we happened to watch.  Under config index 10h the card
;--- aliases an eight-register bank (04 80 05 00 02 FF FF FF) across any
;--- window - the MEI ASIC's own control block, and the last place the
;--- analog switch can be hiding.

ports label word
    dw 530h, 531h, 532h, 533h, 534h, 535h, 536h, 537h, 538h, 539h
    dw 330h, 331h, 332h, 333h
    dw 388h, 389h, 38Ah, 38Bh, 38Ch, 38Dh, 38Eh, 38Fh
    dw MAGCTL, MAGDAT
NPORTS equ 24

logbuf  db LOGMAX*4 dup (0)
lastidx db 0                     ; last index written to 3E0h
logcnt  dd 0                     ; entries captured
logrd   dd 0                     ; read cursor for the dump protocol
logon   db 0                     ; armed?

    .code

;=========================================================================
;--- logit: append one access.  edx = port, al = data, bl = flags.
;--- Stops at LOGMAX rather than wrapping: we want the FIRST events after
;--- arming, which is precisely the enable sequence.
;=========================================================================

logit proc uses edi

    cmp logon, 0
    je  lg_done
    mov edi, logcnt
    cmp edi, LOGMAX
    jae lg_done
    shl edi, 2
    mov byte ptr logbuf[edi], dl
    mov byte ptr logbuf[edi+1], dh
    mov byte ptr logbuf[edi+2], al
    mov byte ptr logbuf[edi+3], bl
    inc logcnt
lg_done:
    ret
    align 4

logit endp

;=========================================================================
;--- io handler: ecx = type of io, edx = trapped port, eax = data (for
;--- output), ebp = client regs.  Input result returned in eax.
;--- Ring 0, interrupts disabled - keep it short.
;=========================================================================

iocb proc uses ebx esi edi

    cmp edx, MAGCTL
    je  mag_ctl
    cmp edx, MAGDAT
    je  mag_dat

;--- The PCIC is a torrent of Card-Services polling: every poll is an
;--- index write plus a data read, and logging all of it buries the one
;--- thing we are hunting.  So the controller is filtered down to what
;--- actually changes hardware - writes to the data port - recorded
;--- against the register they hit (synthetic port 1000h+reg).  Index
;--- writes only update our shadow; PCIC reads are dropped.

    cmp edx, PCICIDX
    jne sp_npidx
    test cl, OUTPUT
    jz  sp_pass                     ; a read of the index port: pass through
    mov lastidx, al
    out dx, al
    ret
sp_npidx:
    cmp edx, PCICDAT
    jne sp_card
    test cl, OUTPUT
    jz  sp_pass                     ; poller read: pass, do not log
    push eax
    push ecx
    push edx
    movzx edx, byte ptr lastidx     ; log as register, not as port 3E1h
    or  edx, 1000h
    mov bl, 1
    call logit
    pop edx
    pop ecx
    pop eax
    out dx, al
    ret
sp_pass:
    in  al, dx
    ret

sp_card:
    test cl, OUTPUT
    jz  spy_in

;--- an OUT: record it, then let it through untouched

    mov bl, 1                       ; flags: OUT
    test cl, WORD_IO
    jz  @F
    or  bl, 2                       ; 16-bit access
@@:
    push eax
    push ecx
    call logit
    pop ecx
    pop eax
    test cl, WORD_IO
    jz  @F
    out dx, ax
    ret
@@:
    out dx, al
    ret

spy_in:
    test cl, WORD_IO
    jz  @F
    in  ax, dx
    mov bl, 2
    jmp spy_inlog
@@:
    in  al, dx
    xor bl, bl
spy_inlog:
    push eax
    call logit
    pop eax
    ret

;--- control port: 0 rewind, 1 clear, 2 arm, 3 disarm; reads give status

mag_ctl:
    test cl, OUTPUT
    jz  mc_in
    cmp al, 0
    jne mc_c1
    mov logrd, 0
    ret
mc_c1:
    cmp al, 1
    jne mc_c2
    mov logcnt, 0
    mov logrd, 0
    ret
mc_c2:
    cmp al, 2
    jne mc_c3
    mov logon, 1
    ret
mc_c3:
    cmp al, 3
    jne mc_cdone
    mov logon, 0
mc_cdone:
    ret
mc_in:
    movzx eax, byte ptr logon
    ret

;--- data port: four bytes of count, then the entries

mag_dat:
    test cl, OUTPUT
    jnz md_out
    mov edi, logrd
    inc logrd
    cmp edi, 4
    jae md_entry
    mov eax, logcnt                 ; byte edi of the 32-bit count
    mov ecx, edi
    shl ecx, 3
    shr eax, cl
    and eax, 0FFh
    ret
md_entry:
    sub edi, 4
    mov eax, logcnt
    shl eax, 2
    cmp edi, eax
    jae md_zero
    movzx eax, byte ptr logbuf[edi]
    ret
md_zero:
    xor eax, eax
    ret
md_out:
    ret
    align 4

iocb endp

;=========================================================================
;--- install: trap the lot; on any failure undo and refuse to load
;=========================================================================

install proc uses esi edi pcomm:ptr JLCOMM

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

end
