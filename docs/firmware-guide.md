# ファームウェア運用・検証

初期実装: 2026-09-28。Ports and Adapters / MVP。実装元の要件は[implementation-goal-prompt.md](implementation-goal-prompt.md)。

## 操作

- 起動はWaiting・エンジン系電装OFF。START TIMINGで1/7、時間0から計測する。
- 有効なGPS通過またはMANUAL LAP +1で周回を進める。6回更新した7/7の状態で、別地点のゴール通過を検出するとFinishedになる。
- 取消はメニュー/SettingsのCANCEL TIMING → 確認画面のCANCEL TIMING。KEEP TIMINGで継続。取消後は新規開始可能。完走後の再計測は提供しない。
- 左下の青緑の電源トグルはエンジン系電装への指令。ON後1000 ms待って点火ボタンを有効にする。右下の炎ボタンはECUへの1000 ms HIGHパルス。電装ONごとに1回まで。
- 電装OFFは準備待ち/パルスを中止する。計測とTab5は動作継続。Tab5はエンジンの実運転状態を確認できない。
- 全体42:00・各周06:00のTARGETとスタート/周回更新/ゴールの各緯度経度をSettingsで編集できる。DONEは編集値の確定、SAVE SETTINGSは一括保存。戻る操作は未保存の編集を破棄する。
- 計測中はUIとApplicationの双方で設定変更を拒否する。全体TARGETと各周合計の一致は強制しない。大会制限39:16とは独立した値。
- Settingsは版付きNVS blob。再起動しても設定を保持する。電装状態・始動権・進行中レースの復元は行わない。

## GPIO・暫定定数

| 用途 | GPIO / 値 | 根拠・扱い |
|---|---|---|
| 車速 | 16 / FALLING / INPUT_PULLUP | M5Bus 2。リードスイッチを対GNDで接続する想定。入力回路・ノイズ耐性は実車で確認 |
| GPS UART1 | RX 7、TX 6 / 9600 bps | M5Bus 15/16。GT-502MGG-NのNMEA / 1 Hz初期設定 |
| 電装 | 45 / 初期HIGH=ON | M5Bus 8。極性はSettingsに集約し校正コマンドで変更可能 |
| 始動パルス | 48 / HIGH 1000 ms | M5Bus 22。ユーザー指定の暫定値 |
| ECU準備 | 1000 ms | ユーザー指定の暫定値 |
| 車輪 | 1.03 m、1 pulse/revolution | 旧版の定数を引き継ぐ。累積パルス差から距離を求める |
| チャタリング | 3000 µs | 最後に受理したFALLINGから3 ms未満を除外する暫定値 |
| 車速停止判定 | 3000 ms | 受理パルスがない間、最後の周期/経過時間から速度を減衰し、3秒で0とする |
| GPS鮮度 | 3000 ms | 有効なRMC受信から3秒。無効RMC、古い/順序逆転の入力を通過として扱わない |
| コース回廊 | 60 m | 指定の近似座標と既存GPSから作成したコースを考慮。実走行で校正 |
| GPS最大ステップ | 80 m | 1 Hzの隣接測位間の不連続ジャンプを除外する暫定値 |
| 周回判定 | 前方600 m以上、前ラップから60秒以上 | コースJSONの進行方向に沿う通過のみ。近傍滞在・逆走・欠損直後の誤検出を抑える |
| 重複抑制 | 10000 ms | 自動/手動を共通の更新契約で受理。同じ通過の手動補正はGPS検出器も再初期化 |
| ゴール判定 | 前方100 m以上、7/7になってから10秒以上 | 1〜6周目は無視。7/7への更新自体で完走にしない |

最初のパルスまでは車速/平均速度は欠損表示。速度の算出には2つの受理パルスが必要だが、1つだけでも停止判定時間を過ぎれば有効0になる。一度入力があった後の停止と配線断は、この入力だけでは区別できない。

リセット中やファームウェア起動前のOFFは回路側の責務。初期active HIGHなら外部プルダウン、極性変更時はそれに対応するOFF条件を設計する。ソフトウェアは設定を読んでからOFFレベルを設定し、出力ドライバを有効にする。実車側の絶縁回路・出力波形は未検証。

コースJSONから生成した定数は`src/adapters/course_data.h`。`scripts/generate_course_data.py`は座標と変換行列を埋め込むデータ変換で、画像生成ツールではない。現在位置は実緯度経度の画素変換で表示する。通過判定はコース上の投影距離を使い、地図位置は投影線へ吸着させない。方向は既存JSONのcounterclockwiseを採用した。

### Tab5専用variant

`platformio.ini`のMCU/パーティションは既存のESP32-P4設定を使用し、Arduinoのピン定義だけを`variants/m5tab5/pins_arduino.h`で置き換える。

汎用EV BoardのvariantはGPIO45をSD電源として操作し、電装出力と競合していた。Tab5ではM5UnifiedがIOエキスパンダからカード電源を供給するため、この定義を除去した。SDはGPIO43/44/39/40/41/42、LDO4、4 bit、20 MHz。Wi-Fi内部SDIOは12/13/11/10/9/8、リセット15で、リードスイッチと競合しない。

参照: [Tab5](https://docs.m5stack.com/en/core/Tab5)、[M5Stack公式SDMMC実装](https://github.com/m5stack/M5Tab5-UserDemo/blob/main/platforms/tab5/components/m5stack_tab5/m5stack_tab5.c)、[Tab5公式Wi-Fi例](https://docs.m5stack.com/en/arduino/m5tab5/wifi)、[GPS製品](https://akizukidenshi.com/catalog/g/g117980/)。M5Unified/M5GFXは検証したコミットへ固定した。

## 保存・通信

- 計測開始から取消/完走までのみ、`/vega/session-0000000001.jsonl`のような個別ファイルをmicroSDへ保存する。NVSの連番と既存ファイルの確認で再起動後も上書きを防ぐ。
- 最初の行は設定メタデータ。500 ms周期のサンプルはMQTTと共通のエンコーダから作り、開始・取消・完走・手動補正・電装操作は`type:event`で記録する。取消データは残す。
- SD Writerは別タスク。48件のキュー、約24秒分（イベント数で減少）。書込、flush/fsync、キューあふれを警告し、計測を継続する。同期は1秒周期/終了時。突然の電源断で直近の未同期データが失われる可能性がある。計測復元は未実装。
- SD未挿入時も開始・計測を継続する。自動再マウント、通信断時の後送信は初期範囲外。
- Wi-Fi/MQTT/NTPは別タスク。未設定なら通信を開始せず、オフラインで動作する。
- `network_secrets.example.h`を`network_secrets.h`へコピーし、Wi-FiとPEM形式のルートCA、クライアント証明書、秘密鍵を設定する。後者はGit除外。TLSホスト名検証を無効化しない。
- MQTT接続先は旧版のAWS IoT、トピックは`v0/delta_machine_alpha/telemetry/racing_data`、計測中500 ms、QoS 0、retainなし。接続済みの最新サンプルだけを送信する。
- 旧版の10項目は保持。`speed`、`average_speed`、`latitude`、`longitude`は未取得/無効時に`null`。旧consumerのnull対応はAWS接続時に確認する。`timestamp_ms`は64 bit起動後msでUTCではない。
- メモ初期値は旧版の大会固有文字列から`Delta Vega v2`へ変更し、機体IDとともに通信設定へ集約した。
- 時計はJST、同期先は`pool.ntp.org`（ローカル設定で変更可能）。未同期はUNSYNCED。同期後、Wi-Fi断または最終同期から2時間超ならHOLDOVER。それ以外はSYNCED。経過時間は常にesp_timerの単調時計を使う。

## シリアル診断

115200 bps。以下は明示的にコマンドを送った時だけ実行する。通常起動で模擬GPS・車速・自動始動を生成しない。

| コマンド | 動作 |
|---|---|
| `status` | 計測・指令・GPIO読み取り・GPS・パルス・SD・通信・TARGET |
| `settings` | UIで編集可能な14項目の現在値 |
| `start` / `cancel` / `lap` | UIと同じ中核へ計測コマンドを送る |
| `on` / `off` / `ignite` | 実GPIOへの指令を伴う。実車接続時は車両状態を把握して使用 |
| `log` | 最後の計測ファイルをSDから読み戻す。記録中は拒否 |
| `ui-status` / `ui-settings` / `ui-back` | 現在のUI状態確認・設定への遷移・復帰 |
| `ui-inspect-field 8` | 指定した設定欄を開き、表示文字列を出力して閉じる。値は変更しない |
| `ui-edit 0 39:16` / `ui-save` | 生成ボタン/編集イベントを通したUIテスト。項目番号0=全体、1〜7=各周、8〜13=地点緯度経度 |
| `ui-start` / `ui-cancel` / `ui-confirm-cancel` | 生成UIの開始/取消操作経路のテスト |
| `config KEY VALUE` | 校正用設定の永続保存。中核の設定検証・計測中ロックを通す |

校正キー: `power_active_high`（0/1）、`ecu_ready_ms`、`ignition_pulse_ms`、`wheel_circumference_m`、`pulses_per_revolution`、`pulse_debounce_us`、`speed_zero_ms`、`gps_stale_ms`、`course_corridor_m`、`max_gps_step_m`、`min_lap_progress_m`、`min_lap_ms`、`lap_duplicate_ms`。電装極性・ECU待ち・パルス幅の変更は電装OFF時だけ受理する。初期実装のSettings画面には校正項目を追加していない。

## 検証記録

2026-09-28、USB接続したTab5単体（microSD挿入、GPS・リードスイッチ・ECU未接続）で確認した。

| 対象 | 結果・検証範囲 |
|---|---|
| 中核のnativeテスト | C++17 / ASan / UBSanでPASS。計時、停止を含む平均速度、7周完走、取消・再開始、完走結果保持、設定ロック、電装ONごとの始動1回制限と途中OFFを確認 |
| GPS・コース | チェックサム/欠損/順序逆転/ジャンプ/逆走と自動・手動の重複を模擬入力で確認。組込の実コース座標でも6回更新後の7周目ゴールを確認 |
| EEZ生成 | EEZ Studio 0.29で生成成功、0 errors / 0 warnings。11ページとフォント元ファイル・生成物、コースJSONのソースハッシュを確認 |
| PlatformIO | ビルド・USB書込SUCCESS、転送データのハッシュ照合成功。RAM 39,892 / 512,000 bytes、Flash 2,848,889 / 6,553,600 bytes |
| 実UI操作経路 | 生成ボタン/編集イベントからTARGET編集・保存、計測開始、計測中の設定禁止、取消確認、Waitingへの復帰を確認。画面の目視は未実施 |
| 実GPIO指令 | GPIO45のON/OFF、GPIO48のパルス中HIGH/期限後LOW、2回目拒否、OFFで中断、OFF→ONで再許可を診断読取りで確認。ECUや計測器による波形検証ではない |
| 実microSD | 計測ファイルを閉じてカードから読み戻し、全行をJSONとして解析。31サンプル、設定メタデータ、開始/電装/始動/取消イベントを確認。未接続の入力はnull、取消ファイルは保持 |
| NVS・再起動 | TARGET 39:16・変更した座標・active LOWを保存して再起動し保持を確認。両極性で電装OFF起動を確認後、初期TARGET/座標/active HIGHへ戻した |
| 起動後の監視 | 最終書込後の再起動と30秒の待機で、SDマウント、電装OFF、未接続入力を確認。Guru Meditation / assertion / 意図しない再起動を観測していない |
| 通信未設定 | Wi-Fi/MQTT/NTP未設定のまま計測・UI・SDの実テストを実行。AWS実接続とNTP実同期はスキップ |

再現コマンド:

```sh
python3 scripts/test_native.py
python3 scripts/build_ui.py
pio run -e esp32p4_pioarduino -t upload
# GPS・リードスイッチ・車両回路を接続していないTab5でのみ実行する。
# pyserialが必要。実GPIOを操作し、SDに検証用セッションを残す。
python3 scripts/test_device.py --standalone-tab5 --log /tmp/vega-device.log
```

`test_device.py`はTARGETを一時変更し、正常完了時に実行前の値へ戻す。GPIO極性と地点の再起動保持は別途シリアルの`config`/`ui-edit`/`ui-save`/`settings`/`status`で確認した。現在の実機は初期設定・Waiting・電装OFFである。

ビルドでは同梱`esp-idf-size`が`--ng`を受け付けずexit code 2の警告を出す。PlatformIO自身のサイズ確認と書込は成功しており、上表の容量はその確認結果。起動初期にはM5GFXの機器検出中にI²C ACKエラーのverboseログがあるが、その後の画面初期化・SDマウント・操作テストは完了した。

SD読み戻しの終了マーカーがUSB出力で欠ける事象を検出し、データと開始/終了マーカーに送信確認を追加した。修正後の再実行で全JSON行と終了マーカーを受信できた。実GPSの測位/通過精度、リードスイッチの電気的ノイズ、ECUパルス波形、リセット中の外部回路、SDの長時間運用/実カード障害、ネットワーク実接続、画面の視認性は今後の実機評価事項。
