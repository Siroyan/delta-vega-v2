# 走行戦略データ（schema 1）

`motegi_demo.json`は**表示と読込を確認するためだけのダミー**です。1周目はスタート地点の0 mでON、全7周に各3回のON/OFF区間を仮置きしています。実際の車両・コースでの点火地点や電装OFF地点を示すものではありません。車両出力の自動制御には使用せず、ドライバー向けの案内と地図描画にのみ使用します。

## Tab5に読み込ませる

microSDに`/vega/strategies/`フォルダを作り、戦略JSONを置きます。Waiting画面のメニューから`COURSE / STRATEGY`を開き、コースと適合する戦略を選びます。`NO STRATEGY`も選択できます。画面上部に`SD OK` / `SD NG`を表示し、その横の更新アイコンでSDの再検出と一覧の再読込を行えます。選択したコースと戦略のファイル名はNVSに保存され、再起動後に復元します。選択は電装OFFのWaiting中に行います。計測開始後は変更できず、計測を取り消すと再選択できます。

一覧にはコースID、周回数、経路、距離の検証に合格した戦略だけを表示します。不適合なファイルは選択画面に並べず、理由はシリアル診断に残します。有効な戦略がない場合は一覧エリア中央に灰色の`NO STRATEGY`をラベルとして表示します。有効な戦略がある場合は`NO STRATEGY`を選択肢として残します。SDがない場合、保存済みファイルがない場合、破損した場合も戦略なしで走行できます。最大11ファイルを一覧表示します。`/vega/strategy.json`も旧版との互換性のため読み込み対象です。初回起動時の選択は`NO STRATEGY`です。

同梱の[`motegi_demo.json`](motegi_demo.json)、[`tamagawa_demo.json`](tamagawa_demo.json)、[`tobitakyu_demo.json`](tobitakyu_demo.json)は各コース用の**表示デモ**です。各JSONを`/vega/strategies/`へコピーし、Waiting画面で対応するコースと戦略を選びます。テストコース用デモのON/OFF地点は画面と選択機能の確認用の仮値です。戦略は案内と地図表示専用で、GPIOを自動操作しません。

USBシリアルの初回転送コマンドは互換ファイル`/vega/strategy.json`へ保存し、現在選択中のコースと照合します。既存ファイルがある場合は上書きしません。

```sh
python3 scripts/upload_strategy.py assets/strategy/motegi_demo.json --port /dev/cu.usbmodem1101
```

## フォーマット

```json
{
  "schema_version": 1,
  "course_id": "motegi_oval_2025_full_v2",
  "plan_id": "example-001",
  "plan_type": "demo",
  "laps": [
    {
      "lap": 1,
      "route_id": "first_lap",
      "runs": [{"on_s_m": 0, "off_s_m": 300}]
    }
  ]
}
```

実ファイルには**選択したコースの全周**を重複なく入れます。4周コースなら1〜4周、7周コースなら1〜7周です。上の抜粋は構造の説明です。

| 項目 | 意味 |
|---|---|
| `schema_version` | 現在は`1`のみ |
| `course_id` | ファームウェアに組み込んだコースのIDと完全一致させる |
| `plan_id` | 戦略の識別子。英数字、`_`、`-`、`.`のみ、39文字以内 |
| `plan_type` | `demo`なら画面に`PLAN DEMO`と表示。実戦略は`race` |
| `lap` | 1〜コースの周回数。順序は自由だが全周必須 |
| `route_id` | 1周目は`first_lap`、中間周は`regular_lap`、最終周は`final_lap` |
| `runs` | その周の点火から電装OFFまでの区間。1〜3件、距離順 |
| `on_s_m` | 経路の始点から測った、エンジン始動を促す地点（m） |
| `off_s_m` | 同じ経路上で電装OFF・惰性走行開始を促す地点（m） |

橙色は各`on_s_m`から`off_s_m`まで、青色はそれ以外の区間です。複数の`runs`があれば、前のOFFから次のONまでを青色にします。指示地点は計画値であり、Tab5はエンジンの実稼働状態を検知できません。`on_s_m`では電装ONとECU準備後のIGNITIONが必要になる場合がありますが、その操作判断はドライバーが行います。

距離`s_m`は緯度経度でもレース全体の通算距離でもなく、**各`route_id`の始点からの距離**です。対象コースのJSONの`routes.*.points[].s_m`と同じ定義です。経路長を超えるON/OFF地点は拒否します。

ファイルはUTF-8のJSON、4096バイト以内です。キー名と識別子はASCII英数字、`_`、`-`、`.`を使用し、数値は0以上の通常の10進表記にします。各周で`0 <= on_s_m < off_s_m <= 経路長`、次の`on_s_m > 前のoff_s_m`を満たしてください。未知の項目、重複した項目、コースID不一致、範囲外の距離はファイル全体を不採用にします。運用時は`plan_id`を変更して版を識別してください。

開発時に最初に配布した`dummy-race-001`だけは、旧形式の`plan_type`省略を`demo`として読み込めます。新規ファイルには必ず`plan_type`を指定してください。
