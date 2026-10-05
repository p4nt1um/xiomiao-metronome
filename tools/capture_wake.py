"""Capture COMx logs WITHOUT touching the reset line.

与 read_serial.py 的区别：打开串口不发 RTS 复制位脉冲，适用于绝不能复位目标的
场合（例如：深睡后等待 A 键 ext1 唤醒，抓取唤醒开机日志）。
打开时 DTR=RTS=1（CDC 转发、EN 释放，均为安全电平）。

Usage: python capture_wake.py [PORT] [SECONDS]
"""
import sys
import time

import serial

port = sys.argv[1] if len(sys.argv) > 1 else "COM8"
secs = float(sys.argv[2]) if len(sys.argv) > 2 else 60.0

s = serial.Serial()
s.port = port
s.baudrate = 115200
s.timeout = 0.2
s.dtr = True
s.rts = True
s.open()

end = time.time() + secs
out = b""
while time.time() < end:
    out += s.read(4096)
s.close()
sys.stdout.buffer.write(out)
sys.stdout.buffer.flush()
