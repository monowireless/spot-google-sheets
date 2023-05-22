<a href="https://mono-wireless.com/jp/index.html">
    <img src="https://mono-wireless.com/common/images/logo/logo-land.svg" alt="mono wireless logo" title="MONO WIRELESS" align="right" height="60" />
</a>

# spot-google-sheets

**Mono Wireless TWELITE SPOT Google Sheets Example**

[![MW-OSSLA](https://img.shields.io/badge/License-MW--OSSLA-e4007f)](LICENSE.md)

[日本語版はこちら](README_J.md)

## Contents

- [About](#about)
- [Usage](#usage)
- [Dependencies](#dependencies)
- [License](#license)

## About

Set ESP32 as a cloud gateway.

You can upload data received from TWELITE ARIA to the Google Spreadsheet.

## Usage

1. Enable Google Sheets / Drive API and create a Service Account for these APIs.
2. Modify Wi-Fi / API
configs in the sketch file.
3. Upload & Run the sketch.
4. Login with your user account, open the "shared with me" page and find a file created by TWELITE SPOT.

For a API setup, see [library's prerequisites](https://github.com/mobizt/ESP-Google-Sheet-Client#prerequisites).

## Dependencies

### TWELITE BLUE on the TWELITE SPOT

- Firmware
  - App_Wings (>= 1.3.0)

### ESP32-WROOM-32 on the TWELITE SPOT

- Environment
  - [Arduino IDE](https://github.com/arduino/Arduino) (1.x)
  - [ESP32 Arduino core](https://github.com/espressif/arduino-esp32) (>= 2.0.5)
- Libraries
  - [MWings](https://github.com/monowireless/mwings_arduino) (>= 1.0.0)
  - [ESP-Google-Sheet-Client](https://github.com/mobizt/ESP-Google-Sheet-Client) (>= 1.3.5)

## License

``` plain
Copyright (C) 2023 Mono Wireless Inc. All Rights Reserved.
Released under MW-OSSLA-1J,1E (MONO WIRELESS OPEN SOURCE SOFTWARE LICENSE AGREEMENT).
```
