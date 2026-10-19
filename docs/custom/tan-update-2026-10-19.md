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

# TANへの回復修正版適用（2026-10-19）

TANの登録済みフォルダー `D:\data\code\sunshineCustom\cmake-build-custom\package` へ、2026-10-18の回復修正版を適用し、Sunshine Customサービスを停止・開始した。

- 本体SHA-256：`c7e1056df2ae049e845990e178e76de7b3ed43ee9c917c79ff96dcbfea96f8d3`。
- 旧本体SHA-256：`a3e82f2a0f5807031e02a47682e378d105866a25018b5af33ae660a67d5c136b`。
- サービス：`SunshineCustomService`、Running。確認時のサービスPID19996／ホストPID29104。旧ホストPID11164は終了。
- 本体・版情報・説明書の9ファイルを差し替え、141個の他の実行時ファイルは互換性の照合のみ。更新前のファイルを `cmake-build-recovery-fix/deployment-20261019-0f4f4b94/before` へ退避。
- サービス定義、起動方式、設定保存先は変更しない。保存先は引き続き `C:\Users\noTempo\AppData\Local\SunshineCustom\config`。
- ホストの応答IDと実際の状態ファイル、TLS証明書と実際の保存済み証明書がそれぞれ一致。既存のMoonlight登録3件を保持。状態ファイル・証明書・秘密鍵・設定・アプリ一覧の最終更新日時はいずれも今回の適用前である。
- ホスト自身のTCP47984・47989・47990・48010の待受を確認。serverinfoはHTTP200、Web UIは未認証のアクセスに401を返し、応答と認証の保護を確認。
- インストール版SunshineServiceはStoppedのまま。AnyDesk、GSv6FwdSvcの状態・PID・起動方式は更新前と同じ。

## 設定を確認する際の注意

このCodexデスクトップから直接読むAppDataには、古い仮想化コピーが見える。通常権限・昇格済みPowerShellで同じドライブパスを読んでも、サービスの実ファイルと異なる場合があった。実サービスの起動引数は正しく共通設定を指していた。

今回は管理者権限で `\\localhost\C$\Users\noTempo\AppData\Local\SunshineCustom\config` を読み、サービスの実ファイルと応答ID・証明書を照合した。このUNCパスは診断だけに使用し、サービスの `--config-dir` は変更していない。古いコピーを実保存先へ上書きしない。

## 証拠と確認範囲

`cmake-build-recovery-fix/tan-deployment-result.json`、`tan-identity-verified.json`、退避フォルダーの `health.json` に結果を保存。初期の直接AppDataハッシュは仮想化コピーについての結果であり、実サービスの状態ファイルの更新前後ハッシュ比較ではない。

今回、Moonlightクライアントからの実映像再接続試験や移動回線試験は行っていない。以前の409件のネイティブテストとループバック回復開始の検証済み本体を、その同じハッシュで配置した。
