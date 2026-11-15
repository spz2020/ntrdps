# ntrdps - RDP console server for Windows NT
# Copyright (c) 2026 ntrdps authors. MIT License, see LICENSE.

CC         = cl
CFLAGS     = -nologo -c -Zl -W3 -O2 -GF- -Oy- -Gy -D_ARM_=1 -DWIN32 -D_WIN32
CFLAGS_MT  = $(CFLAGS) -MT -DNTRDPS_TLS -DNTRDPS_LOGIN
CFLAGS_DLL = $(CFLAGS) -FIstcrt.h

TLS        = 1
BRCFLAGS   = -nologo -c -Zl -W0 -Od -D_ARM_=1 -DWIN32 -D_WIN32 -MT
BRSTDINT   = -Itls\bearssl\compat

LINK       = link
LFLAGS     = -nologo -machine:arm -release -version:3.50 -osversion:3.50
IMPLIB     = lib -nologo -machine:arm

EXE_CON    = -subsystem:console,3.50
EXE_GUI    = -subsystem:windows,3.50
LIBS_MT    = libcmt.lib kernel32.lib

DLL_FLAGS  = -dll -nodefaultlib -subsystem:windows,3.50 -entry:DllMain
DLL_RTLIB  = libc.lib

WINLIBS    = user32.lib gdi32.lib advapi32.lib wsock32.lib

PINT_DEFS  = -DNO_NT40
PINT_FILES = setup\nt35\ntrdps.exe setup\nt35\ntrdpshk.dll setup\nt35\ntrdpsly.dll
!include pintset.mak
