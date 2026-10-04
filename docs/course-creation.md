# コース作成マニュアル

この文書は、新しいコースをコーディングエージェントに作成してもらう方法と、自分で一から作る方法を説明します。どちらも、コース固有の形状・画像・初期設定は`assets/`に置き、Tab5はmicroSDの`/vega/courses/`から読み込みます。通常のコース追加では、既存ファームウェアのC++やEEZ Studioプロジェクトを変更する必要はありません。

## 第1章 コーディングエージェントを使用して作る

### 1. エージェントへ渡す情報

エージェントが道順を取り違えないよう、START・LAP・GOALの座標だけでなく、交差点ごとの進み方も伝えます。少なくとも次を用意してください。

| 情報 | 記入例・注意点 |
|---|---|
| 名称・周回数 | `MISATO`、5周。コースIDと保存先はエージェントが既存規則に合わせて決めてもよい |
| 3地点 | START・LAP・GOALを十進度の**緯度、経度**で指定する |
| 進行方向 | 北が上の地図で時計回りか反時計回りか。STARTから最初に向かう道も指定する |
| 経路の根拠 | 自分のGPS軌跡、現地で確認した道順、地図へのリンクなど。複数の道があり得る場所では曲がる角を記す |
| 運用条件 | 徒歩・自転車・車両のどれで試すか、想定速度、TARGET、使うGPS、microSDの有無 |
| 作業範囲 | データ作成のみか、Tab5への書き込み・microSDへの転送・実機確認まで行うか |

3地点だけでは経路を決められません。道順が未指定なら、エージェントには出典のある地図から妥当な経路を選ばせ、その選択を完了報告とコースのREADMEに残してもらいます。地図上で通れそうに見えても、現地の通行可否とGPS精度は実走で確認します。

### 2. 依頼文の例

次をコピーして、`<>`の部分を埋めて依頼します。不要な項目は削除して構いません。

```text
このリポジトリに新しいコースを追加してください。

コース名: <名称>
START: <緯度>, <経度>
LAP更新地点: <緯度>, <経度>
GOAL: <緯度>, <経度>
周回数: <2〜7>
周回方向: <北が上の地図で時計回り／反時計回り>
道順: <STARTから曲がる角を順に記載。GPS軌跡や地図のリンクがあれば添付>
想定する走行: <徒歩／自転車／車両、速度の目安>
TARGET: <各周・全体。未定なら初期値を提案して記録>
作業範囲: <コースデータ作成／ビルド／microSD転送／実機確認>

既存のコース形式と判定処理を確認し、コース固有の形状・画像・初期設定はassets内に配置してください。
course.json、480×480の地図画像、map.rgb565、マニフェスト、コースのREADMEを整合させてください。
START・LAP・GOALと進行方向、周回数、地図上の位置が一致することを検証してください。
PCでパッケージ生成と必要な周回判定テストを実施してください。
推測した道順・初期設定・未検証事項を最後に箇条書きで報告してください。
```

実機作業も依頼する場合は、車両回路やセンサーの接続状態、microSDの位置、USB接続状況を明記します。車両回路がない状態でも、実機で確認できるのは書き込み、SDからの読み込み、画面表示、保存値までです。現地での自動LAP・GOALやGPS精度は別に検証します。

### 3. 成果物と確認ポイント

エージェントの完了報告では、少なくとも次を確認します。

1. `assets/<コース名>/`に経路JSON、SVGまたはPNG、`map.rgb565`、READMEがあり、[`assets/course_manifest.json`](../assets/course_manifest.json)へ登録されている。
2. 北を上にした地図で道順が指定方向に合い、START・LAP・GOALが道路線の近くにある。1周目、中間周、最終周の経路長とGOALまでの順番が説明されている。
3. `python3 scripts/package_courses.py <出力先>`が成功し、必要なネイティブテストとファームウェアのビルド結果が報告されている。
4. 実機作業を依頼した場合、microSDのコース一覧、選択後の周回数、地図と地点マーカー、再起動後の設定保持を確認した結果がある。
5. 外部の地図やデータを使った場合は出典と利用条件が記録され、仮定した経路・TARGET・判定しきい値・未実施の現地試験が明示されている。

画像と数値を生成できても、現地での通過判定が正しいとは限りません。最初の実走ではGPSログを保存し、LAP・GOALの誤判定や取りこぼしを確認してください。具体的なデータ形式と手動での確認方法は次章にまとめます。

## 第2章 手動で作る

ここからは、GPS軌跡から自動生成せず、地図や現地調査をもとにコースを一から定義します。例として[`assets/misato_loop/`](../assets/misato_loop/)を参照できます。

### 1. 作業前に決めること

次の情報を先に表にまとめます。START・LAP・GOALはすべて**緯度、経度の順**に十進度で記録してください。

| 項目 | 決め方 |
|---|---|
| コースID・フォルダ名 | 例: `misato_loop_v1`・`misato_loop`。他コースと重複させない |
| 周回数 | 2〜7周。計測開始時がLAP 1で、最後のLAP表示でGOALを判定する |
| START | ドライバーが手動で計測を開始する目安。GPSでは自動開始しない |
| LAP | 1周目から最終周の一つ前まで、通過を検出する地点 |
| GOAL | 最終周だけ完走を検出する地点 |
| 進行方向・経路 | 北を上にした地図で進む順番。交差・折返し・分岐は明記する |
| TARGET | 各周と全体の目標秒数。後からSettingsで調整できる |
| 現地の利用条件 | 道路の通行可否、安全な試験方法、GPS受信環境を別途確認する |

START、LAP、GOALの3点だけでは走る道は一意に決まりません。曲がり角を含む経路の中心線を、進行順に追加で採取または地図から読み取ります。地図の道路中心線は測量値ではないので、現地で得たGPS軌跡と照合して修正する前提で扱ってください。他者の地図データを使う場合は出典と利用条件をコースのREADMEに残します。

このファームウェアでは、`N`周の計測経路は次の3種類です。

| 経路名 | 使用する周 | 始点 → 終点 |
|---|---|---|
| `first_lap` | 1周目 | START → LAP |
| `regular_lap` | 2〜`N-1`周目 | LAP → LAPの1周 |
| `final_lap` | `N`周目 | LAP → GOAL |

例えば5周なら、計測開始後にLAPを4回通過して表示がLAP 5になり、その後GOALを通過すると完走します。GOALがLAPのすぐ先にあるコースでも、その短い区間を`final_lap`として定義します。別のゴール分岐がある場合は`segments.finish_approach`も必要ですが、分岐専用の判定条件があるため、既存の茂木データを参考に個別のテストを追加してください。

### 2. 地理座標を経路点に変換する

1. `assets/<フォルダ名>/`を作り、調査した緯度経度の点を進行順に並べます。急な曲がり角、分岐、START、LAP、GOAL付近には点を追加します。
2. コース付近に原点`lat0, lon0`を一つ置き、すべての点を東方向`east_m`・北方向`north_m`のメートル座標へ変換します。小さなコースでは次の近似を使えます。

   ```text
   north_m = (lat - lat0) × north_m_per_lat_deg
   east_m  = (lon - lon0) × east_m_per_lon_deg
   north_m_per_lat_deg ≈ 111195
   east_m_per_lon_deg  ≈ 111195 × cos(lat0 × π / 180)
   ```

   広いコースや高精度が必要な場合は適切な測地変換で係数を求めてください。JSONに書く係数と、各点の計算に使う係数は必ず同じにします。
3. 各経路の最初の点を`s_m = 0`とします。次の点の`s_m`は、前の点までの距離に`hypot(Δeast_m, Δnorth_m)`を足した累積値です。`s_m`は厳密に増加させ、最後の値と`length_m`を一致させます。実機ローダーが許す差は0.02 m以内です。
4. `regular_lap`はLAP付近を最初と最後に置き、進行方向へ1周させます。始点と終点は同じ場所でも、終点の`s_m`は周回長にします。この形式では`closed: false`とし、終端点を明示するのが分かりやすいです。`first_lap`と`final_lap`も同じ進行方向で切り出します。

例えば隣接する点が`{"east_m": 10, "north_m": 20, "s_m": 0}`と`{"east_m": 13, "north_m": 24, "s_m": 5}`なら、その区間は5 mです。点列を逆順にすると自動LAP・GOAL判定は機能しません。

通過判定は設定座標を経路上の最近点へ投影して行います。LAPは`regular_lap`上、GOALは通常`final_lap`または`finish_approach`上に置きます。指定座標と道路中心線が少し離れていても、`course_corridor_m`内で、かつ別の区間へ誤投影されない位置にしてください。地図上のLAP線はこの最近点の接線に垂直に描画されます。

### 3. `course.json`を作る

`assets/<フォルダ名>/course.json`を作ります。Tab5とAtomS3で共用するため、マニフェストの`source`にも`course.json`を指定します。既存の[`MISATOのJSON`](../assets/misato_loop/course.json)を**形式の見本**にして、以下の値を自分のコースに置き換えてください。`landmarks`や`source`は説明用データで、実際の地点マーカーはマニフェストとNVSの座標から描かれます。

| キー | 設定内容 |
|---|---|
| `schema_version` | `2` |
| `course_id` | マニフェストと一致する一意のID |
| `coordinate_system` | 原点、東西・南北の1度当たりメートル係数 |
| `render.local_to_pixel_matrix_2x3` | 地理座標から480×480画像座標への変換行列 |
| `routes.first_lap` / `regular_lap` / `final_lap` | 各経路の`closed`、`length_m`、進行順の`points` |
| `segments` | 通常は`{}`。ゴール分岐がある場合だけ`finish_approach`を追加 |
| `lap_count` | 2〜7の整数 |
| `race_sequence` | 1周目`first_lap`、中間`regular_lap`、最終`final_lap`の順 |
| `race_length_m` | `first_lap.length_m + (N-2) × regular_lap.length_m + final_lap.length_m` |

各経路の点は次の形式です。`from`・`to`は人が読むための名前です。

```json
{
  "from": "start",
  "to": "lap_update",
  "closed": false,
  "length_m": 5.0,
  "points": [
    {"s_m": 0.0, "east_m": 10.0, "north_m": 20.0},
    {"s_m": 5.0, "east_m": 13.0, "north_m": 24.0}
  ]
}
```

各経路は2〜2048点、`length_m`は正数が必要です。周回路での現在位置は`regular_lap`の点列へ投影されます。1周目と最終周の表示・進行には対応する経路が使われます。近接して交差する経路では、GPSが別の区間に投影されないか現地確認が必要です。

### 4. 地図を480×480画像にする

北が上のSVGまたはPNGを作り、経路が480×480 pxの内側に収まるよう余白を取ります。START・GOALの文字やLAP線はファームウェアが上から描くため、背景画像に重複して描かないでください。`render.background`と`render.svg`には編集元ファイル名を記録します。`scripts/generate_course_data.py`を検証に使う場合、この2ファイルも実在させてください。

経路点と画像の位置を合わせる変換行列は次です。`scale > 0`、`x`は右が東、`y`は上が北になるよう符号を反転します。例えば東西・南北の範囲の大きい方を400 pxに収めるなら、`scale = 400 / max(east_max-east_min, north_max-north_min)`として中央寄せします。

```text
x_px = scale × east_m + offset_x
y_px = -scale × north_m + offset_y
local_to_pixel_matrix_2x3 = [[scale, 0, offset_x], [0, -scale, offset_y]]
offset_x = 240 - scale × (east_min + east_max) / 2
offset_y = 240 + scale × (north_min + north_max) / 2
```

画像と行列の原点・縮尺が一致しないと、地図、自己位置、START・GOALの印がずれます。PNGの見た目だけを調整した場合も行列を計算し直してください。

microSD用には**480×480 px、RGB565、1画素2バイト、リトルエンディアン、ヘッダーなし**の`map.rgb565`が必要です。サイズは必ず460800バイトです。PNGから作る場合はPythonのPillowを使い、次の例を実行できます。

```sh
python3 -m pip install Pillow
python3 - assets/my_course/map.png assets/my_course/map.rgb565 <<'PY'
import struct
import sys
from PIL import Image

image = Image.open(sys.argv[1]).convert("RGB")
if image.size != (480, 480):
    raise SystemExit("PNG must be exactly 480x480 pixels")
with open(sys.argv[2], "wb") as output:
    for r, g, b in image.getdata():
        output.write(struct.pack("<H", ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)))
PY
```

既存の[`MISATO生成スクリプト`](../assets/misato_loop/generate.py)は、座標からJSON・SVG・PNG・RGB565を一括生成する参考例です。手作業で描いた画像を使う場合も、画像と行列の一致を優先してください。

### 5. マニフェストへ登録する

[`assets/course_manifest.json`](../assets/course_manifest.json)の`courses`へ1件追加します。ここで指定する座標と初期設定が、実機の通過判定と地点マーカーに使われます。下は構造例です。値は自分のコースに合わせて置き換えてください。

```json
{
  "folder": "my_course",
  "source": "course.json",
  "name": "MY COURSE",
  "short_name": "MY COURSE",
  "settings_namespace": "vega-mycourse",
  "legacy_settings_namespace": "",
  "legacy_total_target_s": 0,
  "pedestrian_gps": true,
  "total_target_s": 900,
  "lap_target_s": 180,
  "course_corridor_m": 18,
  "min_lap_progress_m": 80,
  "min_lap_ms": 8000,
  "lap_duplicate_ms": 5000,
  "start": [35.0, 139.0],
  "timing": [35.0, 139.0],
  "goal": [35.0, 139.0]
}
```

上の3座標は**説明用の仮値**です。そのまま使わないでください。`pedestrian_gps`はPort.AのUnit GPSを徒歩試験する場合に`true`とし、通常の車両用なら用途に合わせて決めます。`lap_target_s`は初期値として全周へ同じ秒数を設定し、周ごとの微調整はSettingsで行います。

`min_lap_progress_m`はLAPまでに必要な進行距離、`min_lap_ms`は最短ラップ時間、`lap_duplicate_ms`は連続操作の抑止時間、`course_corridor_m`は経路から許容する横ずれです。短すぎるコースで既定の600 m・60秒を使うと自動LAPが成立しないため、経路長と想定速度から決めます。一方で小さすぎる値はGPSノイズによる誤判定を増やします。最終周の短いLAP→GOAL区間には別の進行距離条件もあるため、必ずその区間を模擬走行で確認してください。

現在は最大8コースです。`folder`は英小文字・数字・アンダースコアで1〜40文字、`settings_namespace`は英小文字・数字・ハイフンで15バイト以内にしてください。名前やIDには実機ローダーの長さ制限があります。既存ファイルの表記に倣い、短いASCII名にすると確実です。`course_id`と`settings_namespace`は既存コースと重複させないでください。

コース別設定はNVSに保存されます。**同じnamespaceを使うコースを再配布しても、すでに保存されたTARGETや座標は新しいマニフェスト初期値で上書きされません。**既存コースの変更はSettingsから更新し、新しいコースとして扱うなら新しいID・namespaceを付けます。輝度やGPS入力先などの端末共通設定はコース切替時に引き継がれます。

### 6. PCで確認してmicroSDへ配置する

リポジトリのルートで実行します。`my_course`は作ったフォルダ名へ読み替えてください。

```sh
python3 -m json.tool assets/my_course/course.json > /dev/null
python3 -m json.tool assets/course_manifest.json > /dev/null
wc -c assets/my_course/map.rgb565     # 460800 と表示されること
python3 scripts/generate_course_data.py --source assets/my_course/course.json
python3 scripts/package_courses.py /tmp/vega-course-package
```

`generate_course_data.py`は経路の点数・累積距離・`race_sequence`・画像ファイル名を検査し、ローカルテスト用の`course_data.h`を作ります。**このヘッダーは実機のコース登録には不要**です。`package_courses.py`は全コースのSD配置を生成します。両スクリプトが通ってもGPSによる実際の通過判定までは保証しないため、重要な新規コースにはネイティブテストへ模擬走行を追加します。

配置方法は二つあります。

1. **カードリーダー**: Tab5を停止し、カードをPCへ移します。生成した`/tmp/vega-course-package/vega/courses/`をmicroSDの`/vega/courses/`へコピーします。既存のセッションログや戦略ファイルは残します。安全に取り出してTab5へ戻し、再起動します。
2. **USBシリアル**: microSDをTab5に挿したまま、Waiting・電装OFF・非計測で、シリアルモニタを閉じて次を実行します。スクリプトが対象コースと更新したカタログを転送し、再起動して対象を選択します。`pyserial`が必要です。

   ```sh
   python3 scripts/upload_course.py my_course --port /dev/cu.usbmodemXXXX
   ```

どちらの方法でも、`catalog.json`、`<folder>/course.json`、`<folder>/map.rgb565`がそろう必要があります。ファームウェアは起動時にカタログと画像を読み込むため、ファイルを差し替えた後は再起動します。USB転送スクリプトは転送完了後に自動で再起動します。

### 7. 実機で確認する

1. Waiting画面と`COURSE / STRATEGY`画面に新コースが現れ、`SD OK`が表示されることを確認します。選択して地図の向き、START・GOAL・LAP線、凡例、自己位置のずれを見ます。
2. シリアル115200 bpsで`course-list`と`status`を送ります。コースID、`lap_count`、`sd_ready=1`、`power_pin=0`を確認します。必要なら`settings`でTARGETと3地点の座標、周回条件を確認します。
3. GPSが使える環境で、計測を手動開始して進行方向に走ります。各LAP通過で1周ずつ増え、最終周のGOALでのみ完走することを確認します。逆走、近接区間、GPS欠落、START・LAP・GOAL付近の停止も試します。microSDのセッションログに`gps_lap`と完走イベントが記録されたか確認します。
4. 画面が正しく見えても、現地のGPSが経路から外れると判定できません。軌跡、衛星数、HDOPとコース回廊を見て、必要なら経路点または設定座標を調整します。設定だけを変えても背景画像の道路線は動かないため、経路と画像は一緒に更新します。

新しいコースを追加した時点で、経路の通行可否・GPS精度・自動LAP/GOALの成立は未検証です。正式運用前に現地走行で確認し、暫定値と確認結果をそのコースのREADMEへ記録してください。
