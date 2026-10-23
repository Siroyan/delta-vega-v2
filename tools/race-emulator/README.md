# race-emulator

Tab5の走行試験に使うホストアプリとAtomS3ファームウェアを、このディレクトリで一緒に管理します。

| ディレクトリ | 役割 |
|---|---|
| [`host/`](host/README.md) | ブラウザUI、試験ケースのローカル保存、シリアルログの記録 |
| [`atoms3/`](atoms3/README.md) | コースに沿ったGPS UARTと車速パルスをTab5へ送るPlatformIOプロジェクト |

PCには通常AtomS3だけをUSB接続し、Tab5はバッテリーで動かします。配線、書き込み、試験の手順はそれぞれのREADMEを参照してください。

戦略JSONに従って加速・惰性走行させる場合は、[戦略に沿った走行試験](strategy-driving.md)を参照してください。
