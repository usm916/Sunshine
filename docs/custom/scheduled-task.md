# 初回だけUACで登録する管理者起動

現在のこのPCは [LocalSystemサービス方式](system-service.md) へ移行済み。以下は旧タスク方式の記録で、タスクは復旧用に無効化して保管している。

## このPCの導入状況（2026-10-16）

**このPCへのv2登録と常用版配置は完了**しました。`cmake-build-custom/package` は最新の重なりクロップ・回線回復修正版です。
最新ZIP・ステージ・配置候補の156ファイルを照合し、ホスト停止・ポート解放・配置先全ファイルの排他アクセスを確認してから移行しました。
通常権限からStart／Stop／Restartが成功し、Web管理画面の自己再起動で子ホストへ変わった後もタスクはRunningを維持しました。
その子も通常権限からのStopで終了し、Sunshine 0プロセス・対象待受0を確認した後、再起動しています。
証拠と現状態は[セッション引き継ぎ](SESSION-HANDOFF.md)を参照してください。

AnyDeskは維持しています。インストール版SunshineServiceは停止しましたが、ユーザーの指定で起動設定はAutoのままです。
PC再起動後にインストール版が起動している場合は、カスタム版を同じポートへ重複起動しないでください。


UACの承認をキャッシュするのではなく、固定したSunshineを管理者権限で起動できるタスクを一度登録する。
Windowsのタスク機能を使用し、ユーザーのパスワードは保存しない。

## このチェックアウトの操作

1. `Register Sunshine Task.cmd` を起動し、一度だけWindowsの管理者確認を許可する。登録だけでは稼働中ホストを終了しない。
2. 最初の移行時は、従来の手動起動Sunshineを終了する。同じ共通設定で二重起動しない。
3. `Start Sunshine Custom.cmd` で起動する。登録内容が一致すればUACを表示しない。
4. `Stop Sunshine Custom.cmd` で停止、`Restart Sunshine Custom.cmd` で再起動する。

既定の実行先は `cmake-build-custom/package/sunshine.exe`、共通設定は `%LOCALAPPDATA%\SunshineCustom\config`。
このパスのままビルドを差し替えれば再登録は不要。実行中のexeを上書きする前に停止CMDを使う。
ビルドまたは配置に失敗した場合は、正常な配布版を配置してから起動する。
登録された起動先・設定パスを変える場合は、停止して登録を解除し、新しいパスで登録し直す。

タスクは `SunshineCustom-<現在ユーザーのSID>`。ログオン中の現在ユーザーで、最上位の権限で実行する。
ログオン前の起動や自動起動トリガーは設定しない。通常の手動起動と同じ対話デスクトップを使用する。
タスクは固定した監視用PowerShellを非表示で実行し、Sunshineと自己再起動した子プロセスをWindows Job Objectで一括管理する。ホストはサスペンド状態で作り、ジョブへ所属させてから開始する。タスク停止時に監視プロセスのジョブハンドルが閉じると、再起動後のホストも終了する。配信アプリが明示的に使うジョブ離脱は許可し、ホスト自身の通常の再起動はジョブ内に保持する。

Sunshineを直接タスクの実行先にした初期試作では、Web UI等の自己再起動で子プロセスが管理から外れたため、この方式へ修正した。
重複起動は無視し、既定の3日間の実行時間制限を無効化する。AnyDeskやインストール版サービスは操作しない。

起動時にタスクの所有者、対話ログオン・最上位設定、監視プロセス、作業ディレクトリ、固定したホストと設定を含む全引数を照合する。
タスクがない場合や別の実行先の場合は、従来のUAC起動を使う。
追加の `-SunshineArguments` がある場合もタスクの引数を書き換えず、直接起動する。
タスクの実行拒否・スケジューラーのエラーを、成功として扱わない。

## PowerShellでの操作・解除

```powershell
& .\scripts\custom\register-sunshine-task.ps1 -PrepareOnly
& .\scripts\custom\control-sunshine-task.ps1 -Action Status
& .\scripts\custom\control-sunshine-task.ps1 -Action Stop
& .\scripts\custom\register-sunshine-task.ps1 -Remove
```

解除時は必要に応じてUACが表示される。タスクが稼働中・登録内容が異なる場合は解除を拒否する。
別のexeや共通設定を指定する場合は、登録・起動・制御・解除で同じ `-ExecutablePath` と `-ConfigDirectory` を渡す。
別アカウントの管理者資格情報による登録は拒否し、元のユーザーの設定パスを維持する。

## 検証

設定共有・管理者起動の既存テストに加え、Windowsの実タスク定義オブジェクトを読み取り専用で作り、
対話ログオン、最上位権限、時間制限なし、トリガーなし、重複起動無視を確認する。
引数はWindowsの実コマンドラインパーサーで往復し、日本語、空白、引用符、末尾のバックスラッシュを検証する。
制御操作でのタスク不一致、所有者不一致、スケジューラーの拒否、停止タイムアウトでは別ホストを起動しないことを検証する。
実Windowsの子プロセスを使い、親が終了して子だけになっても管理を維持し、ジョブを閉じると子も終了することを確認する。

## 仕様の根拠

- [Microsoft: Security Contexts for Tasks](https://learn.microsoft.com/en-us/windows/win32/taskschd/security-contexts-for-running-tasks)
- [Microsoft: New-ScheduledTaskPrincipal](https://learn.microsoft.com/en-us/powershell/module/scheduledtasks/new-scheduledtaskprincipal)
- [Microsoft: New-ScheduledTaskSettingsSet](https://learn.microsoft.com/en-us/powershell/module/scheduledtasks/new-scheduledtasksettingsset)

- [Microsoft: Job Objects](https://learn.microsoft.com/en-us/windows/win32/procthread/job-objects)

2026-10-15: タスク・監視ジョブ14件、既存の設定共有14件が成功。ジョブの実プロセス試験と、監視経由のネイティブ引数の記録を含む。

2026-10-16: 上記14件ずつを再実行して成功。実v2タスクへの移行と、非昇格呼び出し元からの操作、実ホストの自己再起動後の停止も成功。
