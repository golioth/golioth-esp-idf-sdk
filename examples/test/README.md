# examples/test

Example app that runs a number of integration tests on the target hardware.

The python script will connect to the device over serial and verify
the pass/fail result of each test.

The script will return with an exit code equal to the number of
failed tests (0 means all tests passed), or -1 for general error.

```sh
idf.py build
idf.py flash -p /dev/ttyUSB0 && PYTHONUNBUFFERED=1 python verify_serial_output.py /dev/ttyUSB0
```
