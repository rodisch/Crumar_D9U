# RoDi_D9U

## A drawbar controller for the Roland FA-06/07/08

This is a firmware extension for the GMLAB-D9U hardware to control the SuperNatural drawbar organ of a Roland FA-06/07/08. The firmware sends sysex messages to part 1 and part 2 of the current studioset of the FA. Switching between the parts is done via the mode button at the back of the D9U-device.

In addition the features of the original D9U firmware are maintained. During compilation it can be selected, if CCs and sysex go to USB or serial MIDI-output, see 'what goes where' in source code. Default configuration is sysex to serial output to connect to the FA-06/07/08 and CCs to USB for DAW-/VST controls.

## Libraries

These libraries must be installed via the library manager of Arduino:
- "MIDI Library" by Francois Best, lathoub v5.0.2
- "USB-MIDI" by lathoub v1.1.2

The library "USBMIDI" by Gary Grewal is **not** used.

Choose "Arduino Micro" as board. 

## original repositry
https://github.com/ZioGuido/Crumar_D9U


> # GMLAB_D9U
> This is the Arduino project for GMLAB D9U, an open source DIY drawbar  controller.
> Please refer to the included PDF for details about the project, the code and the electronics.



