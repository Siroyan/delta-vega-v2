# M5Stack Tab5 ファームウェア

PlatformIO / Arduino を使った Tab5 のファームウェアです。M5Unified / M5GFX を介して、
画面とタッチ入力を LVGL 9.2.2 に接続しています。

## ビルドと書き込み

VS Code の PlatformIO でこのフォルダを開くか、次のコマンドを実行します。

```sh
pio run -e esp32p4_pioarduino
pio run -e esp32p4_pioarduino -t upload
pio device monitor -b 115200
```

起動時は開始待ち画面・エンジン系電装OFFです。`START TIMING`で7周の計測を開始します。
GPS未接続時は自動ラップを行わず、`MANUAL LAP +1`で周回を進められます。
全体TARGETは42:00、各周は06:00が初期値です。ハンバーガーメニューのSettingsから
TARGETと3地点を編集・保存できます。計測中は編集を禁止し、確認付きの計測取消が可能です。

## 構成と検証

- [画面仕様](docs/ui-spec.md)
- [アーキテクチャ](docs/architecture.md)
- [設定・GPIO・保存形式・検証手順](docs/firmware-guide.md)

```sh
python3 scripts/test_native.py       # C++17 host tests, ASan / UBSan
python3 scripts/generate_course_data.py
python3 scripts/build_ui.py         # macOS; --studioでEEZ Studioの実行ファイルを指定可能
```

`lib/vega_core`はArduino/LVGLに依存しない計測・制御・MVPの中核です。
EEZ Studioでは`eez/delta-vega-v2.eez-project`を編集し、LVGL 9.2.2 / 1280×720 /
EEZ Flowなしで`../src/ui`へ生成します。生成物は直接編集しません。
`build_ui.py`はEEZ Studio 0.29 CLIが未変更の埋込フォントを削除する問題を回避します。
コースJSONの組込定数は`generate_course_data.py`で生成します。

## Wi-Fi / AWS / NTP

`include/config/network_secrets.example.h`を`network_secrets.h`へコピーし、
Wi-Fi情報とAWS IoTの証明書をローカルで設定して再ビルドしてください。
`network_secrets.h`はGitの追跡対象外です。未設定でも計測・表示・SD保存は動作します。
AWSへの実接続とNTP同期の実検証は今回スキップしています。

## フォントのライセンス

`eez/fonts/RictyDiminished-Regular.ttf` は [Ricty Diminished 4.1.1](https://rictyfonts.github.io/diminished)
に含まれるフォントです。著作権表示と SIL Open Font License 1.1 の全文は
[eez/fonts/OFL.txt](eez/fonts/OFL.txt) に保存しています。

EEZ Studio でこのフォントから生成したフォントデータも同じライセンスの対象です。
ファームウェアや生成したフォントデータを配布するときは、著作権表示とライセンス文を
配布物に添付してください。

## UIアイコンのライセンス

電源・炎・メニュー・閉じる・戻る・設定には[Lucide](https://lucide.dev/)のアイコンを使用しています。
原本SVG、色・配置を調整したSVGとPNG、取得元のコミットは
[assets/icons/lucide/README.md](assets/icons/lucide/README.md)に保存しています。

ISC LicenseとFeather由来アイコンのMIT Licenseの全文・著作権表示は
[assets/icons/lucide/LICENSE](assets/icons/lucide/LICENSE)を参照してください。
これらのアセットや生成画像を含むファームウェアを配布する際は、ライセンス文も添付してください。
