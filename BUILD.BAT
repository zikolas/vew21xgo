@echo off
rem Build VEW21XGO.EXE - Open Watcom 16-bit real mode
set WATCOM=C:\WATCOM
set INCLUDE=C:\WATCOM\H
set PATH=C:\WATCOM\BINW;C:\DOS
wcc -ms VEW21XGO.C -fo=VEW21XGO.OBJ
if errorlevel 1 goto end
wlink system dos name VEW21XGO.EXE file VEW21XGO.OBJ
:end
