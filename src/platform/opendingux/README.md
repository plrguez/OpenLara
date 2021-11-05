## OpenLara for mainline OpenDingux

This port is intended for mainline OpenDingux (current beta) running on Ingenic JZ47XX devices.

This port is a copy of the SDL2 platform with some code borrowed from others platforms as nix (rumble) and gcw0 (rumble and main directories).

OpenGL versions use [GL4ES](https://github.com/ptitSeb/gl4es) wrapper on top of OpenGL ES 2.0.

You can obtain latest mainline OpenDingux firmware [here](http://od.abstraction.se/opendingux/latest/)

Thanks to the OpenDingux and OpenLara dev teams.

### Build versions

There are three available builds: GLES2, OpenGL and OpenGL 1

For regular use the **OpenGL** version is the recommended and have the best performance _(See Visual options)_

The others are mainly for test purposes.

### Game content

Game content in searched first in folder `/media/sdcard/OpenLara`

If previous folder does not exist the folder game content is searched in folder `/media/data/local/home/.openlara`

Put audio tracks in folder `/media/sdcard/OpenLara/audio` or `/media/data/local/home/.openlara/audio` depending on where game content is located.

Audio tracks must be named `track_nn[_lang].format` where:
  - `nn` is a secuential number (01, 02,...) or some specific name as CAFE, CANYON, LIFT, PRISON...
  - `lang` is an optional string with lang code: "_EN", "_FR", "_DE", "_ES", "_IT", "_PL", "_PT", "_RU", "_JA", "_GR", "_FI", "_CZ", "_CN", "_HU", "_SV"
  - `format` correspond to a valid audio format: ogg, mp3 or wav.


### Controls

#### Default controls:

Handheld|OpenLara
--|--
Cursors         |Movement
Left Stick      |Movement
Right Stick     |Look around
A               |Action
B               |Roll
Y               |Draw weapon
X               |Jump
L1              |Look
R1              |Walk
L2              |Duck
R2              |Dash
Select          |Inventory
Start           |Enter

#### In menus:

Handheld|OpenLara
--|--
A/Start         |Accept/Enter
B/Select        |Cancel/Go back

#### Hotkeys

Handheld|OpenLara
--|--
 L1 + Y         |Quick save
L1 + X          |Quick load


### Visual options

Default values (it is recommended not to increase these values)

Option|default value
--|--
Filtering |Medium
Lighting  |Medium
Shadows   |Medium
Water     |Low

### How to Build

Change to the directory where opendingux platform is located

    $ cd src/platform/opendingux

Put your toolchain in path

    $ export PATH=/opt/opendingux/usr/bin:$PATH

To build SDL2/OpenGL use the commmand

    $ make

To build SDL2/GLES2 use the commmand

    $ make GLES2=1

By default `/opt/opendingux` is the default toolchain location. 

If your toolchain is located in another place you can launch make passing the **TOOLCHAIN** variable

    $ make TOOLCHAIN=your_own_toolchain_location
