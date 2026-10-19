# 現在の配信負荷の測定（2026-10-16）

測定: 2026-10-16T10:50:19.7402307+09:00 ～ 2026-10-16T10:50:49.7049497+09:00、約29.989秒。約1秒間隔の30区間。

Sunshine PID 11412、NVIDIA GeForce RTX 4090、論理CPU 32。配信・設定を変更せず読み取りのみ。
オリジナル版との比較測定ではなく、この区間の現在の配信負荷。ゲーム側のFPSや入力→表示遅延は未測定。

| 対象 | 平均 | 最大の約1秒サンプル |
| --- | ---: | ---: |
| Sunshine CPU（PC全体基準） | 0.169% | 0.400% |
| Sunshine GPU 3D | 0.546% | 0.644% |
| Sunshine Video Encode エンジン7 | 3.394% | 3.566% |
| Sunshine Video Encode エンジン6 | 3.364% | 3.518% |

Copyエンジンの当該カウンター差分は全区間0。GPU内コピーが皆無という意味ではなく、3D側で実行される処理もある。
GPUエンジン使用率を足して「GPU全体使用率」としない。CPU平均は1論理CPU基準で約5.41%。

Sunshine物理常駐メモリ（WorkingSet）平均45.5 MiB。PrivateBytesは約394.6 MiBで、物理常駐量とは異なる。

NVIDIAのGPU全体値（他アプリと測定処理を含む）: GPU使用率平均5.52%、VRAM平均2108.74 MiB、消費電力平均55.39 W。
Sunshine単独VRAMカウンターはGPU全体値と整合しないため採用しない。

測定後のNVENCセッション観測: PID 11412、H.265、3840×2560、平均54 fps、エンコーダー平均遅延662 µs。
この遅延はGPUエンコーダーのドライバー統計で、前処理・通信・クライアント表示を含む総遅延ではない。

## 解釈と制限

- この区間ではSunshineによる高いCPU/GPU占有は観測していない。ゲーム高負荷時や異なる映像での挙動は未確認。
- オリジナル版との差分、独自画像処理だけの追加コスト、キャプチャ/DWM等の間接的な負荷は切り分けていない。
- 約1秒の平均カウンターなので、短いフレーム単位のスパイクは見えない。測定処理自体の観測負荷も分離していない。
- 動き解析と領域別QPマップはまだ未実装で、この値に含まれない。

## ローカル証拠

- `cmake-build-custom/stream-load-20261016-105019.json`: 生カウンター、CPU・GPU・NVIDIAの各サンプル。
- `cmake-build-custom/stream-load-20261016-105019-summary.json`: 計算済み平均・最大・p95と測定の制限。
- `cmake-build-custom/stream-load-20261016-105019-encoder-sessions.txt`: 測定後のNVENCセッション。
- `cmake-build-custom/measure-current-stream.ps1`: 今回の読み取り専用測定。固定PIDのため再利用時は対象を再確認。

Windows GPUカウンターの解釈: [Microsoft公式解説](https://devblogs.microsoft.com/directx/gpus-in-the-task-manager/)。
NVENC統計の単位はローカルの `nvidia-smi --help-query-gpu` とセッション表ヘッダーを確認。
