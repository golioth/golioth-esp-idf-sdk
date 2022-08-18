# Changelog
All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Draft, 0.2.0] - TBD
### Breaking Changes
- golioth_client: The function golioth_client_create() takes different parameters.
  Existing code can migrate by placing the PSK-ID and PSK into the
  golioth_client_config_t struct.

## [0.1.0] - 2022-07-25
### Added
- Initial release, verified with esp-idf v4.4.1.
