import time
import serial

# configure the serial port
ser = serial.Serial(
 port='COM3',
 baudrate=115200, # must match the baudrate in the assembly code
 parity=serial.PARITY_NONE,
 stopbits=serial.STOPBITS_TWO,
 bytesize=serial.EIGHTBITS
)
ser.isOpen()
while 1:
    line = ser.readline()        # Wait for a line ending in \n
    if line:
        # Turn bytes like b'25\n' into the string '25'
        string_val = line.decode('utf-8').strip()
        print(f"Temperature is: {string_val} C")

# this code is the bridge between the MCU and the PC.