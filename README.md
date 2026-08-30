# Bluetooth Speaker
A portable, battery-powered Bluetooth speaker using ESP32.
## Hardware Overview

### Architecture
This speaker is a compact, lightweight Bluetooth speaker.
For this reason, I decided to use a mono audio configuration.

### Component Selection

#### MP2672A
The MP2672A works with the two lithium-ion batteries in series powering the device.
It also can supply up to 2A system current, which is what is needed for the microcontroller and amplifier.

#### TS30013
The TS30013 regulates the system voltage down to 3.3V, and supplies up to 3A current.
The microcontroller, an ESP32, requires 3.3V to operate.
I chose the 3A version of the regulator because it needs to source enough current
for the ESP32 operating in radio mode with Bluetooth enabled,
as well as the amplifier driving a speaker.
Since I used the fixed-voltage version of the TS30013 to fully minimize external components.

#### ESP32
I committed to an ESP32-series microcontroller early on in the project for a few reasons:
first, at the time I was teaching myself FreeRTOS and ESP-IDF using ESP32;
second, having Bluetooth and microcontroller on the same SoC was convenient;
third, I was already familiar with ESP32.

The most popular ESP32 microcontroller these days is the ESP32-S3, but there was a certain problem with it:
it only supports Bluetooth Low-Energy, but not LE-Audio.
The only ESP32 that supports LE-Audio is the ESP32-C6, which, at the time of this project, had yet to be released.
My only recourse was to use classic Bluetooth with A2DP, though it be less energy efficient.
That meant using the original ESP32.

#### MAX98357A
Whereas the ESP32 sports its own DAC, it is too low resolution at 8 bits to produce a good audio signal.
Moreover, the ESP32 is not capable of sourcing enough current to drive a speaker driver on its own.
Therefore, I included a dedicated audio IC in the speaker, namely, the MAX98357A, an amplifier popular with hobbyists.
It works with I2S (Inter-IC Sound), making it easy to connect to the ESP32.

### Power
The power system of this speaker consists of two parts: the battery management and the voltage regulator.
This speaker uses an MP2672A battery management IC with 5VDC input and two lithium-ion cells in series.

### Interface
The speaker includes several push-buttons to control its operation and playback.
This includes pause/play, skip track, previous track, and Bluetooth.

The speaker also includes a rotary encoder knob for adjusting volume.
I chose an incremental rotary encoder over a simple potentiometer because the volume can also be adjusted from the Bluetooth controller.
In this context, a Bluetooth controller is whatever device connects to the speaker via Bluetooth and streams audio to it, for example, a smartphone.
If the user updates the volume through the Bluetooth controller, but the hypothetical potentiometer was not adjusted with it, there would be a discrepancy between the physical volume control and the Bluetooth volume control.
The incremental rotary encoder solves this issue by not being tied to a certain angle and instead only sending inputs on being rotated.

### PCB Design
The PCB for this speaker was designed with KiCAD.
Following Espressif's recommendations concering ESP32 board design, the board consists of four layers:
first (top) is the primary routing layer, 
second is the ground plane, 
third is the power plane (3.3V), 
and fourth (bottom) is the secondary routing layer.

### Case Design
The case for this speaker was designed using Onshape and manufactured using 3D printing.

## Software Overview
The ESP32 microcontroller was programmed using ESP-IDF.
### Bluetooth
Although less than ideal, due to the requirements I imposed on myself (see above), the speaker microcontroller uses Bluetooth Classic.
More specifically, the ESP32 is configured as a A2DP (Advanced Audio Distribution Profile) sink, and it uses the internal codec to decode the received audio data into PCM frames.
Additionally, the ESP32 uses AVRC to send and receive volume updates from the Bluetooth controller.
