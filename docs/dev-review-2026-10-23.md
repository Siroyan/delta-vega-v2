# devブランチ全体のレビュー結果

- レビュー日: 2026-10-23
- 対象: `origin/dev` の `ae91934af6d69d041e525ded0c16ea763d929095`
- 対象範囲: Tab5本体、AtomS3、PC側ツール、コースデータ、設定・書き込み処理、関連ドキュメント
- 結果: **A: 0件、B: 1件、C: 15件、D: 4件**
- 状態: 指摘の整理まで。今回のレビューでは実装を変更していない。

この文書は上記コミット時点のレビュー記録である。ソースへのリンクは対象コミットに固定している。修正状況は別途確認すること。C10のコースID比較については、レビュー時点で作業ブランチに未コミットの修正があるが、対象のdevには含まれていない。

## 分類基準

| 分類 | 基準 |
|---|---|
| A | 確実に発生する致命的な不具合。レース中に動作しなくなる、など |
| B | 確実ではないが、発生したら致命的な不具合 |
| C | 致命的ではないが修正すべき不具合 |
| D | 不具合は発生しないが、コード品質・保守性のために修正が望まれる点 |

## A. 確実に発生する致命的な不具合

**今回、該当するものは確認できなかった。**

これは実機での長時間安定性を保証するものではない。以下の故障経路や、実機で評価すべき条件は残っている。

## B. 発生したら致命的な不具合

### B1. 設定読み込み失敗時に、電装出力の極性が変わり得る

根拠: [tab5_settings_store.cpp:74][B1]、[tab5_runtime.cpp:231][B1-output]

現在は、一般設定が「まだ保存されていない場合」と「保存済みだが読み込み・検証に失敗した場合」を区別せず、旧設定または既定値へ戻す。

**LOWで電装ONとなる回路で、保存済み設定の読み込みに失敗すると、HIGHでONという既定値へ戻る可能性がある。** その結果、ソフトウェアがOFFとして出すLOWが、実回路ではONになる。

ネイティブ検証でも、不正な設定の拒否後に既定の極性へ戻る経路を確認した。実機でこの故障を観測したという意味ではない。

**修正方針:** 未保存と読み込み異常を区別する。異常時に極性を推測して上書きせず、検証済みの極性を確保できない場合は故障として扱う必要がある。

## C. 致命的ではないが修正すべき不具合

### Tab5本体

#### C1. 点火解放後の遅延時間で、小数点が消える

根拠: [lvgl_view.cpp:1431][C1]

秒単位の入力欄が数字しか許可しておらず、保存値が`0.5`なら開いた時に`05`になる。そのままSET・保存すると5秒になり得る。

**修正方針:** この項目でも小数点を許可し、表示→編集→保存で値が変わらないことを確認する。

#### C2. 茂木の自動ゴール判定がGPS更新頻度と速度に依存する

根拠: [application.cpp:303][C2]

分岐確認に「直前の測位から2m以上」が必要だが、毎回比較位置を更新している。5Hz・12km/hでは約0.67mずつなので成立しない。模擬試験では12km/hで未完走、45km/hで完走した。手動ゴールは使用できる。

**修正方針:** 直前の1区間ではなく、分岐の確認開始位置からの累積進行量などで確認する。

#### C3. SDキュー満杯時に、開始・終了記録まで捨てる

根拠: [tab5_runtime.cpp:312][C3]

終了記録が落ちるとSDタスクの「記録中」が解除されず、取消後も戦略の再読み込みが進まなくなる。

**修正方針:** 開始・終了を保護し、欠落可能なサンプルと扱いを分ける。セッション状態の整合も確認する。

#### C4. 古い戦略の読み込み結果が、新しい選択を上書きできる

根拠: [tab5_runtime.cpp:589][C4]

同じコースでAの読み込み中にBを要求すると、Aが後から「読込完了」を公開できる。実際の関数を使った再現コードで確認した。

**修正方針:** 読み込み要求に世代番号を付け、最新要求と一致する結果だけを公開する。

#### C5. 別コース用・不正なJSONが戦略一覧の枠を消費する

根拠: [tab5_runtime.cpp:545][C5]

それらが先に11件あると、その後の有効な戦略を調べず、画面にはNO STRATEGYと表示される。再現済み。

**修正方針:** 検証に合格した戦略を一覧上限の対象にする。

#### C6. Waiting画面でSDを抜いた直後でも計測を開始できる

根拠: [tab5_runtime.cpp:1275][C6]、[lvgl_view.cpp:1261][C6-detection]

抜き差し検出は選択画面を開いている間だけで、開始処理は古い`sd_ready`を参照する。記録失敗は開始後に判明する。

**修正方針:** 計測開始前にSDの利用可能性を確認する手順を設ける。

#### C7. 許可している車輪設定の一部を、実入力処理が扱えない

根拠: [reed_pulse_filter.h:37][C7]

例として周長1.03m・8パルス/回転・60km/hでは、Atomの正常パルスも固定10msの再受理条件に阻まれる。100回中1回だけ受理されることを確認した。既定の1パルス設定とは別の問題である。

**修正方針:** 設定範囲、再受理条件、Atomのパルス幅を整合させ、扱えない組み合わせを黙って受け付けない。

#### C8. 円形ボタンの見えていない角でも電装・点火を操作できる

根拠: [screens.c:828][C8]

円形に描画されているが、当たり判定は208×208の長方形である。独自の入力処理もその判定を使っている。

**修正方針:** 円形の当たり判定を有効にし、表示と操作範囲を一致させる。

### 書き込み・試験ツール

#### C9. 1コースのUSB転送で、コース一覧全体を使えなくする場合がある

根拠: [upload_course.py:81][C9]

選んだコースのファイルだけを転送する一方、カタログはリポジトリ内の全コースで置換する。他のコースがSDにないと、起動時にカタログ全体が拒否される。

**修正方針:** カタログに載せる全ファイルを転送するか、SDに存在するコースと整合するカタログを作る。

#### C10. devのrace-emulatorは、Tab5とコースIDを正しく比較できない

根拠: [controller.py:161][C10]

フォルダ名とJSON内部のIDを比較している。これは直前の作業ブランチで修正済みだが、devにはまだ入っていない。

**修正方針:** 作業ブランチの修正をdevへ反映する。

#### C11. 試験準備を停止した後でも、Tab5へ計測開始を送る競合がある

根拠: [controller.py:237][C11]

最後の準備応答直後に停止すると、画面上は停止済みなのにTab5だけ計測を開始できる。模擬通信で再現済み。

**修正方針:** 開始送信と試験状態の確認を同じ排他制御内で行い、停止済みの準備処理を無効化する。

#### C12. AtomがGOALに到着しても、Tab5が完走しない場合がある

根拠: [main.cpp:283（AtomS3）][C12]

玉川学園ではNMEAの座標丸めで約4cm手前となり、そこでAtomが停止するため、ゴール線を跨ぐ入力が届かない。模擬結合試験で確認した。

**修正方針:** コース終端と通過判定を整合させ、丸め後にもゴール線を跨ぐ入力を保証する。

#### C13. 異常注入コマンドの数字が、検証時とAtom側で異なる解釈になる

根拠: [cases.py:32][C13]、[main.cpp:544（AtomS3）][C13-atom]

ホストは`dropout 3e4`を30000msとして受け付けるが、その文字列を送るためAtomは3msとして扱う。

**修正方針:** 整数引数を十進整数へ正規化して送る。Atom側も末尾まで検証する。

#### C14. Tab5実機試験スクリプトが現行仕様では通らない

根拠: [test_device.py:44][C14]

設定数を14個と決め打ちしているが現在は28個である。削除済みのTARGET編集も試験している。

**修正方針:** 現行の設定・戦略仕様に合わせて試験内容と復元処理を更新する。

#### C15. AtomのGPS正常表示が緑にならない

根拠: [sim_display.cpp:123][C15]

先頭文字が`O`かどうかで判定しているが、正常時の文字列も`GPS OK`なので常に橙になる。

**修正方針:** 表示文字列から推測せず、GPS状態を値として渡して色を決める。

## D. コード品質・保守性の改善

### D1. 茂木由来のゴール分岐条件をコースデータへ移す

根拠: [application.cpp:7][D1]

55m・7m・18m・100mという条件が中核に固定されている。「マップ固有の内容をassets内に閉じる」という方針に合わせたい。

### D2. 実行環境Adapterを責務ごとに分割する

根拠: [tab5_runtime.cpp][D2]

GPIO、GPS、SD、転送、通信、タスク制御が集中している。今回見つかった非同期処理の競合を検証しやすくするため、まずSD・戦略読込・ネットワークの単位で分けるのが有効である。

### D3. 戦略形式の互換性テストを共通化する

Tab5、Atom、Python側で検証規則を別々に実装している。既存の互換テストを拡張し、全実装へ同じ正常・異常JSONを通す構成にすると仕様変更時の漏れを減らせる。

主な対象: [Tab5の検証][D3-tab5]、[Atomの検証][D3-atom]、[Strategy Editorの検証][D3-editor]、[race-emulatorの検証][D3-host]

### D4. 試験計画・説明書の古い前提を整理する

根拠: [release-test-plan.md][D4-plan]、[race-emulatorホストREADME][D4-host]

コースの組み込み固定、7周固定、旧NTP表示を前提とした記述が残っている。現在の仕様と過去の検証記録を区別して更新したい。

## 検証結果

既存の以下のテストはすべて成功した。それでも上記の不具合が残っており、特に**UIと入力検証、タスク間の競合、AtomとTab5の結合部分**にテストの不足がある。

| 検証 | 結果 |
|---|---|
| Tab5中核のASan／UBSanテスト | 成功 |
| race-emulator | 8テスト成功 |
| Strategy Editor | 13テスト成功 |
| Atomの運動モデル・設定保存テスト | 成功 |
| 全5コースのデータ生成・パッケージ検証 | 成功 |

追加のネイティブ・模擬通信検証では、以下を確認した。

- B1: 不正な一般設定の拒否後に、電装極性が既定値へ戻る経路。
- C2: 5HzのGPS入力で、12km/hではゴール未成立、45km/hでは成立する差。
- C4: 新しい戦略要求の後に、古い読込結果が公開される競合。
- C5: 別コース用ファイルが先に11件あると、有効な後続ファイルが一覧から漏れる挙動。
- C7: 受け付け可能な設定値でも、100回のパルスのうち1回しか受理されない組み合わせ。
- C11: 試験停止後に、Tab5へ計測開始を送ってしまう競合。
- C12: 玉川学園の終端座標の丸めによるゴール未成立。

今回の検証はPC上で行った。実機への故障注入、GPIO波形の測定、エージング評価は行っていない。生成画像・フォントのバイト列は逐語的なレビュー対象とせず、関連する形式・サイズ・境界チェック、生成元との整合性を確認した。

## 推奨する修正順

1. **B1**: 電装極性の読み込み異常を安全に扱う。
2. **C1〜C4**: 点火遅延の入力、ゴール判定、記録の開始・終了、戦略読込の競合を修正する。
3. **残りのC**: 運用・試験・書き込みツールを含む不具合を修正する。
4. **D**: 責務の整理、検証の共通化、ドキュメントの更新を行う。

<!-- 根拠リンクは、レビュー後の修正で参照位置が変わらないよう対象コミットに固定する。 -->
[B1]: https://github.com/Siroyan/delta-vega-v2/blob/ae91934af6d69d041e525ded0c16ea763d929095/src/adapters/tab5_settings_store.cpp#L74
[B1-output]: https://github.com/Siroyan/delta-vega-v2/blob/ae91934af6d69d041e525ded0c16ea763d929095/src/adapters/tab5_runtime.cpp#L231
[C1]: https://github.com/Siroyan/delta-vega-v2/blob/ae91934af6d69d041e525ded0c16ea763d929095/src/adapters/lvgl_view.cpp#L1431
[C2]: https://github.com/Siroyan/delta-vega-v2/blob/ae91934af6d69d041e525ded0c16ea763d929095/lib/vega_core/src/application/application.cpp#L303
[C3]: https://github.com/Siroyan/delta-vega-v2/blob/ae91934af6d69d041e525ded0c16ea763d929095/src/adapters/tab5_runtime.cpp#L312
[C4]: https://github.com/Siroyan/delta-vega-v2/blob/ae91934af6d69d041e525ded0c16ea763d929095/src/adapters/tab5_runtime.cpp#L589
[C5]: https://github.com/Siroyan/delta-vega-v2/blob/ae91934af6d69d041e525ded0c16ea763d929095/src/adapters/tab5_runtime.cpp#L545
[C6]: https://github.com/Siroyan/delta-vega-v2/blob/ae91934af6d69d041e525ded0c16ea763d929095/src/adapters/tab5_runtime.cpp#L1275
[C6-detection]: https://github.com/Siroyan/delta-vega-v2/blob/ae91934af6d69d041e525ded0c16ea763d929095/src/adapters/lvgl_view.cpp#L1261
[C7]: https://github.com/Siroyan/delta-vega-v2/blob/ae91934af6d69d041e525ded0c16ea763d929095/src/adapters/reed_pulse_filter.h#L37
[C8]: https://github.com/Siroyan/delta-vega-v2/blob/ae91934af6d69d041e525ded0c16ea763d929095/src/ui/screens.c#L828
[C9]: https://github.com/Siroyan/delta-vega-v2/blob/ae91934af6d69d041e525ded0c16ea763d929095/scripts/upload_course.py#L81
[C10]: https://github.com/Siroyan/delta-vega-v2/blob/ae91934af6d69d041e525ded0c16ea763d929095/tools/race-emulator/host/race_emulator/controller.py#L161
[C11]: https://github.com/Siroyan/delta-vega-v2/blob/ae91934af6d69d041e525ded0c16ea763d929095/tools/race-emulator/host/race_emulator/controller.py#L237
[C12]: https://github.com/Siroyan/delta-vega-v2/blob/ae91934af6d69d041e525ded0c16ea763d929095/tools/race-emulator/atoms3/src/main.cpp#L283
[C13]: https://github.com/Siroyan/delta-vega-v2/blob/ae91934af6d69d041e525ded0c16ea763d929095/tools/race-emulator/host/race_emulator/cases.py#L32
[C13-atom]: https://github.com/Siroyan/delta-vega-v2/blob/ae91934af6d69d041e525ded0c16ea763d929095/tools/race-emulator/atoms3/src/main.cpp#L544
[C14]: https://github.com/Siroyan/delta-vega-v2/blob/ae91934af6d69d041e525ded0c16ea763d929095/scripts/test_device.py#L44
[C15]: https://github.com/Siroyan/delta-vega-v2/blob/ae91934af6d69d041e525ded0c16ea763d929095/tools/race-emulator/atoms3/src/sim_display.cpp#L123
[D1]: https://github.com/Siroyan/delta-vega-v2/blob/ae91934af6d69d041e525ded0c16ea763d929095/lib/vega_core/src/application/application.cpp#L7
[D2]: https://github.com/Siroyan/delta-vega-v2/blob/ae91934af6d69d041e525ded0c16ea763d929095/src/adapters/tab5_runtime.cpp
[D3-tab5]: https://github.com/Siroyan/delta-vega-v2/blob/ae91934af6d69d041e525ded0c16ea763d929095/lib/vega_core/src/domain/strategy.cpp
[D3-atom]: https://github.com/Siroyan/delta-vega-v2/blob/ae91934af6d69d041e525ded0c16ea763d929095/tools/race-emulator/atoms3/src/strategy_motion.cpp
[D3-editor]: https://github.com/Siroyan/delta-vega-v2/blob/ae91934af6d69d041e525ded0c16ea763d929095/tools/strategy-editor/strategy_editor/model.py
[D3-host]: https://github.com/Siroyan/delta-vega-v2/blob/ae91934af6d69d041e525ded0c16ea763d929095/tools/race-emulator/host/race_emulator/strategies.py
[D4-plan]: https://github.com/Siroyan/delta-vega-v2/blob/ae91934af6d69d041e525ded0c16ea763d929095/docs/release-test-plan.md
[D4-host]: https://github.com/Siroyan/delta-vega-v2/blob/ae91934af6d69d041e525ded0c16ea763d929095/tools/race-emulator/host/README.md
