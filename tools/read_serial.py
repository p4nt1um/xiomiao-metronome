"""Read COMx logs from the Xiaomiao device (GD32 USB-CDC bridge).

Line semantics (esptool classic auto-download circuit):
  DTR=1 RTS=1 -> EN/IO0 released (run) and GD32 CDC forwards data
  DTR=1 RTS=0 -> EN held low (hard reset)
So: open with DTR=RTS=True, then pulse RTS low briefly to reset the target,
then passively capture output for N seconds.

Usage: python read_serial.py [PORT] [SECONDS]
"""
import sys
import time

import serial

port = sys.argv[1] if len(sys.argv) > 1 else "COM8"
secs = float(sys.argv[2]) if len(sys.argv) > 2 else 12.0

s = serial.Serial()
s.port = port
s.baudrate = 115200
s.timeout = 0.2
s.dtr = True  # keep CDC forwarding; IO0 stays released while RTS also high
s.rts = True
s.open()

# hard reset pulse (EN low) — recovers from download-mode glitch at open too
s.rts = False
time.sleep(0.1)
s.rts = True

end = time.time() + secs
out = b""
while time.time() < end:
    out += s.read(4096)
s.close()
sys.stdout.buffer.write(out)
sys.stdout.buffer.flush()
