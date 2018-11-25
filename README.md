# esp32-bt-serial
ESP32 classic bluetooth to serial port bridge

## Motivation

Most existing bluetooth bridges are based on the Bluecore 4 chip. It is pretty old and has issues while working with baud rates higher than default 115200. The hardware flow control implementation on this family of devices seems to be the kind of the software one. The RTS signal may be delayed by an arbitrary amount of time so that working on baud rates higher than default leads to random buffer overflow and data lost unless you always transferring small chunks of data fitting entirely onto the receiver buffer.
The ESP32 on the other hand provides an excellent platform for BT to UART bridge implementation. 

## Build environment

See https://docs.espressif.com/projects/esp-idf/en/latest/get-started/index.html

## Configuring

The bridge has connection indicator output, serial data RX/TX lines and flow control lines RTS/CTS, the last one is optional. All 5 pin locations can be configured by running *make menuconfig*. Besides one can configure UART baud rate, buffer size and bluetooth device name prefix. The full device name consists of the user defined prefix followed by 6 symbols derived from the device MAC address. Such scheme is convenient in case you have more than one device since it provides the way to distinguish them. The supported serial baud rates are in the range from 9600 to 1843200 with 921600 being the default since it matches the baud rate of the bluetooth channel itself. So further increasing baud rate has no practical sense.

## Bilding

Just issue *make* command in the project directory

## Flashing

Unless you have dev kit with USB programmer included you will need some minimal wiring made to the ESP32 module to be able to flash it. The following figure shows an example of such setup with programming connections shown in blue. The connections providing serial interface to the your system are shown in black.
![ESP32 module wiring](https://github.com/olegv142/esp32-bt-serial/blob/master/doc/wiring.png)
You can use virtually any USB-serial bridge capable of working at 115200 baud. The IO0 pin shoud be connected to the ground while powering up the module in order to turn it onto serial programming mode. After that you can issue *make flash* command in the project directory and wait for the programming completion. Then turn off power, disconnect programming circuit and enjoy using your brand new bluetooth serial bridge.

## Connections

You can use hardware flow control CTS/RTS lines or ignore them depending on your system design details. Basically not using RTS line is safe if packets you are sending to the module's RXD line are not exceeding 128 bytes. The CTS line usage is completely up to your implementation of the serial data receiver. If you are not going to use CTS line you should either connect it to the ground or disable at firmware build stage by means of *make menuconfig*. The EN line plays the role of the reset to the module. Low level on this line turns the module onto the reset state with low power consumption. In case you are not going to use this line it should be pulled up. The pull up resistor on the TXD line is needed to prevent this line from floating during module boot.

## Troubleshooting

The ESP32 module is using the same serial channel used for programming to print error and debug messages. So if anything goes wrong you can attach the programming circuit without grounding the IO0 pin and monitor debug messages during module boot.

## Power consumption

35mA in idle state, 130mA while transferring data at maximum rate. A little more than average but you have got high data rate and excellent range.

## Range

Exceptional. Several timer better than with anything based on the Bluecore chips. I've got stable connection with 10 meters distance and 2 concrete walls in between.



