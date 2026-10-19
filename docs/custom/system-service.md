# LocalSystemサービスによるカスタム版の起動

2026-10-16 16:34 JST、このPCで移行成功。実トークンSID S-1-5-18・対話セッション1・単一起動・4待受・共通設定を照合済み。旧タスクはDisabled。通常権限のStatus/Startと、SCMのStart/Stop/Query権限も確認した。実UAC画面・サービス移行後の音声・Web自己再起動は未確認。2026-10-17、通常権限での実Stop/Startによる50px版配置に成功。開始時点の起動設定はカスタムAuto・インストール版Disabledで、今回変更なし。

最初の登録は監査用SACLを含む記述子を拒否して旧版へ復旧した。修正版は既存DACLを保持して必要な制御権限だけ追加し、SACL・所有者・グループの変更は要求しない。

カスタム版は専用の `SunshineCustomService` を使い、公式サービスと同じ方法でLocalSystemトークンをログイン中のコンソールセッションへ割り当てて起動します。
UACの保護デスクトップのキャプチャに必要な権限を持たせます。UACや保護デスクトップの設定を無効化する変更は行いません。

## 登録と操作

初回は `Register Sunshine Service.cmd` を実行します。Windowsの管理者承認が必要です。
既存の共有設定を `--config-dir` で固定し、認証・ペアリング・秘密鍵・アプリ設定・ログを同じ保存先へ向けます。

- サービス名: `SunshineCustomService`
- アカウント: `LocalSystem`
- 登録の既定起動方法: 手動（現在のこのPCはAuto。今回変更なし）
- 実行ファイル: 選択したpackageの `tools/sunshinesvc.exe`
- ホスト: 同じpackage直下の `sunshine.exe`
- 共有設定: 通常は `C:\Users\noTempo\AppData\Local\SunshineCustom\config`

登録後は通常の `Start Sunshine Custom.cmd`、`Stop Sunshine Custom.cmd`、`Restart Sunshine Custom.cmd` を使用できます。
Startは登録済みの専用サービスを優先します。Stop/Restartは実行ファイル・設定パス・サービス名・LocalSystemアカウント・説明が一致する専用サービスだけを操作します。
登録したユーザーにはサービスの照会・開始・停止権限だけを追加し、サービス設定変更や任意引数を受け付ける実行窓口は作りません。

インストール版の `SunshineService` は別のサービスです。初回移行時はユーザー指定のAutoを維持しました。2026-10-17の読み取りではDisabledで、今回の更新ではその状態を保持しています。
PC再起動後などにインストール版が動作している場合、専用サービスのStartは競合として拒否します。両方を同じポートで起動しないでください。

## 別PCでの登録失敗の確認

Windowsの「サービス」（Win+R → `services.msc`）での表示名は **Sunshine Custom**、内部名は `SunshineCustomService`。`sc.exe query SunshineCustomService` の1060は未登録を意味する。PC再起動で登録されることはない。

新しいPCでは、先にpackageのあるフォルダーで共有設定を準備する。既存の設定は上書きしない。

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File ".\scripts\custom\start-sunshine.ps1" -ExecutablePath ".\sunshine.exe" -PrepareOnly
powershell.exe -NoProfile -ExecutionPolicy Bypass -File ".\scripts\custom\register-sunshine-service.ps1" -ExecutablePath ".\sunshine.exe"
```

2026-10-17、昇格したPowerShellにもプロセス限定のExecutionPolicy Bypassを明示するよう修正した。マシンやユーザーの実行ポリシー設定は変更しない。UAC後のエラーは一時ファイル経由で呼び出し元に返し、成功・失敗・UACキャンセルの後に一時ファイルを削除する。失敗時は終了コードだけでなく、その後に表示されたエラー全文を確認する。別の管理者アカウントを入力した場合は元のアカウントとのSID不一致を明示して停止する。

この修正はエラー伝達と昇格時の実行条件を改善するものであり、別PCで報告された終了コード1の原因を特定したものではない。登録の既定は手動であり、成功後に開始が必要。自動起動にする場合は「サービス」→ Sunshine Custom → プロパティ → 全般タブ → スタートアップの種類 → 自動。

## 旧タスクからの移行

1. 新しいラッパーと設定引数・停止制御をビルド／テストする。
2. 旧タスク定義と既存パッケージを保存し、専用サービスを手動起動で登録する。
3. 旧タスクを停止し、全ホスト終了・対象ポート解放・全ファイルのロック解放を確認して配置する。
4. 専用サービスから起動し、ホストのLocalSystem SID、ユーザーセッション、単一起動、4待受、共通設定を照合する。
5. 正常起動後は旧タスクを無効化して、二重起動を避ける。タスク定義は復旧用に残す。

移行で配信は一度切断されます。共通設定とペアリングの内容は変更しません。

## 確認範囲

Windowsでは、管理者ユーザーのHighestトークンとLocalSystemトークンは異なります。
[DXGIの仕様](https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_2/nf-dxgi1_2-idxgioutput1-duplicateoutput)では、保護デスクトップへのアクセスにはLocalSystemが必要です。
ネイティブテストではUnicode引数、クォート、設定共有、既定の公式サービス動作、未知・重複オプションの拒否を検証します。
PowerShellテストではサービス照合、操作順序、待機上限、インストール版との競合拒否、旧ランチャーへのフォールバックを検証します。
最終的なUAC画面の表示・入力はMoonlight実機で確認してください。
