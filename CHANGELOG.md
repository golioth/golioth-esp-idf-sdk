# Changelog
All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.2.0] - 2022-08-26
### Breaking Changes
- golioth_client: The function `golioth_client_create()` takes different parameters.
  Existing code can migrate by placing the PSK-ID and PSK into the
  golioth_client_config_t struct. See `examples/golioth_basics` for an example.
### Added
- golioth_coap_client: Support for PKI certificate authentication over DTLS
- ble.c: new file, BLE GATT server for provisioning WiFi and Golioth credentials
- New `certificate_auth` example
- docs: Contributing.md, style guide
### Changed
- golioth_basics: custom partition table to accommodate app size over 1 MB
- Kconfig: RPC and Settings feature flags enabled by default
- golioth_coap_client: More useful error message when DTLS handshake fails
- magtag_demo: add support for shell and NVS credentials
- docs: Improved API docs in Doxygen, README landing page
### Fixed
- magtag_demo: support for USB CDC, now serial I/O works
- test: consistent timeouts to prevent flaky CI

## [0.1.0] - 2022-07-25
### Added
- Initial release, verified with esp-idf v4.4.1.
