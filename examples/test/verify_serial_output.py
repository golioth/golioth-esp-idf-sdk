import sys
import os
import serial
import sys
from time import time
import re

def main():
    port = sys.argv[1]
    if not port:
        print('usage: {} <port>'.format(sys.argv[0]))
        sys.exit(-1)

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
