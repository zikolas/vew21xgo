@echo off
rem Build a Watcom tool: BUILD [name]   (default VEWCIS, e.g. BUILD VEWCIS)
rem (VEW21XGO itself is built from VEW21XGO.ASM with NASM - see build.sh.
rem  The legacy 1.x C enabler lives in legacy\VEW21XGO.C.)
set WATCOM=C:\WATCOM
set INCLUDE=C:\WATCOM\H
set PATH=C:\WATCOM\BINW;C:\DOS
if "%1"=="" goto default
wcc -ms %1.C -fo=%1.OBJ
if errorlevel 1 goto end
wlink system dos name %1.EXE file %1.OBJ
goto end
:default
wcc -ms VEWCIS.C -fo=VEWCIS.OBJ
if errorlevel 1 goto end
wlink system dos name VEWCIS.EXE file VEWCIS.OBJ
:end
