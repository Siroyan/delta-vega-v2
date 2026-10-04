# MISATO コース

指定された3地点を結ぶ、反時計回りの約281 mの暫定周回路です。OpenStreetMapの道路中心線（way 120813336、500802680、120813365）から作成しました。地図画像には道路とコース線だけを描き、START・LAP・GOALはファームウェアが重ねます。現地測量やGPS軌跡による校正前です。

| 地点 | 緯度 | 経度 | 役割 |
|---|---:|---:|---|
| START | 36.158741 | 139.163142 | 手動で計測を始める目安 |
| LAP | 36.158129 | 139.162450 | 1〜4回目の周回更新 |
| GOAL | 36.158181 | 139.163041 | 5周目の完走判定 |

STARTから北側の道を西へ進み、LAPを通過後、南側の道を東進して右側の道を北上します。1周目は約136 m、2〜4周目は各約281 m、最後はLAPからGOALまで約59 mです。計測区間の合計は約1.04 kmです。アプリ上の「5周」は、4回のLAP更新後に最終周としてGOALへ進む定義です。

この短い経路の初期設定は、最小周回進行距離80 m、最小ラップ時間8秒、周回操作の重複防止5秒、コース回廊18 m、TARGET各周3分・全体15分としました。いずれもSettingsで後から変更できます。Unit GPSは徒歩テストに使えるようWalking modeを選ぶ設定です。走行戦略は用意していません。

`misato_course.json`が実機の経路、`misato_course.svg`と`misato_course_480.png`が背景画像の編集・表示用、`map.rgb565`がmicroSDへ配置する480×480画像です。`generate.py`でこれらを再生成できます。`course_data.h`はローカルの経路判定テスト用で、実機には組み込まれません。パッケージ全体は`python3 scripts/package_courses.py <microSDのルート>`で配置できます。Tab5をWaiting・電装OFFにしてUSB接続した状態なら、`python3 scripts/upload_course.py misato_loop --port <シリアルポート>`でmicroSDへ転送・再起動・選択まで実行できます。**コースを追加した後はTab5を再起動**してください。

道路形状の出典は[OpenStreetMap contributors](https://www.openstreetmap.org/copyright)（ODbL 1.0）です。取得範囲・way ID・取得日はJSONに記録しました。公道の通行可否やGPS位置ずれ、実際のLAP・GOAL判定は現地で確認してください。
