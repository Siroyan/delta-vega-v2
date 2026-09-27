# 操作用アイコン

[Lucide](https://lucide.dev/) の `power`、`flame`、`menu`、`x`、`arrow-left`、`settings` を使用します。
取得元: https://github.com/lucide-icons/lucide/tree/66d8f9fc394b8530377e5f6112f0b8908ba01280/icons

- `power.svg` / `flame.svg` / `menu.svg` / `x.svg` / `arrow-left.svg` / `settings.svg`: 上記コミットから取得した原本。
- `electrical_power_off.*`: 青緑の電源マーク。
- `electrical_power_on.*`: 白い電源マーク。
- `ignition_flame.*`: 白い炎マーク。
- `course_ignition_flame.*`: 同じ絵柄を48 pxに縮小したオレンジのコース上点火マーク。

派生SVGは色・大きさ・配置を調整しています。
PNGは派生SVGをresvgでラスタライズした透過画像で、EEZプロジェクトにも埋め込んでいます。
電装用208×208の透過キャンバスは円形ボタンの背景画像として使い、CHECKED状態で切り替えます。

LucideのISC LicenseとFeather由来アイコンのMIT Licenseの全文・著作権表示は
同梱の[LICENSE](LICENSE)を参照してください。`power`はFeather由来の対象です。
アセット・生成画像・それらを含むファームウェアを配布する場合は、このライセンスも添付してください。

電源アイコンは96×96 px、炎アイコンは108×108 pxです。画面端で見切れる円の見た目に合わせ、
円の中心から電源は右上へ各12 px、炎は左上へ各12 pxずらしています。
円内のアイコン領域の左上座標は電源（68, 44）、炎（38, 38）です。
炎は電源との視覚的なバランスを補正するため、中心位置を維持して12.5%大きくしています。

メニュー・閉じる・戻るは32 px、設定は40 pxの濃色アイコン（`*_dark.*`）を使用しています。
