import serial
import time

PORT = "/dev/cu.usbmodem190622201"
BAUD = 115200

T_START = 25.0
T_END = 1200.0
RAMP_TIME = 60.0

ser = serial.Serial(PORT, BAUD, timeout=1)

start = time.time()

while True:
    t = time.time() - start
    alpha = min(t / RAMP_TIME, 1.0)

    temp = T_START + (T_END - T_START) * alpha

    tc1 = temp
    tc2 = temp
    tc3 = temp

    line = f"T,{tc1:.2f},{tc2:.2f},{tc3:.2f}\n"
    ser.write(line.encode("ascii"))

    time.sleep(0.2)
