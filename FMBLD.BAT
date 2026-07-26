@echo off
rem Build FMVOL.DLL - the Jemm loadable module that quietens the FM.
rem Run this in C:\VEW21XGO (JLM.INC must be alongside FMVOL.ASM).
set WATCOM=C:\WATCOM
set INCLUDE=C:\WATCOM\H
set PATH=C:\WATCOM\BINW;C:\DOS
C:\JWASMD -nologo -coff -Fo=FMVOL.obj FMVOL.ASM
if errorlevel 1 goto end
wlink @FMVOL.LNK
if errorlevel 1 goto end
rem  wlink emits a plain PE signature; JLOAD only accepts JLMs marked PX,
rem  so flip the 'E' at e_lfanew+1 (file 0x79 = DEBUG address 0x179).
debug FMVOL.DLL < PXPATCH.SCR > NUL
echo FMVOL.DLL built and marked PX.
:end
