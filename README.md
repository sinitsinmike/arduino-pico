# Arduino-Pico [![Join the chat at https://gitter.im/arduino-pico/community](https://badges.gitter.im/arduino-pico/community.svg)](https://gitter.im/arduino-pico/community?utm_source=badge&utm_medium=badge&utm_campaign=pr-badge&utm_content=badge)



Raspberry Pi Pico Arduino core, for all RP2040 boards

This is a port of the RP2040 (Raspberry Pi Pico processor) to the Arduino ecosystem.

It uses a custom toolset with GCC 10.2 and Newlib 4.0.0, not depending on system-installed prerequisites.  https://github.com/earlephilhower/pico-quick-toolchain

There is automated discovery of boards in bootloader mode, so they show up in the IDE, and the upload command works using the Microsoft UF2 tool (included).

# Installing via Arduino Boards Manager
**Windows Users**: Please do not use the Windows Store version of the actual Arduino application
because it has issues detecting attached Pico boards.  Use the "Windows ZIP" or plain "Windows"
executable (EXE)  download direct from https://arduino.cc. and allow it to install any device
drivers it suggests.  Otherwise the Pico board may not be detected.  Also, if trying out the
2.0 beta Arduino please install the release 1.8 version beforehand to ensure needed device drivers
are present.  (See #20 for more details.)

**Raspberry Pi 3/4 Users**:  Sorry, but there is no support for your computer at this time.  I am
in need of a more modern GCC cross-compiler than is available from the Raspbery PI repo (gcc 4.xxx)
in order to build the compiler for your systems.  Any help finding such a cross-compiler would be
appreciated.

Open up the Arduino IDE and go to File->Preferences.

In the dialog that pops up, enter the following URL in the "Additional Boards Manager URLs" field:

https://github.com/earlephilhower/arduino-pico/releases/download/global/package_rp2040_index.json

![image](https://user-images.githubusercontent.com/11875/111917251-3c57f400-8a3c-11eb-8120-810a8328ab3f.png)

Hit OK to close the dialog.

Go to Tools->Boards->Board Manager in the IDE

Type "pico" in the search box and select "Add":

![image](https://user-images.githubusercontent.com/11875/111917223-12063680-8a3c-11eb-8884-4f32b8f0feb1.png)

# Installing via GIT
To install via GIT (for latest and greatest versions):
````
mkdir -p ~/Arduino/hardware/pico
git clone https://github.com/earlephilhower/arduino-pico.git ~/Arduino/hardware/pico/rp2040
cd ~/Arduino/hardware/pico/rp2040
git submodule update --init
cd pico-sdk
git submodule update --init
cd ../tools
python3 ./get.py
`````

# Installing both Arduino and CMake
Tom's Hardware presented a very nice writeup on installing `arduino-pico` on both Windows and Linux, available at https://www.tomshardware.com/how-to/program-raspberry-pi-pico-with-arduino-ide

If you follow Les' step-by-step you will also have a fully functional `CMake`-based environment to build Pico apps on if you outgrow the Arduino ecosystem.

# Uploading Sketches
To upload your first sketch, you will need to hold the BOOTSEL button down while plugging in the Pico to your computer.
Then hit the upload button and the sketch should be transferred and start to run.

After the first upload, this should not be necessary as the `arduino-pico` core has auto-reset support. 
Select the appropriate serial port shown in the Arduino Tools->Port->Serial Port menu once (this setting will stick and does not need to be
touched for multiple uploads).   This selection allows the auto-reset tool to identify the proper device to reset.
Them hit the upload button and your sketch should upload and run.

In some cases the Pico will encounter a hard hang and its USB port will not respond to the auto-reset request.  Should this happen, just
follow the initial procedure of holding the BOOTSEL button down while plugging in the Pico to enter the ROM bootloader.

# Uploading Filesystem Images
The onboard flash filesystem for the Pico, LittleFS, lets you upload a filesystem image from the sketch directory for your sketch to use.  Download the needed plugin from
* https://github.com/earlephilhower/arduino-pico-littlefs-plugin/releases

To install, follow the directions in 
* https://github.com/earlephilhower/arduino-pico-littlefs-plugin/blob/master/README.md 

For detailed usage information, please check the ESP8266 repo documentation (ignore SPIFFS related notes) available at
* https://arduino-esp8266.readthedocs.io/en/latest/filesystem.html

# Status of Port
Lots of things are working now!
* digitalWrite/Read
* shiftIn/Out
* SPI
* analogWrite/PWM
* tone/noTone
* Wire/I2C Master and Slave
* EEPROM
* USB Serial(ACM) w/automatic reboot-to-UF2 upload
* Hardware UART
* Servo, glitchless
* Overclocking and underclocking from the menus
* analogRead and Pico chip temperature
* Filesystems (LittleFS and SD/SDFS)
* printf (i.e. debug) output over USB serial 

The RP2040 PIO state machines (SMs) are used to generate jitter-free:
* Servos
* Tones

# Todo
Some major features I want to add are:
* Updated debug infrastructure
* I2S port from pico-extras

# Tutorials from Across the Web
Here are some links to coverage and additional tutorials for using `arduino-pico`
* Arduino Support for the Pi Pico available! And how fast is the Pico? - https://youtu.be/-XHh17cuH5E

# Contributing
If you want to contribute or have bugfixes, drop me a note at <earlephilhower@yahoo.com> or open an issue/PR here.

-Earle F. Philhower, III
 earlephilhower@yahoo.com
