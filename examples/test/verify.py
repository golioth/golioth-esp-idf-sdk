import sys
import os
import serial
from time import time
import re
import json

def wait_for_str_in_line(ser, str, timeout_s=10):
    start_time = time()
    while True:
        line = ser.readline().decode('utf-8', errors='replace').replace("\r\n", "")
        if line != "":
            print(line)
        if "CPU halted" in line:
            raise RuntimeError(line)
        if time() - start_time > timeout_s:
            raise RuntimeError('Timeout')
        if str in line:
            return line

def set_credentials(ser):
    with open('credentials.json', 'r') as f:
        settings = json.load(f)
    ser.write('\r\n'.encode())
    wait_for_str_in_line(ser, 'esp32>')
    ser.write('settings set wifi/ssid {}\r\n'.format(settings['wifi/ssid']).encode())
    wait_for_str_in_line(ser, 'esp32>')
    ser.write('settings set wifi/psk {}\r\n'.format(settings['wifi/psk']).encode())
    wait_for_str_in_line(ser, 'esp32>')
    ser.write('settings set golioth/psk-id {}\r\n'.format(settings['golioth/psk-id']).encode())
    wait_for_str_in_line(ser, 'esp32>')
    ser.write('settings set golioth/psk {}\r\n'.format(settings['golioth/psk']).encode())
    wait_for_str_in_line(ser, 'esp32>')

def reset(ser):
    ser.write('\r\n'.encode())
    wait_for_str_in_line(ser, 'esp32>')
    ser.write('reset\r\n'.encode())
    # Wait for string that prints on next boot
    wait_for_str_in_line(ser, 'heap_init')
    wait_for_str_in_line(ser, 'esp32>')

def run_built_in_tests(ser):
    unity_test_end_re = '\.c:\d+:test_.*:(PASS|FAIL)'
    unity_done_re = '^\d+ Tests (\d+) Failures \d+ Ignored'
    num_test_failures = 0
    test_results = []

    ser.write('\r\n'.encode())
    wait_for_str_in_line(ser, 'esp32>')
    ser.write('built_in_test\r\n'.encode())

    start_time = time()
    timeout_s = 60
    while True:
        line = ser.readline().decode('utf-8', errors='replace').replace("\r\n", "")
        if line != "":
            print(line)
        if "CPU halted" in line:
            raise RuntimeError(line)
        if time() - start_time > timeout_s:
            raise RuntimeError('Timeout')

        unity_done = re.search(unity_done_re, line)
        if unity_done:
            num_test_failures = int(unity_done.groups()[0])
            print('================ END OF DEVICE OUTPUT ================')
            for tr in test_results:
                print(tr)
            return num_test_failures

        if re.search(unity_test_end_re, line):
            test_results.append(line)

def main():
    if len(sys.argv) != 2:
        print('usage: {} <port>'.format(sys.argv[0]))
        sys.exit(-1)
    port = sys.argv[1]

    # Connect to the device over serial and use the shell CLI to interact and run tests
    ser = serial.Serial(port, 115200, timeout=1)

    # Set WiFi and Golioth credentials over device shell CLI
    set_credentials(ser)
    reset(ser)

    # Run built in tests on the device and check output
    num_test_failures = run_built_in_tests(ser)
    sys.exit(num_test_failures)

if __name__ == "__main__":
    main()
