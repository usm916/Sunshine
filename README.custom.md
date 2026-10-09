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
- 今回は開発方針と管理基盤を追加した段階。独自機能は未実装。
- 共通通信ライブラリとPythonツール用のサブモジュールを初期化済み。他のサブモジュールはビルド前に初期化する。
- 標準パス`C:\msys64`にはMSYS2が見つからず、ネイティブビルドは未実行。

[機能設計・実装順序](docs/custom/design.md)に対象範囲と検証項目を記録しています。本流のREADME、AGENTS.md、ライセンスは引き続き適用します。

## ホスト・クライアント・共通ライブラリ

ホストとクライアントを別リポジトリに分け、共通通信ライブラリを同じ固定コミットで参照します。

| 役割 | リポジトリ | ローカルフォルダ |
| --- | --- | --- |
| Windowsホスト | [usm916/Sunshine](https://github.com/usm916/Sunshine/tree/custom/main) | `D:\data\code\sunshineCustom` |
| Qtクライアント | [usm916/moonlight-qt](https://github.com/usm916/moonlight-qt/tree/custom/main) | `D:\data\code\moonlightCustom` |
| 共通通信ライブラリ | [usm916/moonlight-common-c](https://github.com/usm916/moonlight-common-c/tree/custom/main) | `D:\data\code\moonlightCommonCustom` |

通信仕様の基準は共通ライブラリ内の[clipboard-v1.md](third-party/moonlight-common-c/docs/custom/clipboard-v1.md)です。これは未実装の仕様案です。ホストのOS操作・サーバー側の送受信はSunshine、クライアントの設定UI・OS操作はMoonlight Qtで実装します。

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
