# Build SDL2/GLES version GLES2

# Source and targets
SRCS=main.cpp ../../libs/stb_vorbis/stb_vorbis.c ../../libs/minimp3/minimp3.cpp ../../libs/tinf/tinflate.c
BIN=OpenLara
OPK=$(BIN).opk

# Resources
DESKTOP_FILE=default.gcw0.desktop
ICON=icon.png
MANUAL_SKL=readme.skl
MANUAL=readme.txt

# Build options
#DEBUG=1
#Build with GLES2
#Comment to build SDL2/GLES version, GLES3, which is an extension to GLES2 so we use -lGLESv2, too.
GLES2=1
#OPENGL=1

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
Name=OpenLara
Comment=Classic Tomb Raider open-source engine
Exec=$(BIN)
Terminal=false
Type=Application
StartupNotify=true
Icon=icon
Categories=games;
X-OD-NeedsDownscaling=false
X-OD-Manual=$(MANUAL)
endef

# Rules
$(OPK) : $(BIN) $(DESKTOP_FILE) $(ICON)
	@mksquashfs \
	$(DESKTOP_FILE) \
	$(ICON) \
	$(MANUAL) \
	$(BIN) \
	$(OPK) \
	-all-root -noappend -no-exports -no-xattrs

$(DESKTOP_FILE) :
	@cp -f $(MANUAL_SKL) $(MANUAL)
	@sed -i "s/BUILD_DATE/$$(date +%Y-%m-%d)/g" $(MANUAL)
	@eval "$$desktopfile"

$(BIN) : $(SRCS)
	@echo Compiling $<...
	$(CXX) $(CFLAGS) $(INCLUDES) $(LDFLAGS) $^ -o $@
ifndef DEBUG
	$(STRIP) $@ --strip-all --remove-section=.comment --remove-section=.note
endif

clean:
	@rm -f $(BIN) $(OPK) $(DESKTOP_FILE) $(MANUAL)

