# ビルド間でログイン・ペアリング情報を共有する

実行ファイルと設定を分離するランチャーを追加した。共通の保存先は `%LOCALAPPDATA%\SunshineCustom\config`。管理ログイン、Moonlightのペアリング、ホスト証明書、アプリ一覧、Web UIの設定をビルドごとに作り直す必要がない。

## このチェックアウトでの起動

リポジトリ直下の `Start Sunshine Custom.cmd` を起動する。既定では `cmake-build-custom/package-cursor-focus/sunshine.exe` を使用する。

共通フォルダがまだ存在しない初回だけ、選んだ実行ファイルの隣の `config` フォルダをコピーする。現在の試用版で設定した管理パスワードをそのまま使える。元の設定フォルダは残る。共通フォルダがある場合は上書きしない。

起動中のSunshineを終了してからビルドを切り替える。同じ設定ファイルを複数のSunshineから同時に書き換えない。インストール版のサービス・設定は変更しない。

## 配布フォルダでの起動

ビルドスクリプトが配置する `Start Sunshine.cmd` を使う。どのビルドの配布フォルダでも同じ共通保存先を参照する。`sunshine.exe` を直接起動すると本流の既定どおり実行ファイル隣の `config` を使用するため、共有する場合はランチャーから起動する。

## 別の実行ファイル・保存先

```powershell
& .\scripts\custom\start-sunshine.ps1 -ExecutablePath 'D:\path\to\sunshine.exe'
& .\scripts\custom\start-sunshine.ps1 -ExecutablePath 'D:\path\to\sunshine.exe' -ConfigDirectory 'D:\SunshineSettings'
```

`-ConfigDirectory` で変更した場合は、切り替える各ビルドでも同じ場所を指定する。新しい空の保存先を指定すると、その実行ファイル隣の設定から初回移行する。

ランチャーはSunshineが既に持つ `file_state`、`credentials_file`、`pkey`、`cert`、`file_apps`、`log_path` の指定を使用する。実行ファイルや通信プロトコルに変更はない。設定ファイルや証明書を配布ZIPやGitへ含めない。

## 検証

既存設定の初回移行、証明書とペアリング情報の保持、別ビルドからの再利用と非上書き、空の初期設定、空白・日本語を含むパス、存在しない実行ファイル・保存先の拒否を検証する。`-PrepareOnly` は設定を準備して起動引数を返し、Sunshineを起動しない。

2026-10-14：設定共有の10件のテストが成功。現在の試用版から共通フォルダへ初回移行し、管理ログインのハッシュとsalt、ホストとペアリング情報、証明書と秘密鍵が一致することを確認した。起動中のSunshineは終了していない。次回ランチャーで起動した時点から共通設定を使用する。
