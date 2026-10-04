# MOTEGI OVAL 100

AtomS3とTab5の長時間エージング試験用に、`motegi_oval_full`の経路と地図をそのまま使い、100周に拡張したコースです。実コースの距離・地点を新たに測定したものではありません。

経路と地図の編集元は`../motegi_oval_full/`です。元コースを変更した場合は、このフォルダの`generate.py`を実行して100周版のJSON・PNG・SVG・RGB565を再生成してください。`python3 assets/motegi_oval_full_100/generate.py --check`で同期を確認できます。Tab5とAtomS3は生成済みの同じ`course.json`を直接読みます。

- 1周目はSTARTからLAP、2〜99周目は周回路、100周目はLAPからGOALへ進みます。LAP更新は99回、GOAL判定は100周目だけです。
- `course.json`の`course_id`は`motegi_oval_2025_full_100_v1`です。マニフェスト上のフォルダIDは`motegi_oval_full_100`です。
- 地図のPNG・SVG・RGB565は7周版と同じ絵柄です。100周版には独立した`map.rgb565`を置き、microSDパッケージを単独で生成できます。
- 初期TARGETは各周6分、全体10時間です。Tab5の設定保存形式には各周TARGETが7枠あり、**8〜100周目は「LAP 7+ TARGET」の値を共用**します。
- 現行の走行戦略形式は最大7周までです。この100周コース用の戦略は用意せず、戦略ファイルを読み込ませても拒否します。

AtomS3のLittleFSにはビルド時にこの`course.json`がそのまま配置されます。変更を実機へ反映するときは、AtomS3の`uploadfs`とTab5のmicroSDパッケージ更新が必要です。ファームウェアの7周上限も変更しているため、両機のファームウェアを書き込んでから使用してください。
