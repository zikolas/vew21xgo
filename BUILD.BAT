@echo off
rem Build a Watcom tool: BUILD [name]   (default VEW21XGO, e.g. BUILD VEWCIS)
set WATCOM=C:\WATCOM
set INCLUDE=C:\WATCOM\H
set PATH=C:\WATCOM\BINW;C:\DOS
if "%1"=="" goto default
wcc -ms %1.C -fo=%1.OBJ
if errorlevel 1 goto end
wlink system dos name %1.EXE file %1.OBJ
goto end
:default
wcc -ms VEW21XGO.C -fo=VEW21XGO.OBJ
if errorlevel 1 goto end
wlink system dos name VEW21XGO.EXE file VEW21XGO.OBJ
:end
