# ntrdps - RDP console server for Windows NT
# Copyright (c) 2026 ntrdps authors. MIT License, see LICENSE.

CC       = cl
CFLAGS   = -nologo -c -Zl -kernel -W3 -O2 -GF- -Oy- -Gy -GR- \
           -DNT_INST=0 -DCONDITION_HANDLING=1 -DWIN32_LEAN_AND_MEAN=1 \
           -DWIN32=100 -D_NT1X_=100 -DWINNT=1 -D_WIN32_WINNT=0x0400 \
           -DDEVL=1 -DFPO=1 -DNDEBUG -FIwarning.h \
           -I$(PINTSDK)\include\ddk -I.
CFLAGS_MINIPORT = $(CFLAGS)
LINK     = link
LFLAGS   = -nologo -machine:arm -release -opt:ref,noicf -driver -nodefaultlib \
           -version:4.00 -osversion:4.00
IMPLIB   = lib -nologo -machine:arm
RC       = rc
GDIDRV_FLAGS   = -dll -subsystem:native,4.00 -base:0x10000 \
                 -entry:DrvEnableDriver
GDIDRV_LIBS    = win32k.lib libcntpr.lib
MINIPORT_FLAGS = -subsystem:native,4.00 -base:0x10000 -entry:DriverEntry
MINIPORT_LIBS  = videoprt.lib libcntpr.lib

!include mirror.mak
