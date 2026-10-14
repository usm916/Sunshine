# Sunshine Custom

Windowsホストを中心に、接続中の配信範囲変更、クリップボード同期、低帯域回線への対応を追加するための作業リポジトリです。

本流: [LizardByte/Sunshine](https://github.com/LizardByte/Sunshine)

## 現在の状態

- 初期ベース: `bd0cdfd2eb7d638dfa6014ba4e90bfc88fc52371`（2026-10-10取得）。
- `upstream`: 本流のリモート。
- `master`: 本流追跡用。独自の変更を加えない。
- `custom/main`: 独自機能の統合ブランチ。
- `origin`: [usm916/Sunshine](https://github.com/usm916/Sunshine)。独自変更の保存先。
- フォークの旧`master`と過去の`codex/*`ブランチは保持し、今回の管理起点は`custom/main`とする。
- `feature/clipboard-text`にWindowsのテキスト同期を実装。実験機能として両側で既定OFF。
- ビルドに必要なサブモジュールを再帰的に初期化済み。共通通信ライブラリはクライアントと同じコミットへ固定。
- `C:\msys64`へMSYS2 UCRT64を整備済み。Windows x64のReleaseビルド、57件のgtest、Windows API連携、共通Cテスト、Web UIの14テストと配布版の起動確認が成功。別端末間の配信テストは未実施。

[機能設計・実装順序](docs/custom/design.md)に対象範囲と検証項目を記録しています。本流のREADME、AGENTS.md、ライセンスは引き続き適用します。

## ホスト・クライアント・共通ライブラリ

ホストとクライアントを別リポジトリに分け、共通通信ライブラリを同じ固定コミットで参照します。

| 役割 | リポジトリ | ローカルフォルダ |
| --- | --- | --- |
| Windowsホスト | [usm916/Sunshine](https://github.com/usm916/Sunshine/tree/custom/main) | `D:\data\code\sunshineCustom` |
| Qtクライアント | [usm916/moonlight-qt](https://github.com/usm916/moonlight-qt/tree/custom/main) | `D:\data\code\moonlightCustom` |
| 共通通信ライブラリ | [usm916/moonlight-common-c](https://github.com/usm916/moonlight-common-c/tree/custom/main) | `D:\data\code\moonlightCommonCustom` |

通信仕様の基準は共通ライブラリ内の[clipboard-v1.md](third-party/moonlight-common-c/docs/custom/clipboard-v1.md)です。これは実装した実験拡張の通信仕様です。ホストのOS操作・サーバー側の送受信はSunshine、クライアントの設定UI・OS操作はMoonlight Qtで実装します。

[compatibility.json](docs/custom/compatibility.json)は検査対象のクライアントと共通ライブラリの固定コミットを記録します。両側を更新した際は、この記録も更新します。

```powershell
& .\scripts\check-custom-pair.ps1
# 別の場所にクライアントを置いた場合
& .\scripts\check-custom-pair.ps1 -ClientRoot 'D:\path\to\moonlightCustom'
# ローカルの一時リポジトリで正常系・不一致の検出をテスト
& .\tests\custom\test-pair-check.ps1
```

検査はsubmoduleの取得元・固定コミット・チェックアウトと、記録したソースの基準を確認します。`foundation`段階ではネイティブのソース・ビルド設定が本流の基準から変わっていないことも検査します。通信機能を実装した段階では`phase`を更新し、機能用のテストを追加します。この検査だけでは実際の配信・コピー動作や各OSでの互換性を保証しません。

## 独自変更の管理

機能単位のブランチを`custom/main`から作り、動作確認後にmergeします。

```powershell
git switch custom/main
git switch -c feature/runtime-crop
# 実装・テスト・コミット
git switch custom/main
git merge --no-ff feature/runtime-crop
```

クリップボードは`feature/clipboard-text`、低帯域設定は`feature/low-bandwidth`のように分けます。機能の仕様、設定名、対応クライアント、テスト状況を設計文書へ追記します。公開済みの統合ブランチはrebaseせず、履歴を保持します。

独自変更は`origin`へ保存します。初回は`git push -u origin custom/main`、以後は`custom/main`から`git push`を実行します。本流へのpushは通常の運用に含めません。GitHubの既定ブランチは旧`master`のままなので、新しい作業は`custom/main`を明示して開始します。

## 既存フォークの引き継ぎ

2026-10-10時点の旧`origin/master`は`107f6afa5cfc63c9032d54a720dff2b50c28ce0c`（2025-09-16更新）。取得した本流に対して832コミット遅れ、独自履歴は2コミットありました。過去の`codex/*`ブランチは8本です。

旧`master`の独自修正`593b5210`はmacOSの修飾キーイベント処理を変更しています。現在の本流では対象のCoreGraphics入力処理がlibvirtualhidへ置き換わっており、旧実装への差分をそのまま適用できません。このため旧`master`の履歴をmergeで継承し、競合した`src/platform/macos/input.cpp`には現在の本流の実装を採用します。macOSでの挙動が同等かどうかは実機では未検証です。

旧`master`と既存の作業ブランチを上書き・削除せず、新しい`custom/main`へ履歴と設計文書をまとめます。旧作業ブランチにある未統合の修正は今回の統合対象に含めず、必要になった時点で現在の本流と比較して評価します。

## 本流更新の取り込み

開始前に`git status --short`で作業ツリーが空であることを確認します。未コミットの変更がある場合は先にコミットするか、退避します。以下は各コマンドの成功を確認しながら実行します。

```powershell
git fetch upstream --tags
git switch master
git merge --ff-only upstream/master
git switch custom/main
git switch -c integration/upstream-YYYYMMDD
git merge --no-ff upstream/master
git submodule sync --recursive
git submodule update --init --recursive
uv sync --locked
# ビルド、テスト、実機での接続を確認
git switch custom/main
git merge --ff-only integration/upstream-YYYYMMDD
```

`YYYYMMDD`は実施日に置き換え、同日に複数回取り込む場合は別名を使います。競合は取り込みブランチで解消します。解消を取りやめる場合は、merge中に`git merge --abort`を使います。

上流のsubmodule固定コミットを尊重し、`git submodule update --remote`で無条件に最新版へ変更しません。独自のsubmodule変更が必要な場合は、取得可能なリモートと固定コミットを機能ごとに記録します。

## Windowsでのビルド

本流の[ビルド手順](docs/building.md)に従ってMSYS2 UCRT64と依存関係を準備します。ビルドディレクトリ名はAGENTS.mdに従い`cmake-build-`で始めます。

初回はサブモジュールを初期化し、Python依存関係も同期します。

```powershell
git submodule update --init --recursive
uv sync --locked
& 'C:\msys64\msys2_shell.cmd' -defterm -here -no-start -ucrt64 -c 'cmake -S . -B cmake-build-custom -G Ninja -DBUILD_TESTS=ON'
& 'C:\msys64\msys2_shell.cmd' -defterm -here -no-start -ucrt64 -c 'cmake --build cmake-build-custom'
& 'C:\msys64\msys2_shell.cmd' -defterm -here -no-start -ucrt64 -c './cmake-build-custom/tests/test_sunshine.exe'
```

ネイティブコード変更後は`uv run --locked lb-update-clang-format`を実行し、無関係な差分がないか確認します。新規・変更メソッドには本流の規約に沿ってDoxygenとテストを追加します。

## テキスト同期を使う

Web UIのInputにある`Text Clipboard Sharing`で許可方向を選び、Sunshineを再起動します。Moonlight Custom側でも`Sync text clipboard`を有効にします。既定では両側OFFです。

- `Both directions`: 双方向。
- `Host to client`: ホストの内容をクライアントへ送る。
- `Client to host`: クライアントの内容をホストへ送る。
- `Disabled`: 無効。

テキストはUTF-8で32,755バイトまでです。上限超過は切り詰めず拒否し、空・画像・ファイルは同期しません。ホスト→クライアントを許可した場合、接続時のホストのテキストも送ります。最初の1セッションだけが共有でき、切断時に購読と保留を破棄します。

[別端末間の検証手順](docs/custom/clipboard-validation.md)を用意しています。接続中の配信範囲変更と低帯域対応は後続の実装です。

## 再現用のスクリプト

MSYS2をインストール後、UCRT64ターミナルで`pacman -Syu`を実行します。コア更新でターミナル再起動を求められたら閉じて開き直し、更新がなくなるまで繰り返します。

```powershell
& 'C:\msys64\msys2_shell.cmd' -defterm -here -no-start -ucrt64 -c 'bash scripts/custom/provision-msys.sh'
& 'C:\msys64\msys2_shell.cmd' -defterm -here -no-start -ucrt64 -c 'bash scripts/custom/build-sunshine.sh'
```

ビルドスクリプトはRelease本体、対象gtest、共通Cテストを実行し、`cmake-build-custom/package`へ配布用ファイルを配置します。システムのサービスとしては登録しません。GUIトレイ用のQtも静的リンクします。

Moonlight用Qt MSVC SDKは`uv run --no-project --with py7zr python scripts/custom/install-qt.py`で公式配布元から取得できます。アーカイブのハッシュを公式HTTPSメタデータと照合して展開します。クライアントのビルドはMoonlight側の`scripts/build-custom.ps1`を使用します。

配布フォルダの`sunshine.exe`を起動するときは、同じポートを使用する既存のSunshineサービスを先に停止します。この端末では`SunshineService`が動いているため、新版の実機確認時に切り替えが必要です。ビルドスクリプトは稼働中のサービスを変更しません。

現在の本流はWindowsの仮想入力にVirtual HID Broker（最小版`2026.914.1218.10`）を使用します。キーボード・マウス・仮想ゲームパッドの実機検証には本流のインストール手順に従って必要なBrokerを用意してください。クリップボードのWindows APIテストはBrokerを使わずに実行できます。

## マウス周辺の画質優先

WindowsのD3D11ハードウェア変換向けに、表示倍率を保ってマウス周辺だけ細部を残す実験機能を追加しています。Web UIのAudio/Videoで「Preserve Detail near the Cursor (Experimental)」を有効にし、鮮明部の半径と背景セルサイズを設定します。既定OFF、保存後に再起動が必要です。既存Moonlightで受信できます。

使い方、1Mbps回線の試験候補、比較表と検証の境界は[マウス周辺の画質優先](docs/custom/cursor-focus.md)を参照してください。配信範囲の切り出しとは別の機能です。

## ビルド間の設定共有

リポジトリ直下の `Start Sunshine Custom.cmd`、または配布フォルダの `Start Sunshine.cmd` から起動すると、管理ログイン・Moonlightのペアリング・Web UI設定をビルド外の共通フォルダで再利用します。初回は選択したビルドの既存設定を引き継ぎます。[起動方法と保存先](docs/custom/shared-config.md)を参照してください。

マウス優先の新しい設定では、矩形の幅×高さを指定でき、背景を小区画ずつ更新する一巡の時間または更新停止を選べます。更新停止でも、マウスが通った部分の最新内容を保持します。Web UIのAudio/Videoから変更し、保存・再起動後に適用します。
