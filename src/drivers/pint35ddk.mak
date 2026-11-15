# ntrdps - RDP console server for Windows NT
# Copyright (c) 2026 ntrdps authors. MIT License, see LICENSE.

CC       = cl
CFLAGS   = -nologo -c -Zl -W3 -O2 -GF- -Oy- -Gy -D_ARM_=1 \
           -DNT_INST=0 -DCONDITION_HANDLING=1 -DWIN32_LEAN_AND_MEAN=1 \
           -DWIN32=100 -D_NT1X_=100 -DDBG=0 -DDEVL=1 -DFPO=1 -DNDEBUG \
           -FIstcrt.h -I$(PINTSDK)\include\ddk -I.
LINK     = link
LFLAGS   = -nologo -machine:arm -release -opt:ref,noicf -nodefaultlib \
           -version:3.50 -osversion:3.50
IMPLIB   = lib -nologo -machine:arm
DISPDLL_FLAGS = -dll -noentry -subsystem:console,3.50
DISPDLL_LIBS  = kernel32.lib winsrv.lib libc.lib

LAYER_DEF = layer\layer351.def
LAYER_RES = layer\layer.res
RCFLAGS   =
!include layer.mak
