# -*- coding: utf-8 -*-
# 辅助脚本：复位开发板并抓取启动日志（验证 WiFi 版固件启动状态）
import serial
import time

PORT = 'COM22'
OUT = r'D:\gitee\ZJ_M1\UITouch3.5\boot_wifi_log.txt'

s = serial.Serial(PORT, 115200, timeout=1)
# 经典 esptool 复位时序：DTR/RTS 组合触发 EN 复位
s.setDTR(False)
s.setRTS(True)
time.sleep(0.1)
s.setRTS(False)
time.sleep(0.1)
s.setDTR(True)
time.sleep(8)  # 覆盖 app_main 全部初始化 + wifi 启动段
data = s.read(30000)
s.close()
with open(OUT, 'wb') as f:
    f.write(data)
print(len(data), 'bytes written to', OUT)
if b'wifi manager ready' in data or b'wifi' in data.lower():
    for line in data.decode('utf-8', 'replace').splitlines():
        low = line.lower()
        if 'wifi' in low or 'main:' in low or 'lvgl_port:' in low:
            print(line)
