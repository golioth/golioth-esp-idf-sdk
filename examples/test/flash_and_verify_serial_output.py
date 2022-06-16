import sys
import os
import serial
import sys
from time import time
import re
import json
import subprocess

def flash_firmware(port):
    with open('build/flasher_args.json', 'r') as f:
        flasher_args = json.load(f)

    esptool_command = [
        "esptool.py",
        "--port", port,
        "--baud", "460800",
        "--before", "default_reset",
        "--after", "hard_reset",
        "write_flash",
    ]

    for arg in flasher_args['write_flash_args']:
        esptool_command.append(arg)

    for offset, file in flasher_args['flash_files'].items():
        esptool_command.append(offset)
        esptool_command.append("build/{}".format(file))

    process = subprocess.run(
            esptool_command,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            universal_newlines=True)
    print(process.stdout)
    if process.returncode != 0:
        raise RuntimeError('esptool command failed')

def main():
    port = sys.argv[1]
    if not port:
        print('usage: {} <port>'.format(sys.argv[0]))
        sys.exit(-1)

    flash_firmware(port)

    ser = serial.Serial(port, 115200, timeout=1)

    unity_test_end_re = '\.c:\d+:test_.*:(PASS|FAIL)'
    unity_done_re = '^\d+ Tests (\d+) Failures \d+ Ignored'
    num_test_failures = 0
    test_results = []

    start_time = time()
    timeout_s = 60
    while True:
        line = ser.readline().decode('utf-8', errors='replace').replace("\r\n", "")

        if line != "":
            print(line)

        if "CPU halted" in line:
            sys.exit(-1)

        if time() - start_time > timeout_s:
            print("Timeout, tests never completed")
            sys.exit(-1)

        unity_done = re.search(unity_done_re, line)
        if unity_done:
            num_test_failures = int(unity_done.groups()[0])
            print('================ END OF DEVICE OUTPUT ================')
            for tr in test_results:
                print(tr)
            sys.exit(num_test_failures)

        if re.search(unity_test_end_re, line):
            test_results.append(line)

if __name__ == "__main__":
    main()
