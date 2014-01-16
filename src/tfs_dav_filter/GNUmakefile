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

SRC_CPP = tfs_dav_filter.cpp
OBJ = ${SRC_CPP:.cpp=.cpp.o}

CPPFLAGS += -I$(CURDIR)
CXXFLAGS ?= $(CPPFLAGS) -fno-rtti -fno-exceptions -nostdinc++ -fno-threadsafe-statics -std=c++11

all: tfs_dav_filter.sys

%.cpp.o: %.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

%.a: %.def
	$(DLLTOOL) -k -d $^ -l $@

tfs_dav_filter.sys: $(OBJ) libmyntoskrnl.a libmyfltmgr.a
	$(CC) 	-Wl,--base-file,base.tmp \
		-Wl,--entry,_DriverEntry@8 \
		-nostartfiles -nostdlib  -static-libgcc \
		-o junk.tmp \
		-L$(CURDIR) \
		$(OBJ) \
		-lmyntoskrnl -lmyfltmgr -lgcc
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
		-L$(CURDIR) \
		$(OBJ) \
		-lmyntoskrnl -lmyfltmgr -lgcc

clean:
	rm -f tfs_dav_filter.sys
	rm -f $(OBJ)
	rm -f libmyntoskrnl.a libmyfltmrg.a

.PHONY: clean
