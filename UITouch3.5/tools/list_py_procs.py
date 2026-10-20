# -*- coding: utf-8 -*-
# 列举 python 进程命令行，找出占用 COM22 的 device monitor
import subprocess
out = subprocess.check_output(
    ['powershell', '-NoProfile', '-Command',
     "Get-CimInstance Win32_Process -Filter \"Name='python.exe'\" "
     "| Select-Object ProcessId,CommandLine | ConvertTo-Json"],
    text=True)
print(out)
