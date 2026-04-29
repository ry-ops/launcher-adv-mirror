```
.
├── platformio.ini
├── boards
    ├── _JsonFiles
    │   └── [board].json
    ├── [board]
    │   ├── interface.cpp
    |   └── platformio.ini
    ├── pinouts
    │   ├── pins_arduino.h
    │   └── [target].h
    └── Readme.md

...
```

# Files
(Replace \[board] with the board name)

## boards/pinouts/pins_arduino.h
This is where you will put the flag that will include your boards pinouts header.

## boards/pinouts/\[target].h
This file has the needed variables and pinouts for the board. No need to change anything here, unless if adding new Targets
Here is an official example and what we are actually using here:
https://github.com/espressif/arduino-esp32/blob/master/variants/esp32s3/pins_arduino.h

## boards/\[board]/interface.cpp
This is where you do the board specific setup code

## boards/_JsonFiles/\[board].json
This is the board config. Look at other boards for whats needed.
Here is an offical example and what we are actually using here:
https://github.com/platformio/platform-espressif32/blob/master/boards/esp32-s3-devkitc-1.json

## boards/\[board]\platformio.ini
This is the platformio config for the device, where the Macro definitions must be added. Look at other boards for whats needed.
