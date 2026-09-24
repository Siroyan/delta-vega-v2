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

EEZ Studio の画面をまだ配置していない場合は、`Hello World` と `Tap me` ボタンが
表示されます。ボタンを押すと文字が `Touch OK` に変わり、シリアルにも出力されます。

## EEZ Studio の画面を組み込む

1. EEZ Studio で **LVGL** プロジェクト（EEZ Flow なし）を作り、LVGL **9.2.2**、
   横向き **1280 × 720** を選びます。
2. `.eez-project` ファイルを `eez/` に保存し、ファームウェアと一緒に管理します。
3. **Settings → Build** で生成先を `../src/ui` に設定し、EEZ Studio でビルドします。
   この相対パスは `eez/` に置いた `.eez-project` ファイルを基準にします。
4. `src/ui/ui.h` と生成されたソースが揃ったら、ファームウェアを再ビルドします。
   `src/main.cpp` はそれらを検出し、`ui_init()` と `ui_tick()` を呼びます。

生成された `src/ui` 内のコードは直接編集せず、機器側の処理は別のファイルに書きます。
追加のフォントや LVGL 機能を使う場合は `include/lv_conf.h` で有効にします。
**LVGL with EEZ Flow** を選ぶ場合は別途 EEZ Flow ランタイムが必要です。
