# Build options
#DEBUG=1

#By default SDL2/GLES3 is built, which is an extension to GLES2 so we use -lGLESv2

#Uncomment to build SDL2/GLES version, GLES2
#GLES2=1

#Uncomment to build SDL2/OpenGL (Recommended for OD beta)
#OpenGL in OpenDingux use GL4ES
#  - 2 for OpenGL 2.1 GLES 2.0 backend
#  - 1 for OpenGL 1.5 GLES 1.1 backend
#OPENGL=2

# Source
SRCS=main.cpp ../../libs/stb_vorbis/stb_vorbis.c ../../libs/minimp3/minimp3.cpp ../../libs/tinf/tinflate.c

#Targets
BIN=OpenLara
ifeq ($(OPENGL),2)
OPK=$(BIN)_opengl.opk
BUILD_TYPE=OpenGL
else
ifeq ($(OPENGL),1)
OPK=$(BIN)_opengl1.opk
BUILD_TYPE=OpenGL 1.5
else
ifdef GLES2
OPK=$(BIN)_gles2.opk
BUILD_TYPE=GLES2
else
OPK=$(BIN)_gles3.opk
BUILD_TYPE=GLES3
endif
endif
endif

# Resources
DESKTOP_FILE=default.gcw0.desktop
EXEC_FILE=$(BIN)
ifeq ($(OPENGL),1)
SHELL_GL1_FILE=openlara_gl1.sh
EXEC_FILE=$(SHELL_GL1_FILE)
endif
ICON=icon.png
MANUAL_SKL=readme.skl
MANUAL=readme.txt

# Toolchain
TOOLCHAIN=/opt/opendingux-toolchain
BINDIR=$(TOOLCHAIN)/usr/bin
CC = $(BINDIR)/mipsel-linux-gcc
CXX = $(BINDIR)/mipsel-linux-g++
LD = $(BINDIR)/mipsel-linux-g++
STRIP = $(BINDIR)/mipsel-linux-strip

SYSROOT     := $(shell $(CC) --print-sysroot)
SDL_CONFIG  := $(SYSROOT)/usr/bin/sdl2-config
SDL_CFLAGS  := $(shell $(SDL_CONFIG) --cflags)
SDL_LIBS    := $(shell $(SDL_CONFIG) --libs)

# Flags
DEFS = -D__SDL2__ -D__ODBETA__

ifdef OPENGL
DEFS += -D_SDL2_OPENGL
else
DEFS += -DSDL2_GLES
ifdef GLES2
DEFS += -D_GAPI_GLES2
endif
endif

F_OPTS = -fno-exceptions -fno-rtti -ffunction-sections -fdata-sections
O_OPTS = -std=c++14 -mips32r2
ifdef DEBUG
O_OPTS += -O0 -g3
else
DEFS += -DNDEBUG
O_OPTS += -O3 -g0
endif
W_OPTS = -Wl,--gc-sections
CFLAGS+=$(DEFS) $(O_OPTS) $(F_OPTS) $(W_OPTS) $(SDL_FLAGS)

# Libraries
O_LIBS+= 
ifdef OPENGL
L_LIBS+=-lGL
else
L_LIBS+=-lGLESv2 -lEGL
endif
L_LIBS+=-lpthread -lrt -lm -Loaudio -ludev -lshake
LDFLAGS+=$(SDL_LIBS) $(L_LIBS) $(O_LIBS)

INCLUDES+=-I/opt/vc/include -I../../

# Create desktop file
export define desktopfile
cat > $(DESKTOP_FILE) << 'EOF'
[Desktop Entry]
Name=OpenLara $(BUILD_TYPE)
Comment=Classic Tomb Raider open-source engine
Exec=$(EXEC_FILE)
Terminal=false
Type=Application
StartupNotify=true
Icon=icon
Categories=games;
X-OD-NeedsDownscaling=false
X-OD-Manual=$(MANUAL)
EOF
endef

# Create shell to launch build SDL2/OpenGL 1.5
export define shell_gl1
cat > $(SHELL_GL1_FILE) << 'EOF'
#! /bin/sh
LIBGL_ES=1 LIBGL_GL=15 ./$(BIN)
EOF
chmod ugo+x $(SHELL_GL1_FILE)
endef

# Rules
$(OPK) : $(BIN) $(DESKTOP_FILE) $(ICON) $(SHELL_GL1_FILE) $(MANUAL)
	@mksquashfs \
	$(DESKTOP_FILE) \
	$(SHELL_GL1_FILE) \
	$(ICON) \
	$(MANUAL) \
	$(BIN) \
	$(OPK) \
	-all-root -noappend -no-exports -no-xattrs

$(DESKTOP_FILE) : $(MANUAL_SKL)
	@cp -f $(MANUAL_SKL) $(MANUAL)
	@sed -i "s/BUILD_DATE/$$(date +%Y-%m-%d)/g" $(MANUAL)
	@sed -i "s/BUILD_TYPE/$(BUILD_TYPE)/g" $(MANUAL)
	@eval "$$desktopfile"
ifeq ($(OPENGL),1)
	@eval "$$shell_gl1"
endif

$(BIN) : $(SRCS)
	@echo Compiling $<...
	$(CXX) $(CFLAGS) $(INCLUDES) $(LDFLAGS) $^ -o $@
ifndef DEBUG
	$(STRIP) $@ --strip-all --remove-section=.comment --remove-section=.note
endif

clean:
	@rm -f $(BIN) $(OPK) $(DESKTOP_FILE) $(MANUAL) $(SHELL_GL1_FILE)

