# Delta Vega v2

エコマイレッジ競技車両向けの **M5Stack Tab5 車載ファームウェア**です。このリポジトリには、走行中の計時・周回判定・コース表示を行うプログラムと、その画面設計、コースデータ、テスト、運用資料をまとめています。PlatformIO / Arduinoでビルドし、EEZ Studioで設計した画面をLVGLで表示します。

## このリポジトリでできること

| 機能 | 現在の実装 |
|---|---|
| 走行の計測 | ドライバーの操作で開始し、経過時間・ラップ・目標時間を表示。GPSによる周回更新・ゴール判定、手動ラップ・確認付き手動ゴール・計測取消に対応 |
| コース表示 | コース線、スタート・周回更新・ゴール地点、GPSによる現在位置を表示。茂木の7周コースと、玉川学園前・飛田給の4周テストコースを収録 |
| 車速とGPS | 車輪のリードスイッチから速度・距離を算出。GPS入力はM5BusとPort.AをSettingsで切替可能 |
| エンジン系の操作 | 画面の電装スイッチと点火ボタンからGPIOへ指令。点火は電装ONごとに1回に制限。走行戦略からの自動点火は行わない |
| 走行戦略の表示 | 茂木ビルドではmicroSDから戦略JSONを読み、エンジン使用区間と操作地点を地図に重ねて案内 |
| 保存と通信 | 設定をNVS、計測中のサンプルとイベントをmicroSDに保存。設定済みの場合はWi-Fi経由でAWS IoT CoreへMQTT送信し、NTPで時計を同期 |

画面上の電装・点火操作は、特殊な競技車両のECUへ送る**指令**です。Tab5はエンジンの実際の稼働状態を検出しません。GPIOの配線・極性・パルス幅は実車の回路に合わせた確認が必要です。

## リポジトリの構成

| 場所 | 内容 |
|---|---|
| `src/main.cpp`、`src/adapters/` | Tab5の起動、GPIO・GPS・microSD・通信、LVGL画面との接続 |
| `lib/vega_core/` | 計時・周回・コース判定・設定・表示モデル。ArduinoやLVGLに依存しない中核 |
| `eez/`、`src/ui/` | EEZ Studioの編集元プロジェクトと生成済みLVGLコード |
| `assets/` | コースJSON・画像、茂木用の戦略例、UIアイコン |
| `scripts/`、`test/` | コース定数とUIの生成、ローカルテスト、実機支援ツール |
| `docs/` | [画面仕様](docs/ui-spec.md)、[設計](docs/architecture.md)、[運用・設定](docs/firmware-guide.md)、[正式版向け試験計画](docs/release-test-plan.md) |

## ビルドと実機への書き込み

VS CodeのPlatformIOでこのフォルダを開くか、PlatformIO CLIを使います。Tab5のUSBシリアルは115200 bpsです。

~~~sh
pio run -e esp32p4_pioarduino
pio run -e esp32p4_pioarduino -t upload
pio device monitor -b 115200
~~~

玉川学園前、飛田給、茂木の3コースを同時に組み込んでいます。Waiting画面のメニューからコースとmicroSDの走行戦略を選べます。選択は再起動後も復元されます。SettingsではTARGET・地点・周回判定をコース別に、画面輝度・GPS入力先・車両設定を端末共通で保存します。操作とアセット構成は[コースアセット](assets/README.md)を参照してください。

Wi-Fi・AWS IoT Coreを使う場合は`include/config/network_secrets.example.h`を同じ場所の`network_secrets.h`へコピーして接続先と証明書を設定します。`network_secrets.h`はGitの追跡対象外です。未設定でも画面、計測、microSD保存は動作します。通信設定とログ形式は[運用・設定ガイド](docs/firmware-guide.md)を参照してください。

## 画面・コースデータの編集とテスト

- 画面の編集元は`eez/delta-vega-v2.eez-project`です。EEZ Studioから`src/ui/`へ生成します。生成コードを直接編集せず、Tab5固有の描画やSettingsの数字キーは`src/adapters/lvgl_view.cpp`で扱います。
- コースの編集元は`assets/`内のJSONと画像です。`scripts/generate_course_data.py`で組込用のC++定数を生成します。走行戦略JSONの形式と配置は[戦略データの説明](assets/strategy/README.md)を参照してください。
- 中核のローカルテストは`python3 scripts/test_native.py`で実行できます。試験内容は[test/README.md](test/README.md)に記載しています。

## 検証状況

Tab5への書き込み、画面表示、Port.AのUnit GPS受信、玉川学園前テストコースでのGPS自動周回、microSDへの短時間記録、Wi-Fi・MQTT接続を確認しています。実車のECU・リードスイッチを使った試験、茂木の本番コースでの走行、長時間の安定性評価は今後の対象です。長時間のフィールド試験ではSD書き込み異常が一度発生しており、原因を調査中です。詳細は[運用・設定ガイド](docs/firmware-guide.md)と[試験計画](docs/release-test-plan.md)に残しています。

## 同梱アセットのライセンス

`eez/fonts/RictyDiminished-Regular.ttf`は[Ricty Diminished 4.1.1](https://rictyfonts.github.io/diminished)に含まれます。フォントと生成したフォントデータはSIL Open Font License 1.1の対象です。著作権表示とライセンス全文は[eez/fonts/OFL.txt](eez/fonts/OFL.txt)を参照してください。

電源・炎・メニューなどのアイコンはLucideを使用しています。原本と加工版の出典、著作権表示、ISC・MITライセンスは[assets/icons/lucide/README.md](assets/icons/lucide/README.md)にまとめています。フォントやアイコンを含むファームウェアを配布する際は、対応するライセンス文を添付してください。
