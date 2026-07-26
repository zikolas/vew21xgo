@echo off
rem ------------------------------------------------------------------
rem FMGO.BAT - bring the CF-VEW211/212 up and quieten its FM synth.
rem
rem   FMGO        attenuate the FM by the default 16 TL steps (-12 dB)
rem   FMGO 24     attenuate by 24 steps (-18 dB).  0..63, 0.75 dB each,
rem               0 = leave the level alone (traps but does not scale).
rem
rem The card's FM has no hardware volume control, so FMVOL.DLL traps the
rem OPL ports under JEMM386 and rescales the level of every CARRIER
rem operator on its way to the chip.  Modulators are left alone, so the
rem timbre does not change - only the loudness.
rem
rem Change the level:  JLOAD -u C:\VEW21XGO\FMVOL.DLL   then run FMGO again
rem Remove the trap :  JLOAD -u C:\VEW21XGO\FMVOL.DLL
rem (JEMM386 itself stays loaded; removing that needs a reboot.)
rem
rem Real-mode DOS games only: protected-mode games (DOS/4GW and the
rem like) bypass the V86 trap entirely.
rem ------------------------------------------------------------------
set FMV=%1
if "%FMV%"=="" set FMV=16
echo [1/3] Enabling the sound card...
C:\VEW21XGO\VEW21XGO.COM
echo [2/3] Loading JEMM386 (the V86 host for the port trap)...
rem  D000-D3FF is excluded: that is the enabler's card window.
C:\VEW21XGO\JEMM386.EXE LOAD NOEMS X=D000-D3FF
echo [3/3] Loading the FM volume trap, %FMV% TL steps...
C:\VEW21XGO\JLOAD.EXE C:\VEW21XGO\FMVOL.DLL %FMV%
set FMV=
