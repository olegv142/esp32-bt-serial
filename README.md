# esp32-bt-serial
ESP32 classic bluetooth to serial port bridge

# Motivation

Most existing bluetooth bridges are based on the Bluecore 4 chip. It is pretty old and has issues while working with baud rates higher than default 115200. The hardware flow control implementation on this family of devices seems to be the kind of the software one. The RTS signal may be delayed by an arbitrary amount of time so that working on baud rates higher than default leads to random buffer overflow and data lost unless you always transferring small chunks of data fitting entirely onto the receiver buffer.
The ESP32 on the other hand provides an excellent platform for BT to UART bridge implementation. 

# Build environment

See https://docs.espressif.com/projects/esp-idf/en/latest/get-started/index.html

# Configuring

The bridge has connection indicator output, serial data RX/TX lines and flow control lines RTS/CTS, the last one is optional. All 5 pin locations can be configured by running *make menuconfig*. Besides one can configure UART baud rate, buffer size and bluetooth device name prefix. The full device name consists of the user defined prefix followed by 6 symbols derived from the device MAC address. Such scheme is convenient in case you have more than one device since it provides the way to distinguish them. The supported serial baud rates are in the range from 9600 to 1843200 with 921600 being the default since it matches the baud rate of the bluetooth channel itself. So further increasing baud rate has no practical sense.

# Power consumption

35mA in idle state, 130mA while transferring data at maximum rate. A little high but you have got high data rate and excellent range.

# Range

Exceptional. Several timer better than with anything based on the Bluecore chips. I've got stable connection with 10 meters distance and 2 concrete walls in between.



