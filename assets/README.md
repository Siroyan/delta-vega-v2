# コースアセット

収録済みコースの形状、画像、初期値とNVS領域の対応は、このディレクトリで管理します。`course_catalog.cpp`の並びが画面のコース一覧です。初回起動では先頭の玉川学園前を選びます。茂木、玉川学園前、飛田給は同じ`CourseAsset`として扱い、コースごとの初期座標、TARGET、地図画像、歩行向けGPS設定は各エントリから参照します。

| フォルダ | 周回数 | NVS設定領域 |
|---|---:|---|
| `tamagawagakuen_station_loop/` | 4 | `vega-test4` |
| `tobitakyu_hospital_loop/` | 4 | `vega-tobi4` |
| `motegi_oval_full/` | 7 | `vega` |

各コースの`course_data.h`は同じフォルダのJSONから生成します。例:

```sh
python3 scripts/generate_course_data.py --source assets/tobitakyu_hospital_loop/tobitakyu_course.json
```

各フォルダの`course_image.c`と`course_catalog.cpp`はPlatformIOの`scripts/build_course_assets.py`でファームウェアに組み込みます。EEZの画面定義には個別の地図を埋め込まず、共通の地図ウィジェットへ選択した画像を設定します。新しいコースを追加する際はJSON、480×480画像、そのLVGL画像データ、初期設定とカタログのエントリを同じアセット領域へ追加します。

戦略JSONはmicroSDの`/vega/strategies/`に配置します。詳細は[走行戦略データ](strategy/README.md)を参照してください。
