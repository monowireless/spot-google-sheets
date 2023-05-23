<a href="https://mono-wireless.com/jp/index.html">
    <img src="https://mono-wireless.com/common/images/logo/logo-land.svg" alt="mono wireless logo" title="MONO WIRELESS" align="right" height="60" />
</a>

# spot-google-sheets

**Mono Wireless TWELITE SPOT Google Sheets Example**

[![MW-OSSLA](https://img.shields.io/badge/License-MW--OSSLA-e4007f)](LICENSE.md)

## 目次

- [概要](#概要)
- [使用方法](#使用方法)
- [依存関係](#依存関係)
- [ライセンス](#ライセンス)

## 概要

ESP32 をクラウドゲートウェイとして使います。

TWELITE ARIA から受信したデータを Google スプレッドシートへアップロードできます。

スケッチ解説：[Google スプレッドシートの利用 | TWELITE SPOT マニュアル](https://twelite.net/manuals/twelite-spot/example-sketches/advanced-examples/spot-google-sheets/latest.html)

## 使用方法

1. Google Sheets / Drive API を有効化したのち、これらの API を使うためのサービスアカウントを作成します
2. スケッチ内の無線 LAN 及び API 設定を変更します
3. スケッチを書き込み、実行します
4. ユーザアカウントでログインし、「共有アイテム」を開いて TWELITE SPOT が作成したファイルを探します

API のセットアップに関する詳細は [ライブラリページ](https://github.com/mobizt/ESP-Google-Sheet-Client#prerequisites) をご覧ください。

## 依存関係

### TWELITE SPOT 内の TWELITE BLUE

- ファームウェア
  - App_Wings (>= 1.3.0)

### TWELITE SPOT 内の ESP32-WROOM-32

- 環境
  - [Arduino IDE](https://github.com/arduino/Arduino) (1.x)
  - [ESP32 Arduino core](https://github.com/espressif/arduino-esp32) (>= 2.0.5)
- ライブラリ
  - [MWings](https://github.com/monowireless/mwings_arduino) (>= 1.0.0)
  - [ESP-Google-Sheet-Client](https://github.com/mobizt/ESP-Google-Sheet-Client) (>= 1.3.5)

## ライセンス

``` plain
Copyright (C) 2023 Mono Wireless Inc. All Rights Reserved.
Released under MW-OSSLA-1J,1E (MONO WIRELESS OPEN SOURCE SOFTWARE LICENSE AGREEMENT).
```
