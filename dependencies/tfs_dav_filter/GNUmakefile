# Lockbox: Encrypted File System
# Copyright (C) 2014 Rian Hunter <rian@alum.mit.edu>
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU Lesser General Public License as published by
# the Free Software Foundation, either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>

include config.mk

DLLTOOL ?= dlltool

SAFE_SIMREP_SRC_C = safe_simrep.c
SAFE_RAMDISK_SRC_CPP = ramdisk.cpp

SAFE_SIMREP_OBJ = ${SAFE_SIMREP_SRC_C:.c=.c.o}
SAFE_RAMDISK_OBJ =${SAFE_RAMDISK_SRC_CPP:.cpp=.cpp.o}

SAFE_RAMDISK_TARGET = safe_ramdisk.sys
SAFE_SIMREP_TARGET = safe.sys

CPPFLAGS ?= -I$(CURDIR) -I$(DDKINCLUDE) -I$(CURDIR)/../../src $(if $(RELEASE),,-DDBG)
CXXFLAGS ?= $(CPPFLAGS) -fno-rtti -fno-exceptions -nostdinc++ -fno-threadsafe-statics -std=c++11 -Wall -Werror $(if $(RELEASE),-Os -flto,)
CFLAGS ?= $(CPPFLAGS) -Wall -Werror $(if $(RELEASE),-Os -flto,)

all: $(SAFE_RAMDISK_TARGET) $(SAFE_SIMREP_TARGET)

tfs_dav_filter.cpp: ntoskrnl_cpp.hpp fltkernel.h
ramdisk.cpp: ntoskrnl_cpp.hpp

%.cpp.o: %.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

%.c.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

%.a: %.def
	$(DLLTOOL) -k -d $^ -l $@

$(SAFE_SIMREP_TARGET): $(SAFE_SIMREP_OBJ) libmyntoskrnl.a libmyfltmgr.a
	$(CC) 	-Wl,--base-file,base.tmp \
		-Wl,--entry,_DriverEntry@8 \
		-nostartfiles -nostdlib  -static-libgcc \
		-o junk.tmp \
		$(CFLAGS) \
		-L$(CURDIR) \
		$(SAFE_SIMREP_OBJ) \
		-lmyntoskrnl -lmyfltmgr -lhal -lgcc
	-rm -f junk.tmp

	$(DLLTOOL) --dllname $@ --base-file base.tmp --output-exp ${@:.sys=.exp}
	-rm -f base.tmp

	$(CC)	-Wl,--subsystem,native \
		-Wl,--image-base,0x10000 \
		-Wl,--file-alignment,0x1000 \
		-Wl,--section-alignment,0x1000 \
		-Wl,--entry,_DriverEntry@8 \
		-Wl,${@:.sys=.exp} \
		-mdll \
		-nostartfiles -nostdlib -static-libgcc \
		-o $@ \
		$(CFLAGS) \
		-L$(CURDIR) \
		$(SAFE_SIMREP_OBJ) \
		-lmyntoskrnl -lmyfltmgr -lhal -lgcc

$(SAFE_RAMDISK_TARGET): $(SAFE_RAMDISK_OBJ) libmyntoskrnl.a
	$(CC) 	-Wl,--base-file,base.tmp \
		-Wl,--entry,_DriverEntry@8 \
		-nostartfiles -nostdlib  -static-libgcc \
		-o junk.tmp \
		$(CFLAGS) \
		-L$(CURDIR) \
		$(SAFE_RAMDISK_OBJ) \
		-lmyntoskrnl -lhal -lgcc
	-rm -f junk.tmp

	$(DLLTOOL) --dllname $@ --base-file base.tmp --output-exp ${@:.sys=.exp}
	-rm -f base.tmp

	$(CC)	-Wl,--subsystem,native \
		-Wl,--image-base,0x10000 \
		-Wl,--file-alignment,0x1000 \
		-Wl,--section-alignment,0x1000 \
		-Wl,--entry,_DriverEntry@8 \
		-Wl,${@:.sys=.exp} \
		-nostartfiles -nostdlib -static-libgcc \
		-o $@ \
		$(CFLAGS) \
		-L$(CURDIR) \
		$(SAFE_RAMDISK_OBJ) \
		-lmyntoskrnl -lhal -lgcc
# TODO: use stampinf or similar tool to make this automatic
	@echo "New safe_ramdisk.sys made, make sure to tweak DriverVer in the safe_ramdisk.inf"

elevate.exe: elevate.c
	$(CC) -mconsole -Wall -Werror -o $@ $^

install_non_pnp_device.exe: install_non_pnp_device.c
	$(CC) -mconsole -Wall -Werror -std=c99 -o $@ $^ -lsetupapi -lnewdev

reload: $(SAFE_SIMREP_TARGET) elevate.exe \
	install_non_pnp_device.exe $(SAFE_RAMDISK_TARGET)
	elevate.exe reload.bat

clean:
	rm -f $(SAFE_SIMREP_TARGET) $(SAFE_RAMDISK_TARGET)
	rm -f $(SAFE_SIMREP_OBJ) $(SAFE_RAMDISK_OBJ)
	rm -f libmyntoskrnl.a libmyfltmrg.a
	rm -f elevate.exe install_non_pnp_device.exe

.PHONY: clean install
