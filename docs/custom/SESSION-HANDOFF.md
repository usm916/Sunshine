# 2026-10-19：GitHub共有とMac開発の開始

最新ソースは `usm916/Sunshine` と `usm916/moonlight-qt` の `custom/main` を共有元にする。[Mac開発引き継ぎ](mac-development.md) を最初に読み、[互換性記録](compatibility.json) のクライアントと共通ライブラリのコミットを確認する。[検証概要](validation-2026-10-19.json) はWindowsの結果であり、Macのビルド・接続は未確認。

以下はTANの稼働・配置記録と過去の作業履歴。MacでWindows用サービスやD3D設定をそのまま実行しない。

# 2026-10-19 16:11 JST：移動回線・第2修正をTANへ適用済み

本体SHA-256：`e256765a81f36540ab406a427df36d874a5cfa4b2e51a658f3fbdeb04ce6efc4`。正式426件／54スイート成功、変更実行行155/155、GPU読み戻し、隔離ループバックで同一接続中の粗粒化有効→無効を確認した。実WAN・iPadでの第2修正の回復動作は未確認。

専用サービスSunshineCustomServiceはRunning、確認時サービスPID11032／ホストPID55304。配置先は `cmake-build-custom/package`、サービス定義と設定保存先は継続。実保存先の設定・状態ファイル・証明書・秘密鍵・アプリ一覧は更新前後でバイト一致。実ホストのIDとTLS証明書も一致し、登録済み3台を保持。AnyDesk・GSv6FwdSvc・インストール版Sunshineの状態は維持。

- 部分更新：`cmake-build-custom/Sunshine-mobile-recovery-v2-20261019-binary-update.zip`
- 更新前の本体：`c7e1056df2ae049e845990e178e76de7b3ed43ee9c917c79ff96dcbfea96f8d3`
- 更新前ファイルの退避：`cmake-build-mobile-recovery-v2/deployment-20261019-7f551c21/before`
- [変更内容・検証](mobile-link-recovery-v2-2026-10-19.md)、[iPad/iPhone入力の調査](input-low-bandwidth-audit-2026-10-19.md)
- 証拠：`cmake-build-mobile-recovery-v2/tan-deployment-result.json`、`tan-identity-verified.json`、`native-tests.xml`、`changed-line-coverage.json`、`live-result.json`、`package-result.json`

巻き戻す場合は同じサービスを停止し、上記退避先の本体と版情報を同じ配置先へ戻して開始する。設定・ペアリングをコピーし直さない。AppDataの実ファイルを読む場合は下記のUNC診断上の注意を守る。

以下は前回修正などの履歴であり、現行版は本節を優先する。

# 2026-10-19：TANへ回復修正版を適用済み

本体 `c7e1056d...` へ差し替え、Sunshine Customを再起動した。確認時のサービスPID19996／ホストPID29104。登録済み配置先・共通設定・起動方式を維持。実保存先とホストID・TLS証明書が一致し、既存のMoonlight登録3件を保持。4つのTCP待受とWeb UIの401応答を確認。AnyDesk等の他サービスは変更しない。

[適用・復元先と診断上の注意](tan-update-2026-10-19.md)。証拠：`cmake-build-recovery-fix/tan-deployment-result.json`、`tan-identity-verified.json`。AppDataの古い仮想化コピーを実設定と取り違えない。以下の「未適用」は適用前の記録である。

# 2026-10-18の回復修正：ビルド済み・常用版は未適用

移動回線向けの3秒送信量平均、条件付きRTT基準更新、状態変化時の診断ログを実装した。正式409件のテスト、変更実行行104/104、隔離ループバックで4秒の受信停止後に同じ接続で回復開始を確認。実WANでの検証と、実接続での粗粒化解除までの確認は未実施。

- 修正版：`cmake-build-custom/package-recovery-fix-20261018`
- 配布ZIP：`cmake-build-custom/Sunshine-recovery-fix-20261018-windows-x64.zip`
- 修正版exe SHA-256：`c7e1056df2ae049e845990e178e76de7b3ed43ee9c917c79ff96dcbfea96f8d3`
- 常用先の旧exe SHA-256：`a3e82f2a0f5807031e02a47682e378d105866a25018b5af33ae660a67d5c136b`。この作業では停止・差し替えを行っていない。
- 詳細：[recovery-fix-2026-10-18.md](recovery-fix-2026-10-18.md)
- 証拠：`cmake-build-recovery-fix/result.json`、`native-tests.log/xml`、`changed-line-coverage.json`、`live-result.json`。
- 回復速度（10秒安定後・約2秒ごと5%増加）と、エンコーダー再設定失敗後の停止方針は維持。

以下は、この変更前の常用版についての記録。

# Sunshine Custom セッション引き継ぎ

更新日: **2026-10-17（JST）**。最終配置 2026-10-17T04:07:48.8414765+09:00。現在は**20px初期値＋端中央でのモニター切り替え**を有効にした版。
作業場所: `D:\data\code\sunshineCustom`。以下の旧50px記録より本節を優先する。

## 現在の動作とユーザー指定

- クロップの初期値を上下左右20pxへ変更。旧0値も20pxとして扱う。内部のクロップ区画は従来の固定区画。
- ユーザー指定は「画面の端中央に一定時間マウスを置いたらモニター切り替え」。モニター外端に達した枠で、表示辺の中央20%・端20px以内に600ms静止すると、同方向・同じ横／縦座標の最寄りの画面へ移る。4px超の動きで待機をやり直す。
- 同じGPUと同じHDR状態のWindows D3D画面・1配信接続で有効。直接タッチ／ペン押下中と複数接続では休止。キャプチャ・互換性・マウス移動失敗時は元の画面へ戻す。
- 送信サイズと通信セッションを保ち、既存のキャプチャ／エンコーダー再初期化を使う。映像が止まる可能性と、リソース枯渇等での再作成失敗リスクは残る。切断しないことを実証したという意味ではない。
- キャプチャ再作成時間は `Monitor edge dwell` の `capture_reopen_ms` へ記録。総停止時間ではない。
- GPU差分処理や画像のCPU読み戻しは未実装のまま。今回の追加はマウス位置最大40Hz・モニター配置最大2HzのCPU側確認。
- 現環境で実際に列挙できた画面は1枚。2画面間の実キャプチャ切り替え、停止時間、iPad操作感は未確認。

## 稼働先と復元先

| 項目 | 値 |
| --- | --- |
| 常用exe | `cmake-build-custom/package/sunshine.exe` |
| SHA-256 | `a3e82f2a0f5807031e02a47682e378d105866a25018b5af33ae660a67d5c136b` |
| 現ホスト／親サービス | PID11164 / PID39780、対話セッション1（採取時点） |
| 配置ファイル | 164ファイルを照合 |
| 専用サービス | SunshineCustomService、Running、LocalSystem、Auto |
| インストール版 | SunshineService、Stopped、Disabled（作業開始時から変更なし） |
| 周辺サービス | AnyDesk PID5108、GSv6FwdSvc PID13804を維持 |
| 共通設定 | `C:\Users\noTempo\AppData\Local\SunshineCustom\config` |
| 現設定 | `viewport_crop_monitor_switch = enabled`、`viewport_crop_monitor_switch_delay = 600` |
| 機能なし20px復元先 | `cmake-build-custom/package-crop-margin-20`、162ファイル |
| 復元先SHA-256 | `d71030946d2c98881323295cc14542aef46723f92c6cf9b00de32bbe1fa6a4ff` |

通常のStart/Stop/Restartは専用LocalSystemサービスを使う。ラッパーは以前にSYSTEM SIDを照合したハッシュと同じ。
現在はSYSTEMホストの直接パス照会がAccess deniedとなるため、固定SCMコマンド・既知ラッパーのハッシュ・親子関係・対話セッション・待受所有者から起動先を確認する。今回ホストSIDを直接再読取していない。
GSv6FwdSvcはMoonlight Internet Hosting ToolのIPv6転送で3ポートを別途待ち受ける。Sunshineの複数起動ではなく、停止対象にしない。
AnyDeskと両Sunshineサービスの起動方法、既存のペアリングは維持。

## 巻き戻し

- 直下の **`Rollback Monitor Switch.cmd`** で、モニター機能追加前の20px版へ丸ごと復元する。
- `-CheckOnly` は停止せず、パッケージ全162ファイルと復元ヘルパーのハッシュを照合する。
- **実コマンドでの復元を確認済み**。旧20pxホストPID27332への復元、旧exeハッシュ、単一ホスト・4待受を確認した後、機能あり版へ戻した。
- `checkpoint-crop20.json` に固定一覧、`checkpoint-crop20-source.zip` と別の一覧に機能追加前の変更済みソース104ファイルを保存。復元コマンドは稼働物だけを戻し、作業ソースを上書きしない。
- Web設定の `Switch monitors at edge centers` をオフにして再起動しても、20pxの通常クロップへ戻せる。
- 共通設定には機能追加前バックアップと再配置前バックアップが残る。最新バックアップは `sunshine.conf.before-monitor-edge-20261017-040734.bak`。新モニター設定以外を書き換えない。
- 設定変更・配置失敗時は以前の設定／パッケージを復元する配置手順を使った。実行中ファイルの上書きは行わない。

## 検証と証拠

ネイティブ**398件／49スイート**、Web**72件／11ファイル**、巻き戻し検証**5件**が成功。
端中央・4方向・負座標・隙間・抜き差し・静止待ち・直接接触・複数接続・失敗復帰を模擬配置で検証。
実WARPデバイスでGPU/HDRキーを取得し、実Windows配置／マウスを照合。2画面の実配信試験とは区別する。
実行行カバレッジはビューポート94/94、モニターポリシーヘッダー81/82（未実行はcapture_key_tのbool hdr宣言に付く行127）、Windows制御51/53（アダプター照会とカーソル取得のOS失敗分岐）。キャプチャループ全体や全分岐100%を示さない。

証拠は `cmake-build-custom/` 内。

| ファイル | 内容 |
| --- | --- |
| `monitor-edge-switch-native-tests.log/xml` | 398件成功 |
| `monitor-edge-switch-web-tests.log` | Web72件成功 |
| `monitor-checkpoint-tests.log` | 復元前検証5件成功 |
| `monitor-edge-switch-coverage.json` | 102オブジェクトからの実行行集計 |
| `monitor-edge-switch-deploy.json` | 最新の機能あり版配置と設定バックアップ |
| `monitor-edge-switch-first-deploy.json` | 最初の機能あり版配置 |
| `monitor-edge-switch-live-rollback.json` | 実復元コマンドでの20px版復帰 |
| `monitor-edge-switch-final-runtime.json` | 文書同期後の単一起動・待受・設定・ファイル照合 |
| `monitor-edge-switch-final-manifest.json` | 最新配布164ファイルのハッシュ |
| `checkpoint-crop20.json` | 変更しない巻き戻し先と復元ヘルパーのハッシュ |

操作の詳細は [monitor-switch.md](monitor-switch.md)。背景の自動倍率1/2・1/3・1/4・1/6、音声復帰修正は維持。音声の実再生・UAC画面の実キャプチャ・実WAN回復の今回の再検証はしていない。

## 以前のユーザー制約

AnyDeskを維持。PCのMoonlightはウィンドウモード限定。既存の多数の未コミット変更をreset/clean/checkoutで失わない。
旧「端で待たない」は単一モニター内クロップに適用する。最新ユーザー指定のモニター切り替えは端中央での静止を使い、その切り替え時だけ移動先へカーソルを置く。

---

# 過去の50px版までの引き継ぎ

更新日: **2026-10-17（JST）**。上下左右50pxのクロップ切り替えを配置し、専用LocalSystemサービスから再起動を確認。今回の証拠は `crop-margin-50-*`。
作業場所: `D:\data\code\sunshineCustom`。

## 現在の状態

- **50pxの端判定を反映**。上下左右の表示端50pxに入ると固定枠を切り替え、中央では固定。初期値と旧0設定を50pxへ統一。重なりが狭い場合は往復防止のため半分以下（最小1px）に制限。
- 現ホストPID21764、親サービスPID9348、対話セッション1、1ホスト・4待受を確認。専用サービスの通常権限での実Stop/Startとパッケージ全体照合が成功。LocalSystemアカウントと以前にSYSTEM SIDを検証した同一ハッシュのラッパーを維持。今回はホストトークンSIDと直接実行パスの再読取は権限で不可。固定SCMコマンドと検証済みラッパーの親子関係から起動先を照合した。
- 2026-10-16の移行時には昇格処理で実トークンSID `S-1-5-18` を検証済み。
- 最初の試行はサービスの監査用SACLもあることを拒否して失敗したが、旧版へ自動復旧した。DACLだけを更新して監査設定を触らない方式へ修正・回帰テストを追加し、2回目の管理者承認後に成功。`system-service-first-failure.json` は過去の失敗記録。
- 新しいラッパーは公式サービス実装を拡張し、別名サービス・固定共通設定パスに対応。移行時にはインストール版SunshineServiceのAutoを維持した。2026-10-17の開始時点ではカスタムAuto・インストール版Disabledになっており、今回この設定は変更していない。UACや保護デスクトップの設定を無効化していない。
- 非管理者から通常のStatus・Startが成功。実際のSCMハンドルを開いてQuery/Start/Stop権限を検証済み。今回実Stop/Startを確認。サービス移行後のWeb自己再起動・UAC表示・音声再生は未確認。以前の旧タスクでの自己再起動試験とは区別する。
- サービス移行時のネイティブ384件/47スイート、新規サービス操作8件、既存タスク14件、共有設定14件が成功。新規サービスログは共有設定14件を含む22件で、重複計上しない。ヘルパーgcovは55/56行（未実行はquote_argument末尾の閉じ括弧に付いた行45）。SCMとトークンは実移行で確認したが全経路のカバレッジではない。
- 現配信負荷の30秒測定結果は [current-stream-load-2026-10-16.md](current-stream-load-2026-10-16.md)。サービス移行前の測定で、オリジナル版との差分測定ではない。
- 連続スクロールの報告後、固定区画版を照合・再起動し、ユーザーから再接続後に固定フレーム切り替えが正常になったとの確認を得た。全12位置・全入力方式を検証したという意味ではない。証拠は `frame-switch-runtime.json`、`frame-switch-tests.log/xml`。
- 常用先 `cmake-build-custom/package` は重なりクロップ・回線回復修正を含む、背景自動段階 **1/2・1/3・1/4・1/6** の最新版。帯域境界と更新周期は維持した。
- 音声の再接続・停止後の復帰を修正した。実効予算3 Mbps以上で音声の送出予算と映像最低予算を確保できる場合に開始・復帰する。3 Mbps未満では停止する。ユーザーから再接続後に音声が戻ったとの確認を得た。帯域低下→回復での自動復帰は未確認。
- 旧版の混在は解消。サービス移行時の `package-system-service` と配置候補・常用先の162ファイルを照合した。旧 `package-overlap-crop` と旧ZIPは、変更前の倍率による配布物として残してある。
- 旧タスク **Sunshine Custom interactive launcher v2** はDisabledで保管。現在は専用サービスの公式方式のWindows Job Objectがホストを管理する。
- 旧タスク方式では通常権限からStart／Stop／Restartと停止後0プロセス・0待受を確認済み。サービス移行後は上記の検証範囲を参照する。
- Web管理画面から自己再起動した `14788 → 32308` を観測。タスクはRunningを維持し、通常権限からのStopで子ホストと全待受も消えた。その後、常用ホストを再起動した。
- AnyDeskはRunning。インストール版SunshineServiceはStopped。
- 以前のユーザー指定はインストール版Autoの維持。その後の変更元は未確認だが、2026-10-17の実測はカスタムAuto・インストール版Disabled。今回の配置では開始時の設定を保持した。カスタム版起動前に競合がないことを確認する。
- iPad実機での最新クロップ、実WANでの帯域低下→回復は未確認。

前回の文書にあった「packageが混在」「v1のまま」「package-stream-fixesが稼働中」は過去の状態。
現在は本書と今回の証拠JSONを優先する。PIDは採取時点の値であり、次回の停止対象へそのまま流用しない。

## ユーザーの方針と制約

- Windowsホスト・iPad優先。Mac・Windowsを含む通常版Moonlightからも接続できる方針。
- PC上でMoonlightを試す際は必ずウィンドウモード。フルスクリーンは禁止。
- AnyDeskを維持し、無関係なプロセスを停止しない。
- インストール版とカスタム版を同じポートで同時起動しない。インストール版の自動起動設定を勝手に変更しない。
- 共通設定を使い、ビルドごとのログイン再設定・再ペアリングを避ける。
- 管理者画面への入力に対応するため、カスタムホストは管理者権限で起動する。
- 調査・回答は異なる視点から最低3回検証するという希望がある。
- 割り切れるクロップの「分割数を1プラス」は明示仕様。端で待つ方式やカーソル強制移動へ変更しない。
- 多数の既存未コミット変更がある。reset、clean、ブランチ切替で失わない。

## 稼働先と操作

| 項目 | 値 |
| --- | --- |
| 常用exe | `D:\data\code\sunshineCustom\cmake-build-custom\package\sunshine.exe` |
| SHA-256 | `718889c39b9060e88cb1b50b6fd2b7d5b4bf403f20db3f947358387c0b7faba5` |
| 共通設定 | `C:\Users\noTempo\AppData\Local\SunshineCustom\config` |
| 現行サービス | `SunshineCustomService`、Running、LocalSystem、自動起動（今回変更なし） |
| 旧タスク | `SunshineCustom-S-1-5-21-3284364156-1134421427-1457271923-1001`、Disabled |
| タスク方式 | 現ユーザー、対話ログオン、最上位権限、トリガーなし、時間制限なし、重複起動無視 |
| 待受確認 | TCP `47984 / 47989 / 47990 / 48010` をPID `21764`が所有（採取時点） |

通常の操作はルートの `Start Sunshine Custom.cmd`、`Stop Sunshine Custom.cmd`、`Restart Sunshine Custom.cmd`。
通常操作は登録済み専用サービスを優先し、UACを再表示しない。実行中のファイルは上書きしない。
Webの自己再起動ボタンは「トラブルシューティング」内の黄色い「サンシャインを再起動」。

```powershell
& .\scripts\custom\control-sunshine-task.ps1 -Action Status
& .\scripts\custom\control-sunshine-task.ps1 -Action Stop
& .\scripts\custom\control-sunshine-task.ps1 -Action Start
& .\scripts\custom\control-sunshine-task.ps1 -Action Restart
```

## 配置と証拠

2026-10-17: 50px版を専用サービスの停止・全ホストとラッパー終了・4ポート解放・全旧ファイル排他オープン確認後に配置。全ファイルのハッシュと、起動後の固定サービス起動先・検証済みラッパーのハッシュ・親サービス・対話セッション・単一起動・4待受を照合。SYSTEMホストの直接パス照会は権限で拒否されるため、サービスの固定起動方式から照合した。AnyDeskと両サービスの起動設定は維持。Moonlight Internet Hosting ToolのGSv6FwdSvc（PID13804）がIPv6で3ポートを中継しているため、その待受は維持してSunshine本体と区別した。旧版の保存先は `package-before-crop-20261017-033737`。

ネイティブ385件/47スイート、Web72件が成功。境界計算follow_sectionは実行行26/26、ビューポートヘッダー94/94（99オブジェクト統合）。変更全体・全分岐の100%を示す数値ではない。今回の全サイズ走査は端判定-1/0/1/49/50/200pxを含む。カバレッジの実数と範囲は `crop-margin-50-coverage.json` を参照。

| 最新の証拠 | 内容 |
| --- | --- |
| `crop-margin-50-deploy.json` | 配置・非管理者Stop/Start・起動PIDと親サービス・起動設定保持 |
| `crop-margin-50-final-runtime.json` | 文書同期後の最終ファイル照合と稼働状態 |
| `crop-margin-50-final-manifest.json` | 最新パッケージの全ファイルハッシュ |
| `crop-margin-50-native-tests.log/xml` | 385件成功 |
| `crop-margin-50-web-tests.log` | 72件成功 |
| `crop-margin-50-coverage.json` | 今回のビューポート境界計算の実行行 |

以下は過去の配置記録。

最新のサービス移行では、ホスト停止・ポート解放・全ファイルの排他アクセスを確認後に162ファイルを配置した。配信exeのSHA-256は音声修正版と同じで、サービスラッパーと起動資材を更新した。正常な旧版は `package-before-system-20261016-163421` に保管。

最新の音声修正は既存v2タスクを使って停止・配置・再起動し、タスクの再登録や共通設定の変更は行っていない。
全ホスト終了・全待受解放・旧配置全ファイルの排他オープンを確認した後に差し替え、157ファイルを照合した。
正常な変更前パッケージは `package-before-audio-20261016-020310` に保存した。倍率変更時のバックアップも残してある。

以下は初回のv1→v2移行の記録。再開時はインストール版 `C:\Program Files\Sunshine\Sunshine.exe` が稼働していた。
管理者権限でサービスの子であることを照合し、AnyDeskを維持してSunshineServiceだけを停止した。
全Sunshineの終了・4ポートの解放・旧配置先全ファイルの排他オープン成功を確認してから、
準備済みディレクトリを常用先へ配置し、156ファイルを再検証した。ロックエラーを無視する差し替えは行っていない。
旧v1タスクは所有者・全引数・権限・実行先が一致することを確認してからv2へ移行した。

証拠はすべて `cmake-build-custom/` 内。認証内容・秘密鍵・クリップボード本文を含めていない。

| ファイル | 内容 |
| --- | --- |
| `system-service-migration.json` | 最新の移行成功・SYSTEM SID・対話セッション・親サービス・単一起動・162ファイル照合 |
| `system-service-non-admin-control.json` | 非管理者Status/Start成功、SCMのQuery/Start/Stop権限、4待受、旧タスクDisabled |
| `system-service-manifest.json` | 最新配布162ファイルのハッシュ |
| `service-native-tests.log/xml` | ネイティブ384件/47スイート成功 |
| `service-script-tests.log` | サービス8件＋共有設定14件成功 |
| `service-legacy-task-tests.log`、`service-shared-config-tests.log` | 既存タスク14件・設定共有14件再実行成功 |
| `service-coverage.json` | 新規サービス引数ヘルパー55/56実行行 |
| `audio-recovery-deploy.json` | 最新音声修正の配置成功、実行パス・ハッシュ・PID・サービス状態 |
| `audio-recovery-final-runtime.json` | 文書同期後の全157ファイル照合と最終稼働確認 |
| `audio-recovery-manifest.json` | 最新配布157ファイルのハッシュ |
| `audio-recovery-tests.log/xml` | 最新ネイティブ379件の成功記録 |
| `audio-recovery-web-tests.log` | 最新Web 72件の成功記録 |
| `audio-recovery-coverage.json` | 選択した音声・予算関連7ヘルパー64/64実行行 |
| `background-steps-deploy.json` | 前回の倍率変更の配置記録 |
| `background-steps-manifest.json` | 最新配布157ファイルのハッシュ |
| `background-steps-tests.log/xml` | 前回のネイティブ369件の成功記録 |
| `background-steps-web-tests.log` | 前回のWeb 72件の成功記録 |
| `background-steps-coverage.json` | 前回変更した自動段階選択メソッド19/19実行行 |
| `migration-before.json` | 移行直前の管理者読み取り。インストール版、Auto、旧v1、gcov残存なし |
| `migration-package-manifest.json` | 配布156ファイルのパス・サイズ・SHA-256 |
| `migration-status.json` | 停止・ロック確認・配置検証・v2起動の成功記録 |
| `migration-original-task.xml` | 移行前のv1タスク定義 |
| `non-admin-task-control.json` | 非昇格Restart、PID変更、4ポート、サービス状態 |
| `self-restart-task-control.json` | Web自己再起動後の子ホストと非昇格Stopの成功記録 |
| `migration-final-runtime.json` | 最終ホスト数・ポート所有者・タスク一致・配置exeのハッシュ・サービス状態 |
| `migration-coverage.json` | 今回再生成した関連15オブジェクトの主要ヘッダー行カバレッジ |

最終の管理者読み取りはUACがキャンセルされたため未実施。`migration-after.json` は作成されていない。
最上位タスクの登録一致と実運用は確認したが、最終PIDの昇格トークン・ジョブ所属の直接再読取は未確認として残す。

旧混在ディレクトリは `package-before-migration-20261016-000707` として保管した。正常な復旧用配布版ではない。
正常な旧配布版 `package-stream-fixes` は変更していない。
ローカルの `migrate-supervised-task.ps1` はこのPC・今回の配置候補専用。今後の汎用更新へそのまま流用しない。

前回の配置失敗ではexeロックがあった。停止・ロック解放確認なしでリネームする案は自動承認レビューで拒否された。
今回は停止と全ファイルの排他アクセスを先に検証した。前回の失敗JSONや拒否記録を今回の失敗と混同しない。

## 最新版に含まれる機能

### 重なりのある固定区画クロップ

実サイズSが表示サイズCより大きい辺では `N = floor(S / C) + 1`。収まる辺は1区画。
原点は `floor(i * (S - C) / (N - 1))`。退化サイズでは重複原点と整数オーバーフローを避ける。

- `3840 → 2560`: 2区画、原点 `0, 1280`。
- `3840 → 1280`: 4区画、原点 `0, 853, 1706, 2560`。
- `2160 → 960`: 3区画、原点 `0, 600, 1200`。
- `3840×2160 → 1280×960`: 横4×縦3の12位置。

重なり内で切り替えに遊びを残し、絶対マウス入力でも次の区画へ到達できる。
タッチ・ペン接触中は固定し、全接触解除後に追従を再開する。対象は選択中の1モニター。横断は未実装。
中心処理は `src/video_viewport.h`、入力は `src/input.cpp`、D3D11は `src/platform/windows/display_vram.cpp`。
詳細は [viewport-crop.md](viewport-crop.md)。最新方式のiPad実機操作は未確認。

### 回線回復後も背景が粗いままになる問題

500ms窓に制御応答がないだけで、回復の10秒タイマーをリセットしていた問題を修正した。
1秒ごとの健全な応答でも回復し、2秒以上の途絶・損失・RTT上昇・不正な報告等では回復を止める。
新しい応答と正のRTTを要求し、10秒安定後に概ね2秒ごと5%ずつ戻す既存方針を維持する。

C++制御の失敗→修正後成功、映像設定の適用、GPU加工前画素からの細部回復という3視点で検証済み。
実回線の全原因を確定したものではなく、今回の配置版によるWAN復旧は未確認。
対象は背景の細かさ・更新周期・FPSで、配信全体の要求解像度を変更する処理ではない。
ログは `low_bandwidth_decision direction=recover` と `low_bandwidth_apply` を照合する。
詳細は [recovery-sparse-feedback.md](recovery-sparse-feedback.md)。

### 音声の再接続・帯域回復

接続時に低帯域モードだけを見て音声を無効化し、そのまま終了まで待機していた判定を修正した。
音声予算を映像から先に確保し、映像エンコーダーの設定反映成功後に音声を再開する。
停止・再開の世代番号で古いPCM・Opus・送出パケットを除外し、音声FECの区切りも揃える。
ユーザーが明示的にStream Audioを無効にした場合は再開しない。

合成PCMを本番Opusワーカーで符号化・復号する停止→再開テスト、再接続時の設定、予算境界、
エンコーダー設定失敗時の音声抑止を確認した。ユーザーから実クライアントの再接続後に音声が戻ったとの確認を得た。帯域低下→回復の一連確認は未完了。
詳細は [low-bandwidth.md](low-bandwidth.md)。

### 管理者タスク

`scheduled-task.ps1`、`register-sunshine-task.ps1`、`control-sunshine-task.ps1`、
`run-sunshine-task.ps1`、`task-job.cs` が中心。固定exeをサスペンド生成→ジョブ所属→再開する。
自己再起動した子も監視し、監視終了でまとめて停止する。配信アプリが明示的に要求するジョブ離脱は許可する。
今回、実タスクへの移行と実ホストでの通常操作・自己再起動後の停止まで検証した。
詳細は [scheduled-task.md](scheduled-task.md)。

## 検証範囲

| 対象 | 結果・証拠 |
| --- | --- |
| 最新ソースのネイティブ対象テスト | 今回384件 / 47スイート、失敗・スキップ0。`service-native-tests.log/xml` |
| Web UI全テスト | 音声修正時72件 / 11ファイル。サービス移行はUI変更なしで再実行していない。`audio-recovery-web-tests.log` |
| クロップ・鮮明領域のUI対象2ファイル | 前回45/45行、47/47分岐、18/18関数 |
| タスク・監視ジョブ | 初回タスク移行時14件成功。`task-launcher-tests.log` |
| 設定共有・既存起動 | 初回タスク移行時14件成功。`shared-config-tests.log` |
| 実機タスク運用 | 非昇格Start/Stop/RestartとWeb自己再起動後Stop成功 |
| 行カバレッジ | 今回の選択した音声・予算関連7ヘルパーは64/64行。前回select_settingsは19/19行。タスク移行時はviewport 93/93、low_bandwidth 83/83、背景保持67/67 |
| 空白・パッチ整合性 | `git diff --check` 成功 |

384件はフィルター対象であり、全ネイティブテスト完走ではない。音声の再接続・世代管理・予算確保・Opus停止復帰を追加検証した。
今回の行カバレッジは選択した7ヘルパーに限定する。キャプチャ・ネットワーク統合経路、変更全体・全分岐の100%は確認していない。
古いルート直下gcovには旧出力と途中ファイルがある。今回の `coverage-audio-recovery/` 内の9件は読取完了。古いgcdaは `coverage-before-audio-20261016-015959` に退避して再測定した。
GPU検証はWARP上の本番D3D11変換器による画素読み戻しであり、実iPad・実WAN・実GPUエンコードの代替ではない。
`recovery-repro.log` は意図した修正前の失敗記録。

## 次回の作業

1. 本書とAGENTS.mdを読み、既存未コミット変更を保持する。現PID・実行パス・4ポート・タスク・AnyDeskを再確認する。
2. PC再起動後などでインストール版が稼働している場合は、カスタム版と競合させない。Auto設定はユーザーの指定なので変更しない。
3. Moonlightを再接続してUAC保護画面の表示と入力を確認する。サービス移行後の音声と、必要に応じて帯域低下→回復後の自動復帰を確認する。移行前の再接続音声はユーザー確認済み。
4. iPadから `3840×2160 → 1280×960` で接続し、横4・縦3の全位置を往復する。絶対マウス・直接タッチ・接触中固定・キャレット追従を確認する。
5. 実回線で帯域低下→回復を再現し、recoverログと適用予算・背景scale・更新周期・FPSを記録する。クライアント設定値と実送出量を区別する。
6. 必要に応じて主要ヘッダー以外の変更範囲・分岐カバレッジを測る。成功した範囲だけを資料へ追記する。

今回のサービス移行を繰り返す必要はない。実機・WANで不具合が出た場合は現象とログから切り分ける。

## ビルド・Git・関連資料

- ブランチ `codex/cursor-background-loop`、HEAD `9a7fda754926767f862c2642b53737d2957dadf5`。コミット・pushしていない。
- origin `https://github.com/usm916/Sunshine.git`、upstream `https://github.com/LizardByte/Sunshine.git`。
- `third-party/moonlight-common-c`: `ae67fe009386d536569fecc34ba156d581309b54`。
- `third-party/lizardbyte-common`: `f9d91e1d29b7473f58e43acde4579da4e56c4abe`。
- 今回 `uv sync --locked` 成功。`uv run --locked lb-update-clang-format`も実施し、整形による追加差分なし。サービス変更の整形ログは `service-format.log`。
- Windowsのビルド・テストはMSYS2 UCRT64を使用し、ビルド先は `cmake-build-` で始める。

```powershell
& 'C:\msys64\msys2_shell.cmd' -defterm -here -no-start -ucrt64 -c 'bash cmake-build-custom/build-audio-recovery.sh'
& 'C:\msys64\msys2_shell.cmd' -defterm -here -no-start -ucrt64 -c 'bash cmake-build-custom/web-audio-recovery.sh'
& 'C:\msys64\msys2_shell.cmd' -defterm -here -no-start -ucrt64 -c 'bash cmake-build-custom/test-task-launcher.sh'
```

ローカルの再現スクリプトは内容を確認してから使う。テスト実体は `cmake-build-custom/tests/test_sunshine.exe`。
通常のビルドスクリプトで稼働中 `package` を上書きしない。ステージへ出し、停止・ロック解除後に配置する。

- [system-service.md](system-service.md): 現在の専用LocalSystemサービスと通常操作。
- [README.custom.md](../../README.custom.md): 本流master・独自custom/mainの方針。未保存変更があるままブランチを切り替えない。
- [cursor-focus.md](cursor-focus.md)、[caret-focus.md](caret-focus.md)、[stream-fixes.md](stream-fixes.md): 鮮明領域・背景保持・キャレット追従。
- [low-bandwidth.md](low-bandwidth.md)、[low-bandwidth-validation.md](low-bandwidth-validation.md): 可変予算・FPS・音声復帰。実効予算3 Mbps未満では音声停止し、予算確保後に復帰。
- 背景自動段階: <=250kbpsは1/6・5秒、251〜999は1/4・3秒、1000〜1999は1/3・1秒、2000〜2999は1/2・5変換フレーム、>=3000は加工解除。
- [clipboard-validation.md](clipboard-validation.md): テキスト拡張は両側対応が必要。通常クライアントでのクロップ・映像とは条件が異なる。
- [disconnect-diagnostics.md](disconnect-diagnostics.md)、[unstable-link-research.md](unstable-link-research.md)、[vnc-low-bandwidth-research.md](vnc-low-bandwidth-research.md)。
- クライアントは `D:\data\code\moonlightCustom`、共通ライブラリ作業用は `D:\data\code\moonlightCommonCustom`。今回は変更・ビルドしていない。
- マルチモニター横断、独立したWindows入力デスクトップ、画像・ファイル転送は今回の実装範囲外。

共有ログへ認証ヘッダー、秘密鍵、ペアリング状態、クリップボード本文を含めない。


## 2026-10-18：PINを引き継ぐ部分更新ZIP

`cmake-build-custom/Sunshine-recovery-fix-20261018-binary-update.zip` を追加。旧exe `a3e82f2a...` から回復修正版 `c7e1056d...` への差分。本体以外の実行時ファイルが全て同一であることを確認した。設定・ペアリング・証明書・秘密鍵は含まない。サービスを停止して登録済みフォルダーへ上書きし、同じ設定保存先で再開する。手順は [binary-update.md](binary-update.md)。TANへの適用は未実施。
