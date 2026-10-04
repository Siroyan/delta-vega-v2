# コースアセット

コース形状、地図画像、初期設定はmicroSDから読み込みます。ファームウェアには個別コースを組み込みません。元データはこのディレクトリで管理し、`course_manifest.json`の順序が選択画面の順序と初回選択を決めます。茂木、玉川学園前、飛田給、MISATOは同じ形式です。

## microSDへの配置

次のコマンドで、指定したディレクトリに`vega/courses/`を生成します。microSDのルートを指定すれば直接配置できます。

```sh
python3 scripts/package_courses.py sdcard-package
```

生成された`sdcard-package/vega/courses/`をmicroSDの`/vega/courses/`へコピーしてください。配置内容は`catalog.json`と、各コースの`course.json`・`map.rgb565`です。画像は480×480のRGB565データです。Gitでは編集元のJSON・画像・マニフェストに加え、MISATOの`map.rgb565`を管理します。microSD用の`catalog.json`はパッケージ生成時に作ります。

カードをTab5に挿したままUSB経由で1コースを追加する場合は、Waiting画面・電装OFFで`python3 scripts/upload_course.py misato_loop --port <シリアルポート>`を実行します。`course.json`・`map.rgb565`・更新した`catalog.json`を順に転送し、Tab5の再起動とコース選択まで確認します。転送中はmicroSDを抜かないでください。

起動時に最大8コースを検証してPSRAMへ読み込みます。microSDがない、カタログがない、ファイルが壊れている場合はコースを選択できず、計測開始もできません。走行中にカードが抜けた場合、読み込み済み経路で画面と周回判定は続け、SDログのエラーを表示します。カードを差し戻すと選択画面の`SD OK`と一覧が戻ります。**コースファイルを変更したときはTab5を再起動**してください。設定済みコースIDと一致すればNVSのコース別設定を引き継ぎます。

旧版の端末共通設定を初回移行するときだけ、microSDを読む前に電装出力の極性が必要です。そのため旧3コースのIDとNVS領域の対応だけはファームウェアに互換表として残します。コース形状・画像・初期設定はこの表に含みません。

| 元フォルダ | 周回数 | NVS設定領域 |
|---|---:|---|
| `tamagawagakuen_station_loop/` | 4 | `vega-test4` |
| `tobitakyu_hospital_loop/` | 4 | `vega-tobi4` |
| `motegi_oval_full/` | 7 | `vega` |
| `misato_loop/` | 5 | `vega-misato5` |

新しいコースは同じschema 2のJSONと480×480 RGB565画像を用意し、`course_manifest.json`へID、表示名、NVS領域、初期座標とTARGETを追加して再生成します。短いコースでは`min_lap_progress_m`、`min_lap_ms`、`lap_duplicate_ms`もマニフェストで初期設定できます。旧アセットの`course_image.c`または新規アセットの`map.rgb565`をパッケージ生成元に使えます。個別コースの形状をC++へ再生成する必要はありません。

戦略JSONはmicroSDの`/vega/strategies/`に配置します。詳細は[走行戦略データ](strategy/README.md)を参照してください。
