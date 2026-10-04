# AtomS3 GPS・車速シミュレーター

Tab5の長時間試験用対向機です。`assets/course_manifest.json` に登録された各 `assets/<コース>/course.json` をAtomS3のLittleFSへ配置し、起動時に**Tab5と同じJSON**を読み込みます。生成されたC++のコース配列は使いません。同じ走行距離からGPSのNMEA文と車輪パルスを生成します。Tab5本体のファームウェアは変更しません。

AtomS3の画面には、コース名、走行状態、現在の周回、設定速度、走行距離、送出した車速パルス数、GPSの異常注入状態、送出遅れのパルス数を表示します。画面下端の細いバーは現在のルートの進行率です。`READY`、`RUN`、`PAUSE`、`GOAL` は本体ボタンまたはUSBシリアルからの走行操作と連動します。`MISS` が0以外ならパルス送出が間に合っていないため、試験結果を確認してください。

## 接続

ブラウザから試験を管理する場合は**Tab5とAtomS3をそれぞれPCのUSBにつなぎ**、[race-emulator](../race-emulator/README.md)で両方のシリアルを監視します。Tab5だけが再起動してもAtomS3の走行は続き、両機のログを同じPC時刻で記録できます。単体運用ではAtomS3を独立したUSBモバイルバッテリーで給電することもできます。AtomS3をTab5のPort.Aの5Vから給電するとTab5の電源変動が対向機にも波及するため、この試験では使用しません。両機の**GNDを共通化**します。AtomS3のPort.CUSTOMの**白（G1、UART TX）**をTab5のPort.Aの**白（G54、UART RX）**へ接続します。Tab5の設定で `GPS INPUT = PORT.A` を選びます。必要ならAtomS3の黄（G2、UART RX）とTab5 Port.Aの黄（G53、UART TX）も接続できます。GPS出力は9600 bps、8N1、RMC 5 Hz、GGA 1 Hzです。

車速用にはAtomS3底面の **G5** をTab5 M5Busの **G16** に直結します。G5はオープンドレイン出力で、パルス中だけGNDへ引き下げ、通常は解放します。両機器の5V線は接続しません。AtomS3のG5が3.3Vロジックであり、Tab5側G16が現行ファームのプルアップ入力である前提です。実車のECU・リレー・実GPSを同時に接続しないでください。異なる電圧の車両回路を接続する場合は、この直結方式を使わず絶縁してください。

| AtomS3 | Tab5 | 信号 |
| --- | --- | --- |
| Port.CUSTOM 白 G1 | Port.A 白 G54 | NMEA |
| 底面 G5 | M5Bus G16 | 車速パルス |
| GND | GND | 共通基準 |
| Port.CUSTOM 黄 G2 | Port.A 黄 G53 | 任意、UART受信 |

### Tab5と接続するスモークテスト

1. 両機の電源を切って配線する。AtomS3 Port.CUSTOMの黒GNDをTab5 Port.Aの黒GND、白G1をTab5 Port.Aの白G54へ接続する。車速はAtomS3底面G5をTab5 M5BusのG16（**2番ピン**）へ接続する。M5Busの1番ピンもGNDとして使える。**赤い5V線は接続しない**。4芯ケーブルを使うなら赤線を確実に絶縁し、コネクタの向きとピン番号を確認する。実GPS・車両回路は外しておく。
2. 初回設定ではAtomS3をPCのUSBにつなぎ、シリアルモニター（115200 bps）から `course misato_loop`、`speed 30`、`wheel 1.03 1`、`reset`、`status` の順に送る。画面が `MISATO / READY` になり、位置が約 `36.158748, 139.163149` なら準備完了。コース・速度・車輪設定はNVSに保存され、再起動しても残る。設定後、AtomS3を独立したUSBモバイルバッテリーへつなぎ替える。
3. Tab5をUSBに接続し、GENERAL SETTINGSの `GPS INPUT` を `PORT.A` にする。車輪外周と1回転あたりのパルス数をAtomS3の `wheel` コマンドと一致させる。COURSE / STRATEGYで `MISATO` を選び、Waiting画面のGPS FIXと座標を確認する。Tab5のファームウェアとmicroSDにMISATOがない場合は、両機に共通する別のコースを選び、AtomS3へその `course <id>` を送る。
4. Tab5で `START TIMING` を押した直後、AtomS3の画面ボタンを短く押す。AtomS3が `RUN` になり、Tab5で速度・自己位置・LAPが進むことを確認する。30 km/hならMISATOの全5周は約2分。AtomS3はGOALで自動停止する。まず短時間だけ試す場合は、Tab5で計測を取り消し、AtomS3のボタンを2秒以上押して停止する。次回はSTOP画面で短押ししてリセットする。電装・点火ボタンは使わない。
5. 異常があればAtomS3の `status` と、Tab5のUSBシリアルの `status`・`wheel-debug` を照合する。AtomS3の `MISS` はパルス送出遅れ、Tab5の `gps_bytes` / `gps_rmc` はUART受信、`pulses` は採用パルスの確認に使う。

Tab5はPort.Aの実GPS向けにCASIC設定コマンドも送る。このシミュレーターはNMEA送信だけを実装しているため、歩行モード設定の応答待ちがタイムアウトすることがある。NMEA受信と周回判定には影響しないが、`gps_bytes` と `gps_rmc` が増え、画面でGPS FIXになることを確認する。

### Tab5の再起動を記録する

Tab5だけをPCのUSBへ接続し、VS Codeなど他のシリアルモニターを閉じてから、以下を実行する。ポート名は `pio device list` で確認する。ログは `tools/atoms3-simulator/logs/` に保存される。

```sh
~/.platformio/penv/bin/python tools/atoms3-simulator/scripts/monitor_tab5.py --port /dev/cu.usbmodemXXXX
```

各シリアル行にPC側の日時が付き、USB切断・再接続を `DISCONNECTED` / `CONNECTED` として記録する。起動メッセージを検出すると `BOOT MARKER` も記録する。Tab5の再起動でポート名が変わっても、USBシリアル番号が取得できる場合は自動追跡する。終了はCtrl+C。AtomS3はモバイルバッテリー給電のため、Tab5側の再起動に伴って走行を止めない。

### AtomS3画面ボタン

| 状態 | 短押し | 長押し |
| --- | --- | --- |
| READY | 走行開始 | 1.2秒以上で次のコースを選択 |
| RUN | 一時停止 | 2秒以上で停止 |
| PAUSE | 再開 | 2秒以上でSTARTにリセット |
| GOAL / STOP | STARTにリセット | STARTにリセット |

画面にはREADY時の `TAP GO / HOLD NEXT` など、次の操作を表示する。USBシリアルのコマンドも従来どおり使用できる。NVSに残るのはコース・速度・車輪設定であり、GPSノイズや欠測などの一時的な異常注入は電源再投入で解除される。

## ビルド・書き込み

```sh
pio run -d tools/atoms3-simulator
pio run -d tools/atoms3-simulator -t uploadfs --upload-port /dev/cu.usbmodemXXXX
pio run -d tools/atoms3-simulator -t upload --upload-port /dev/cu.usbmodemXXXX
pio device monitor -d tools/atoms3-simulator -p /dev/cu.usbmodemXXXX -b 115200
```

リポジトリ直下ではなく、このディレクトリが独立したPlatformIOプロジェクトです。VS Codeでも `tools/atoms3-simulator` を別のワークスペースとして開けます。ビルド時と`uploadfs`時に、共通の`assets/course_manifest.json`と各`course.json`を内容を変えずに、Git管理外の`data/`へ自動配置します。**コースを変更したときは`uploadfs`が必須**です。ファームウェアの書き込みだけではAtomS3内のコースJSONは更新されません。初回も`uploadfs`と`upload`の両方が必要です。JSONを読み込めない場合、車速出力を止め、画面に`COURSE DATA UNAVAILABLE`を表示します。

Apple SiliconのMacでは、PlatformIOに付属するLittleFS作成ツールがIntel専用の場合があります。その場合は`brew install mklittlefs`を実行してください。このプロジェクトはインストールされたネイティブ版を自動で使用します。

## 操作

USBシリアルモニターからコマンドを送ります。起動時は選択コースのSTART地点で有効なGPSを送信し、車速パルスは出しません。`start` で移動を開始します。ルートは `first_lap` → `regular_lap` の繰り返し → `final_lap` の順です。最後のGOALで自動停止します。

```text
courses                         利用可能なコース一覧
course misato_loop              コース選択（停止時のみ、番号も可）
speed 12                        走行速度、1～60 km/h
wheel 1.03 1                    車輪外周[m]、1回転のパルス数
start / pause / resume / stop   走行制御
reset                           STARTへ戻し、距離・パルス数をリセット
status                          状態・進行距離・座標・パルス数を表示
nmea on / nmea off              NMEA文をUSBシリアルにも表示／解除
help                            全コマンド表示
```

`wheel` の2値はTab5のGENERAL SETTINGSに合わせてください。初期値は1.03 m、1パルス/回転、速度は12 km/hです。車速パルスは5 ms間G5をLOWにして、その後解放します。スケジューラーが遅れた場合、パルスを短時間にまとめて出さず、欠落数を `missed` として報告します。

### GPS異常の注入

```text
seed 123                        GPSノイズの再現用乱数シード
noise 5                         測位に標準偏差約5 mのランダム誤差
bias 20 -10                     東20 m、北-10 mの持続誤差
spike 100 0                     次のNMEA文1件だけ東100 mに飛ばす
dropout 30000                   30秒間、GPS出力を停止
invalid 30000                   30秒間、RMC無効・GGA fixなし
corrupt 10                      次の10文でチェックサムを破損
```

`noise 0`、`bias 0 0`、`dropout 0`、`invalid 0`、`corrupt 0` で解除します。GPS異常中も走行距離と車速パルスは進みます。シリアルの定期表示とTab5のmicroSDログを照合できます。GPS時刻は固定の模擬UTC起点から進みます。

## 試験上の注意

- コースの座標・経路は既存のアセットを使います。Tab5でも同じコースを選択し、必要ならmicroSDに最新アセットを配置してください。
- この対向機はGPS受信モジュールとリードスイッチの信号を模擬します。電源喪失、配線接触不良、車両側電気ノイズの再現は別試験です。
- 現行Tab5は規定周回後に計測完了となります。数百周の継続評価には、繰り返し計測を開始する運用かTab5側の試験専用モードが別途必要です。
