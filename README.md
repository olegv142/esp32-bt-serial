# esp32-bt-serial
ESP32 dual mode bluetooth to serial port bridge

## Motivation

Most existing bluetooth bridges are based on the Bluecore 4 chip. It is pretty old and has issues while working with baud rates higher than default 115200. The hardware flow control implementation on this family of devices seems to be the kind of the software one. The RTS signal may be delayed by an arbitrary amount of time so that working on baud rates higher than default leads to random buffer overflow and data lost unless you always transferring small chunks of data fitting entirely onto the receiver buffer.

The ESP32 on the other hand provides an excellent platform for BT to UART bridge implementation. It even supports working in classic BT and BT low energy (BLE) modes simultaneously. This project was developed in attempt to create flexible wireless communication solution for some embedded controller. Typically it needs fast interface for commands and responses with option for using it for firmware update. Such communication channel is best implemented over classic Bluetooth link. The BLE channel has its own benefits though. The host side of the BLE connection may be implemented in web page using web BLE API. Such approach allows you to write code once and run it in the browser on all popular desktop and mobile platforms such as Windows, Linux, Android and iOS. Since BLE channel is slow its best suited for controller monitoring. Currently this project implements it in one direction only transmitting data from controller to monitoring application.

## Configuring

The bridge has connection indicator output, serial data RX/TX lines and flow control lines RTS/CTS, the last one is optional and is not enabled by default. All pin locations can be configured by running *make menuconfig*. Besides one can configure UART baud rate, buffer size and bluetooth device name prefix. The full device name consists of the user defined prefix followed by 6 symbols derived from the device MAC address. Such scheme is convenient in case you have more than one device since it provides the way to distinguish them. The supported serial baud rates are in the range from 9600 to 1843200 with 921600 being the default since it matches the baud rate of the bluetooth channel itself. So further increasing baud rate has no practical sense.

## Flashing

Unless you have dev kit with USB programmer included you will need some minimal wiring made to the ESP32 module to be able to flash it. The following figure shows an example of such setup with programming connections shown in blue. The connections providing interface to your system are shown in black.
![ESP32 module wiring](https://github.com/olegv142/esp32-bt-serial/blob/master/doc/wiring.png)
You can use virtually any USB-serial bridge capable of working at 115200 baud. The IO0 pin should be connected to the ground while powering up the module in order to turn it onto serial programming mode. After that you can issue *make flash* command in the project directory and wait for the flashing completion. Then turn off power, disconnect programming circuit and enjoy using your brand new bluetooth serial bridge.

## Connections

You can use hardware flow control CTS/RTS lines or ignore them depending on your system design details. Basically not using RTS line is safe if packets you are sending to the module's RXD line are not exceeding 128 bytes. The CTS line usage is completely up to your implementation of the serial data receiver. If you are not going to use CTS line you should either connect it to the ground or disable at firmware build stage by means of *make menuconfig*. The EN line plays the role of the reset to the module. Low level on this line turns the module onto the reset state with low power consumption. In case you are not going to use this line it should be pulled up. The pull up resistors on the TXD and RTS lines are needed to prevent them from floating during module boot.

## Alternative settings

Pulling low nALT input pin (IO4 by default) while powering on activates alternative settings for baud rate and device name. It may be handy in case you need separate settings for flashing firmware for example. With this application in mind the serial protocol in alt mode does not use hardware flow control but does transmit even parity bit (to be compatible with STM32 boot-loader) though it may be disabled in config. Leave nALT pin floating if alternative setting are not required.

While in alternative mode the ALT indicator pin (IO32 by default) is pulled high. Otherwise it is left in high impedance state. Such behavior is handy in case the alternative mode is used for upgrading firmware of the controller that is normally driving EN input. If ALT indicator pin is connected to EN input it will keep ESP32 module active while upgrading firmware of the controller.

## BLE adapter

The BLE communication channel uses separate BLE_RXD data input. It expects even parity bit by default though it may be disabled in config. Hardware flow control is not used.
The BLE transmits data received from serial input by updating 'characteristic' since BLE has no notion of the serial communication channel at all. Updates are delivered to monitoring application which subscribes to them. In theory this mechanism is inherently unreliable since the update may be lost. Though reliable update delivery is possible (its called 'indication') the web BLE API does not support such mechanism.

To control updates delivery the BLE adapter inserts sequence tag as the first symbol of the characteristic value. The sequence tag is assigned a values from 16 characters sequence 'a', 'b', .. 'p'. The next update uses next letter as sequence tag. The 'p' letter is followed by the 'a' again. The sequence tag symbol is followed by the data to be transmitted. The receiving application may use sequence tags to detect lost chunks of data transmitted or just ignore them. An example web page receiving BLE data with sequence tags validation may be found in *www* folder.

If you don't need BLE communication channel it may be disabled completely by setting Bluetooth controller mode to *BR/EDR Only* instead of *Dual Mode* in *Components config*.

## Testing

The *test* folder contains two python2 scripts for classic BT and BLE channels testing. The *bt_echo.py* sends random data to the given BT device and expects to receive the same data in response. To run this test one should enable CTS flow control and connect RX-TX and RTS-CTS pins so the adapter will send the same data back. The *ble_test.py* sends randomly generated messages to given serial port which should be connected to BLE_RXD input. The web page in *www* folder receives such data and validates it. It prints data received as well as the total count / the number of corrupt fragments and messages. The test web page is also available at address https://olegv142.github.io/esp32-bt-serial/www/

## Troubleshooting

The ESP32 module is using the same serial channel used for programming to print error and debug messages. So if anything goes wrong you can attach the programming circuit without grounding the IO0 pin and monitor debug messages during module boot.

## Power consumption

35mA in idle state, 110mA while transferring data at maximum rate. A little more than average but you have got high data rate and excellent range.

## Range

Exceptional. Several timer better than with anything based on the Bluecore chips. I've got stable connection with 10 meters distance and 2 concrete walls in between.

## Known issues

Working in classic BT and BLE modes simultaneously is tricky since they use the same transceiver and the same frequency band. So the frequency band should be shared between them properly which apparently is not always done by esp-idf framework. An attempt to connect to the adapter using classic BT while BLE is already paired with monitoring application and sending data to it may fail. The BT stack on windows host may even loose the ability to connect to this adapter till the system reboot. On the other hand the BLE pairing while classic BT connection is established is always possible.

I had to carefully choose BLE advertising flags to make adapter working on various operating systems. The BR_EDR_NOT_SUPPORTED flag is set since setting dual mode flag breaks ability to connect to device on Linux. This is the only flag set on advertising packet since setting connectivity flag leads to listing two devices with identical names on attempt to pair with adapter from Windows host.

## Building

The code can be built with esp-idf version 3.2.5. Later versions of esp-idf need code modifications to build and work properly. Yet newer versions of esp-idf don't improve stability in any way so I decided to keep code based on older version.

## Useful links

Flexible dual role BLE to serial bridge for Arduino using ESP32 https://github.com/olegv142/esp32-ble

Example of the BLE receiver implemented in web page: https://github.com/enspectr/ble-term

Ethernet to serial adapter based on ESP32: https://github.com/olegv142/esp32-eth-serial

Esp-idf installation instructions: https://docs.espressif.com/projects/esp-idf/en/latest/get-started/index.html
