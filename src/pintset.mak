# ntrdps - RDP console server for Windows NT
# Copyright (c) 2026 ntrdps authors. MIT License, see LICENSE.

SETUP_DEFS = -DNO_NT31 $(PINT_DEFS)

all: ntrdpset.exe

setup\setup.obj: setup\setup.c setup\setup.h service\version.h
	$(CC) $(CFLAGS_MT) $(SETUP_DEFS) -Iservice -Idrivers\common -Fosetup\setup.obj setup\setup.c

setup\setup.res: setup\setup.rc setup\setup.h $(PINT_FILES)
	rc -r $(SETUP_DEFS) -DNTRDPS_LOGIN -fo setup\setup.res setup\setup.rc

ntrdpset.exe: setup\setup.obj setup\setup.res
	$(LINK) $(LFLAGS) $(EXE_GUI) \
	    -out:ntrdpset.exe setup\setup.obj setup\setup.res $(LIBS_MT) \
	    user32.lib advapi32.lib gdi32.lib
