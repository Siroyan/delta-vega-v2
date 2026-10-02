# ファームウェア運用・検証

初期実装: 2026-09-28。Ports and Adapters / MVP。実装元の要件は[implementation-goal-prompt.md](implementation-goal-prompt.md)。

## 操作

- 起動はWaiting・エンジン系電装OFF。START TIMINGで1周目、時間0から計測する。茂木は全7周、玉川学園前・飛田給のテストコースは全4周。
- 有効なGPS通過またはMANUAL LAP +1で周回を進める。最終周（茂木7/7、テストコース4/4）で別地点のゴール通過を検出するとFinishedになる。最終周では手動ラップボタンが赤い`GOAL / FINISH`に変わり、GPSがゴールを取り逃した場合は確認画面の`FINISH TIMING`で現在時刻の結果を確定できる。`KEEP TIMING`では計測を続ける。手動完走はSD記録に`manual_finish`イベントとして残る。
- GPSの短い途切れでは周回進捗を保持するが、途切れ中に計測線を越えたと推定して周回を進めることはしない。GPS自動周回の直後は、次周の進捗が十分になるまで手動ラップを無効にする。GPSを失った場合は手動ラップ補正を利用できる。手動ゴールは確認画面を通せば、直前の周回更新からの待ち時間なしに確定する。
- 取消はメニュー/SettingsのCANCEL TIMING → 確認画面のCANCEL TIMING。KEEP TIMINGで継続。取消後は新規開始可能。完走後の再計測は提供しない。
- 左下の青緑の電源トグルはエンジン系電装への指令。ON後、設定されたECU準備時間（初期1000 ms）は右下の点火ボタンが無効時と同じ薄い赤から時計回りに通常の赤へ変わり、操作できない。準備完了時に全面が通常の赤となり、炎ボタンを操作できる。炎ボタンはECUへの1000 ms HIGHパルス。電装ONごとに1回まで。
- 電装OFFは準備待ち/パルスを中止する。計測とTab5は動作継続。Tab5はエンジンの実運転状態を確認できない。
- 全体42:00・各周06:00のTARGETとスタート/周回更新/ゴールの各緯度経度をSettingsで編集できる。Settingsの`DISPLAY BRIGHTNESS`は25～100%相当（内部値64～255）をスライダーで調整し、操作中は画面へ反映する。初期値は従来と同じ127。`ADVANCED SETTINGS`から車輪・GPS・周回判定・ECU関連の13項目とGPS接続先を変更できる。数値画面のSETは編集値の確定、SAVE SETTINGSは一括保存。Advancedの戻る矢印はSettingsへ戻り、Settingsの戻る矢印は未保存の編集と輝度プレビューを破棄する。
- 電装極性（POWER HIGH）は`1`でHIGH=ON、`0`でLOW=ON。電装ON中は極性・ECU準備時間・点火パルス幅の編集を無効にし、アプリケーション側も変更を拒否する。これらの値は実車接続前に回路とECUの仕様に合わせて確認する。
- 計測中はUIとApplicationの双方で設定変更を拒否する。全体TARGETと各周合計の一致は強制しない。大会制限39:16とは独立した値。
- Settingsは版付きNVS blob（現行v3）。v1を読み込むとGPS接続先はM5Bus、v1/v2を読み込むと明るさは127となり、既存のTARGET・座標・車両設定とv2のGPS接続先は維持される。次回保存時にv3へ移行する。保存した明るさは起動後に適用し、再起動しても保持する。電装状態・始動権・進行中レースの復元は行わない。
- GPS接続先はAdvanced Settingsの`GPS INPUT`で`M5BUS`または`PORT.A`を選び、`SAVE SETTINGS`で確定する。切替は計測前または計測取消後のみ可能。保存成功時にUART、NMEAパーサ、直前のGPS位置・受信状態を初期化して選択先から受信し直す。Port.AのAT6558 Unit GPSは最初のRMC受信後、[CASICプロトコル](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/950/Multimode_satellite_navigation_receiver_cn.pdf)のコマンドで測位を5 Hz（200 ms）、RMCを毎回、GGAを5回に1回へ変更する。設定はGPS内のFLASHに保存せず、UARTを開き直すたびに送信する。M5BusのGT-502MGG-Nにはこのコマンドを送らない。GPSの測位確認には屋外の開けた場所で待つ。シリアルの`status`で`gps_source`・`gps_bytes`（受信バイト数）・`gps_rmc`（チェックサムが正しいRMC文の件数）・`gps_rmc_hz`（直近5秒間のRMC受信頻度）・`gps`（新鮮な測位の有無）を確認できる。
- 2026-10-01、Port.AのUnit GPSを接続してTab5を再起動したところ、起動ログに5 Hzの設定要求が出て、`gps_rmc_hz`は4.9～5.0を示した。この確認時は屋内で未FIXだったため、5 Hzの位置更新や実走での周回判定は未検証。
- コース地図にはSettingsで保存したスタート地点を青緑の点、ゴール地点を赤の点、周回更新地点をコースに垂直な紫の線として表示する。`START`・`GOAL`・`LAP`の文字は地図下端の凡例にまとめる。GPS未接続でも表示し、座標変更を保存すると対応する点・線が移動する。周回更新線は通過判定用の基準経路に投影した位置へ置き、回廊外や画像範囲外の場合は非表示にする。

## GPIO・暫定定数

| 用途 | GPIO / 値 | 根拠・扱い |
|---|---|---|
| 車速 | 16 / FALLING / INPUT_PULLUP | M5Bus 2。リードスイッチを対GNDで接続する想定。入力回路・ノイズ耐性は実車で確認 |
| GPS UART1 / M5Bus | RX 7、TX 6 / 9600 bps | 初期選択。M5Bus 15/16。GT-502MGG-NのNMEA / 1 Hz初期設定 |
| GPS UART1 / Port.A | RX 54、TX 53 / 9600 bps | AT6558 Unit GPSの白線TXをG54、黄線RXをG53へ接続。Port.Aの5 V給電を使用。受信開始後にRMC 5 Hz / GGA 1 Hzを要求 |
| 電装 | 45 / 初期HIGH=ON | M5Bus 8。極性はAdvanced Settingsまたは校正コマンドで変更可能 |
| 始動パルス | 48 / HIGH 1000 ms | M5Bus 22。ユーザー指定の暫定値 |
| ECU準備 | 1000 ms | ユーザー指定の暫定値 |
| 車輪 | 1.03 m、1 pulse/revolution | 旧版の定数を引き継ぐ。累積パルス差から距離を求める |
| チャタリング | 3000 µs | 最後に受理したFALLINGから3 ms未満を除外する暫定値 |
| 車速停止判定 | 3000 ms | 受理パルスがない間、最後の周期/経過時間から速度を減衰し、3秒で0とする |
| GPS鮮度 | 3000 ms | 有効なRMC受信から3秒。無効RMC、古い/順序逆転の入力を通過として扱わない |
| コース回廊 | 60 m | 指定の近似座標と既存GPSから作成したコースを考慮。実走行で校正 |
| GPS最大ステップ | 80 m | 隣接測位間の不連続ジャンプを除外する暫定値。5 Hzでの実走に合わせて校正が必要 |
| 周回判定 | 前方600 m以上、前ラップから60秒以上 | コースJSONの進行方向に沿う通過のみ。近傍滞在・逆走・欠損直後の誤検出を抑える |
| 重複抑制 | 10000 ms | 自動/手動を共通の更新契約で受理。同じ通過の手動補正はGPS検出器も再初期化 |
| ゴール判定 | 前方100 m以上、最終周になってから10秒以上 | 最終周より前は無視。最終周への更新自体で完走にしない |

最初のパルスまでは車速/平均速度は欠損表示。速度の算出には2つの受理パルスが必要だが、1つだけでも停止判定時間を過ぎれば有効0になる。一度入力があった後の停止と配線断は、この入力だけでは区別できない。

リセット中やファームウェア起動前のOFFは回路側の責務。初期active HIGHなら外部プルダウン、極性変更時はそれに対応するOFF条件を設計する。ソフトウェアは設定を読んでからOFFレベルを設定し、出力ドライバを有効にする。実車側の絶縁回路・出力波形は未検証。

3コースは同時にファームウェアへ収録する。Waiting画面のメニューから玉川学園前・飛田給・茂木を選び、画像、地点、周回判定を切り替える。コース固有のデータ・初期値・画像・NVS領域は[コースアセット](../assets/README.md)で管理する。現在位置は緯度経度を画像へ変換して表示し、投影線へ吸着させない。茂木コースには進入路・周回路・ゴール分岐がある。周回更新は選択したコースのSettingsにある座標を周回路へ投影して判定する。合流・分岐と地点座標は近似値であり、実走評価で校正する。

### Tab5専用variant

`platformio.ini`のMCU/パーティションは既存のESP32-P4設定を使用し、Arduinoのピン定義だけを`variants/m5tab5/pins_arduino.h`で置き換える。

汎用EV BoardのvariantはGPIO45をSD電源として操作し、電装出力と競合していた。Tab5ではM5UnifiedがIOエキスパンダからカード電源を供給するため、この定義を除去した。SDはGPIO43/44/39/40/41/42、LDO4、4 bit、20 MHz。Wi-Fi内部SDIOは12/13/11/10/9/8、リセット15で、リードスイッチと競合しない。

参照: [Tab5](https://docs.m5stack.com/en/core/Tab5)、[M5Stack公式SDMMC実装](https://github.com/m5stack/M5Tab5-UserDemo/blob/main/platforms/tab5/components/m5stack_tab5/m5stack_tab5.c)、[Tab5公式Wi-Fi例](https://docs.m5stack.com/en/arduino/m5tab5/wifi)、[GPS製品](https://akizukidenshi.com/catalog/g/g117980/)。M5Unified/M5GFXは検証したコミットへ固定した。

## 保存・通信

現在のビルドは`platformio.ini`の`VEGA_ENABLE_MQTT=1`でAWS/MQTT送信を有効にしている。起動時にmbedTLSの動的メモリ確保先をPSRAMへ変更し、TLS接続中もESP-Hosted Wi-Fiが使う内部DMAメモリを確保する。Wi-FiがIPを取得した後にMQTTを開始し、Wi-Fi切断時は停止、復旧時に再開する。USB給電の実機でMQTT接続とNTP同期、約6秒の計測と取消、AWS IoT Coreでの新着JSON受信を確認した。GPSを外したバッテリー駆動では、ボタンに触れない10分間で水色画面・再起動が起きなかった。正式版の長時間エージングは[試験計画](release-test-plan.md)に従って別途行う。

2026-10-01の切り分けでは、GPSを外したバッテリー駆動中にMQTT接続試行を有効にすると、内部DMA用の最大連続空き領域が約1.5 KBまで減り、保存されたクラッシュ情報に`transport_drv_sta_tx ... (copy_buff)`が残った。MQTTだけを停止し、ボタンに触れずに再試験すると、同じバッテリー条件で10分35秒連続稼働し、内部DMA用の最大連続空き領域は約67 KBを維持した。手動Resetでもリセット理由`7`が記録されるため、この番号単独では自動再起動と判定しない。関連する[Espressifの報告](https://github.com/espressif/esp-hosted-mcu/issues/144)がある。現在の対策はTLSのメモリ確保先を変更して送信バッファ用の内部DMAメモリを残すもの。TLSが扱う秘密鍵もPSRAMに置かれるため、実車投入前にメモリ保護要件を確認する。

- 走行戦略はWaiting画面からmicroSDの`/vega/strategies/*.json`または互換ファイル`/vega/strategy.json`を選び、選択したコースの周回数・経路ID・ON/OFF地点を検証する。Waitingで1周目をプレビューし、Mainでは現在周回の橙色のエンジン使用区間、青色の惰性区間、点火/OFF地点を地図へ重ねる。GPSが有効なら次の地点までの距離を表示する。形式とダミーは[走行戦略データREADME](../assets/strategy/README.md)。プランは表示専用で、GPIO出力を変更しない。
- 計測開始から取消/完走までのみ、`/vega/session-0000000001.jsonl`のような個別ファイルをmicroSDへ保存する。NVSの連番と既存ファイルの確認で再起動後も上書きを防ぐ。
- 最初の行はコースID・周回数と設定メタデータ（`schema_version:3`）。500 ms周期のサンプルはMQTTの10項目に加え、`gps_seen`、`gps_fix_valid`、`gps_fresh`、`gps_speed_kmh`、`gps_satellites`、`gps_hdop`、`gps_gga_fix_quality`、`gps_utc_ms_of_day`、`gps_age_ms`、`gps_quality_age_ms`を記録する。GGA未受信などで不明な数値は`null`。`gps_age_ms`は最後のRMC受信から、`gps_quality_age_ms`は最後のGGA受信からの経過時間。`gps_utc_ms_of_day`はRMCのUTC時刻を午前0時からのミリ秒で表す。開始・取消・完走・手動補正・電装操作は`type:event`で記録し、取消データも残す。
- GPSのNMEA原文と画面上のアイコン座標を記録する一時的な診断機能は撤去した。位置、速度、衛星数、HDOPなどは500 ms周期の通常サンプルに残る。
- USB接続時にTab5が再起動しても、シリアルの`log`でNVSの記録番号を使って最新のSDセッションを読み出せる。`log-read 番号`で古いセッションも指定できる。
- SD Writerは別タスク。48件のキューを使い、500 msサンプルだけなら約24秒分（イベント数で減少）。書込、flush/fsync、キューあふれを警告し、計測を継続する。同期はセッション開始時、1秒周期、終了時。エラー表示は次のセッションのファイルを正常に開き、メタデータを同期できた場合に解除する。突然の電源断で直近の未同期データが失われる可能性がある。計測復元は未実装。
- SD未挿入時も開始・計測を継続する。開始時にマウントできなかった場合は次のセッション開始時に再試行する。通信断時の後送信は初期範囲外。
- Wi-Fi/MQTT/NTPは別タスク。未設定なら通信を開始せず、オフラインで動作する。
- `network_secrets.example.h`を`network_secrets.h`へコピーし、Wi-FiとAWS IoTの接続先を設定する。Amazon Root CA 1は設定例に含まれる。クライアント証明書と秘密鍵はPEM全文を各`R"EOF(...)EOF"`文字列に貼り付ける。`network_secrets.h`はGit除外。TLSホスト名検証を無効化しない。
- MQTT接続先は旧版のAWS IoT、トピックは`v0/delta_machine_alpha/telemetry/racing_data`、計測中500 ms、QoS 0、retainなし。接続済みの最新サンプルだけを送信する。
- 旧版の10項目は保持。`speed`、`average_speed`、`latitude`、`longitude`は未取得/無効時に`null`。旧consumerのnull対応はAWS接続時に確認する。`timestamp_ms`は64 bit起動後msでUTCではない。
- メモ初期値は旧版の大会固有文字列から`Delta Vega v2`へ変更し、機体IDとともに通信設定へ集約した。
- 時計はJST、同期先は`pool.ntp.org`（ローカル設定で変更可能）。未同期はUNSYNCED。同期後、Wi-Fi断または最終同期から2時間超ならHOLDOVER。それ以外はSYNCED。経過時間は常にesp_timerの単調時計を使う。

## GPS移動診断の手順

1. Port.AのUnit GPSを空が見える位置に置き、Waiting画面の`GPS FIX`を確認する。安全に停止した状態で計測を開始する。
2. 約20秒静止し、一方向へ約50 mを一定のペースで歩き、再び約20秒静止する。歩行の開始・停止時刻を控える。歩行中は画面を注視せず、終了後に計測を取消す。
3. USBを接続し、115200 bpsのシリアルで`log`を送って最新セッションを読み出す。USB接続でTab5が再起動しても、NVSの連番から最新ファイルを探す。公道の生位置を含むのでログは公開リポジトリへ追加しない。
4. 通常サンプルの緯度経度、速度、衛星数、HDOPを時刻順に確認する。通常サンプル上でも位置が止まる場合はGPS受信・解析側を調べる。ログ上の位置が動いているのにアイコンが止まる場合は表示側を調べる。

一時的な診断版では、約6秒の屋内計測でNMEA原文36行、アイコン座標12行、通常サンプル13行をmicroSDから読み戻した。この詳細記録は現在のファームウェアでは生成しない。USB接続による再起動後も`log`で最新ファイルを読み戻せた。起動後のシリアルではGPSのRMC受信5.0 Hz、SD正常、Wi-Fi/MQTT接続を確認した。

2026-10-02の屋外試験では、再起動前のセッション48のNMEA原文が実位置から約200 m離れた地点を示し、約48秒間ほぼ停止していた。再起動後のセッション49は「静止20秒→歩行→静止20秒」。歩行中の約60秒間にNMEAの速度は0、座標変化は約7 mで、最後の静止20秒には約15 m変化した。画面モデルとLVGLマーカーの座標は受信した位置に追従しており、表示処理より前の受信機出力に遅れがある。実際の画面上での移動は未確認。

Port.AのUnit GPSにCASIC `CFG-NAVX`を問い合わせると、動作モードは`0`（Portable）、静止判定しきい値は`0.000 m/s`だった。したがって「高すぎる静止しきい値」は確認されなかった。`fix_mode`欄は仕様書の値1〜3に該当しない`0x30`で、意味は未確認。比較試験のため、設定をFLASHへ保存せず動作モードだけ`2`（Walking）へ切り替え、ACKと再読出しで反映を確認した。

Walking modeで玉川学園コースを歩いたセッション50は約10分45秒の記録中、通常サンプル1276件のうちGPS速度0は1件だけで、前回のような長い座標停止は見られなかった。記録上の軌跡長は約764 mで、計測開始から約617.6秒で`gps_lap`イベントが発生し、画面もLAP 2へ進んだ。受信機の動作モード以外に現場条件も変わるため、改善の原因をこの1回だけで断定はしない。歩行テストを再現しやすくするため、テストコース用ビルドのPort.A入力ではUART開始後にWalking modeを自動で要求する。茂木用ビルドでは自動変更しない。受信機のFLASHには保存しない。

セッション50のSDファイルは約644.5秒時点のサンプルで終わり、ユーザーが取消したにもかかわらず終了イベントが残らなかった。実機の`sd_error=1`と一致する。読出しは成功しており、取消後の短いセッション51・52では終了イベントと`sd_error=0`を確認したため、長時間運転またはバッテリー歩行中の一時的な書込み異常として継続調査する。原因は未確定。次回の異常を特定するため、書込み失敗時の種類と件数を`status`およびシリアルに出すようにした。

## シリアル診断

115200 bps。以下は明示的にコマンドを送った時だけ実行する。通常起動で模擬GPS・車速・自動始動を生成しない。

| コマンド | 動作 |
|---|---|
| `status` | 計測・指令・GPIO読み取り・GPS・パルス・SD・通信・TARGET。`sd_last_failure`は最後のSD異常種類、`sd_record_lost`は記録の欠落件数 |
| `settings` | UIで編集可能な27項目の現在値 |
| `plan-status` | 走行戦略の読込状態と採用したID |
| `plan-upload BYTES` | 4096バイト以内のJSONを後続の生バイトで受け取り、SDに`/vega/strategy.json`がない場合だけ検証・保存。通常は`scripts/upload_strategy.py`から実行 |
| `start` / `cancel` / `lap` | UIと同じ中核へ計測コマンドを送る |
| `on` / `off` / `ignite` | 実GPIOへの指令を伴う。実車接続時は車両状態を把握して使用 |
| `log` | 最後の計測ファイルをSDから読み戻す。記録中は拒否 |
| `log-read NUMBER` | 指定したセッション番号の計測ファイルをSDから読み戻す。記録中は拒否。SD上のファイルは変更しない |
| `gps-nav` | Port.AのUnit GPSからCASICの動作モード・静止判定しきい値を読み出す。Waiting時のみ |
| `gps-walk` | Port.AのUnit GPSを試験用Walking modeへ一時変更し、ACK後に再読出しする。FLASHには保存しない。Waiting時のみ |
| `ui-status` / `ui-settings` / `ui-back` | 現在のUI状態確認・設定への遷移・復帰 |
| `ui-course-markers` | メイン画面のスタート・ゴールマーカーと周回更新線の画像内座標・表示状態 |
| `ui-plan` | Main上の戦略線・地点マーカー・次の操作案内の状態 |
| `ui-advanced` / `ui-advanced-back` | Advanced Settingsを開く/Settingsへ戻る |
| `ui-inspect-field 8` | 指定した設定欄を開き、表示文字列を出力して閉じる。値は変更しない |
| `ui-edit 0 39:16` / `ui-save` | 生成ボタン/編集イベントを通したUIテスト。項目番号0=全体、1〜7=各周、8〜13=地点緯度経度、14〜26=Advanced Settings |
| `ui-start` / `ui-cancel` / `ui-confirm-cancel` | 生成UIの開始/取消操作経路のテスト |
| `config KEY VALUE` | 校正用設定の永続保存。中核の設定検証・計測中ロックを通す |

校正キー: `power_active_high`（0/1）、`ecu_ready_ms`、`ignition_pulse_ms`、`wheel_circumference_m`、`pulses_per_revolution`、`pulse_debounce_us`、`speed_zero_ms`、`gps_stale_ms`、`course_corridor_m`、`max_gps_step_m`、`min_lap_progress_m`、`min_lap_ms`、`lap_duplicate_ms`。すべてAdvanced Settingsから編集できる。電装極性・ECU待ち・パルス幅の変更は電装OFF時だけ受理する。NVSの設定形式バージョンとSDログ連番は内部管理値で、Settingsの編集項目ではない。

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

2026-09-29、進入路・周回路・ゴール分岐を含むschema 2のコースへ更新した。nativeテストでは1周目の進入、2〜6周目の更新、7周目のゴール分岐、周回路を走り続けた場合の誤完走防止、Settingsで周回更新地点またはゴールを動かした場合を模擬測位で確認した。EEZビルドは0 errors / 0 warnings、PlatformIOビルドはSUCCESS。Tab5単体（車両回路・GPS・リードスイッチ未接続）へUSBシリアルで書き込み、転送データのハッシュ照合に成功した。起動後の`status`はWaiting・電装OFF・SD ready、`ui-course-markers`はWaiting画面のSTART `(216,380)`、GOAL `(106,181)`、周回更新線表示を示した。利用者が実機画面で進入路とゴール分岐の両方を確認した。実GPSによる合流・分岐・通過の精度は未検証。

2026-09-29、7周のダミー戦略を`/vega/strategy.json`へシリアル転送してmicroSDに保存した。再書込・再起動後も`plan-status`が`loaded=1 id=dummy-race-001`を報告。Mainで`ui-plan`は`PLAN DEMO`、橙・青の線とON/OFFマーカーを表示状態と報告した。GPSなしで手動ラップを1〜7周まで進め、各周でマーカー座標が変わることを確認し、最後に取消してWaiting・電装OFFへ戻した。nativeテストは実コースとサンプルの全7周、欠周・重複・経路不一致・範囲外データの拒否、次地点までの距離と表示モデルを検証した。初回の戦略描画は経路点への繰り返し投影をやめ、実機の`perf`でView最大437 msから22 msへ短縮した。同じ場面のLVGL描画最大は約294 ms。実GPSによる案内距離、画面の目視、車両での戦略妥当性は未検証。

待機画面の1周目プレビューを追加したファームウェアも書き込み済み。書き込み直後は画面とUSBシリアルが応答しなかったが、電源ボタンで再起動すると復帰した。復帰後の`plan-status`は読込完了、`ui-plan`はWaitingで1周目の橙・青の線とON/OFFマーカーを表示状態と報告した。`ui-start`後のMainでも同じ描画と`PLAN DEMO`を確認し、計測取消後はWaiting・電装OFFに戻った。再実行したnativeテストとPlatformIOビルドは成功。書き込み後の自動再起動が確実かは別途確認する。
