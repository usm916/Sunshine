# PINを引き継ぐ部分更新

Sunshineの本体と、ペアリング・設定の保存先は別である。現在ペアリングしているホストと同じ状態ファイル・証明書・秘密鍵を使い続ければ、本体の更新だけでPINを再登録する必要はない。

## 引き継ぐファイルと起動方法

カスタム版のサービス／ランチャーは、既定で `%LOCALAPPDATA%\SunshineCustom\config` を共通保存先にする。サービス登録時に別の保存先を指定した場合は、その登録先を継続する。LocalSystemサービスも、登録済みの `--config-dir` を使用する。

| ファイル | 内容 |
| --- | --- |
| `sunshine_state.json` | ホストID、Moonlightの登録、Web UIのログイン情報 |
| `credentials/cakey.pem`、`credentials/cacert.pem` | ホストの秘密鍵と証明書 |
| `sunshine.conf`、`apps.json` | 設定とアプリ一覧 |

更新時はこれらを残す。配布物へ個人の状態ファイルや秘密鍵を入れない。

新しい展開先で `sunshine.exe` を直接起動すると、そのフォルダー隣の `config` を使用する。保存先が変わり、別のホストID・証明書が生成された場合にはPIN登録が必要になる。共通保存先を使用する `Start Sunshine.cmd`、または登録済みの **Sunshine Custom** サービスから起動する。

## 2026-10-19の移動回線・第2修正

最新の部分更新は `cmake-build-custom/Sunshine-mobile-recovery-v2-20261019-binary-update.zip`。既存の実行ファイルのみ差し替え、版情報・説明書も同梱する。DLL・Web画面・シェーダー・サービス用プログラム・起動スクリプト141ファイルは前回版と同一であることを確認した。個人設定・状態ファイル・証明書・秘密鍵は含まない。

- 対象の旧本体SHA-256：`c7e1056df2ae049e845990e178e76de7b3ed43ee9c917c79ff96dcbfea96f8d3`
- 更新後の本体SHA-256：`e256765a81f36540ab406a427df36d874a5cfa4b2e51a658f3fbdeb04ce6efc4`
- 内容・検証：[回復制御の第2修正](mobile-link-recovery-v2-2026-10-19.md)
- TANの適用状況：[適用記録](tan-update-2026-10-19.md)

対象と違う本体や実行時ファイルへこの差分を重ねず、互換性を確認する。以下のサービス停止・同じフォルダーへのコピー・開始の手順と、同じ `--config-dir` の継続は第2修正にも適用する。

## 2026-10-18の部分更新ZIP（前回分）

`cmake-build-custom/Sunshine-recovery-fix-20261018-binary-update.zip` は、2026-10-18の回復修正をTANの現行版へ適用するための差分である。実行に必要な差し替えは `sunshine.exe` の1ファイルだけで、版情報と変更した説明書も同梱する。

DLL、サービス用 `tools/sunshinesvc.exe`、Web画面、シェーダー、起動スクリプトは現行版と同一であることを全ファイルのSHA-256で確認した。この差分に設定・ペアリング・証明書・秘密鍵は含めない。

対象の旧 `sunshine.exe` SHA-256：

```text
a3e82f2a0f5807031e02a47682e378d105866a25018b5af33ae660a67d5c136b
```

更新後の `sunshine.exe` SHA-256：

```text
c7e1056df2ae049e845990e178e76de7b3ed43ee9c917c79ff96dcbfea96f8d3
```

別PCへ適用する場合は、そちらの旧exeが上記対象と一致することを確認する。版が異なる場合はDLLや画面ファイルの互換性をこの差分では確認できないため、完全版を既存フォルダーへ更新する方法を使う。いずれも同じ設定保存先を継続する。

## サービス登録済みの場合の手順

1. **Win+R → `services.msc` → Sunshine Customをダブルクリック → 全般タブ → 実行ファイルのパス** を確認する。`...\package\tools\sunshinesvc.exe` なら、更新先はその `package` フォルダーである。末尾の `--config-dir` が既存の設定保存先である。
2. 同じ **Sunshine Customのプロパティ → 全般タブ → 停止** を押し、状態が停止になるまで待つ。配信はこの間切断される。`sunshine.exe` の終了も **タスクマネージャー → 詳細タブ** で確認する。
3. 既存フォルダーの `sunshine.exe` と `build-info.json` を退避する。部分更新ZIPを一時フォルダーへ展開し、内容を既存フォルダーへ上書きコピーする。TANの更新先は `D:\data\code\sunshineCustom\cmake-build-custom\package`。別PCの従来の配置先は `C:\data\code\package` だが、手順1の登録内容を優先する。旧exeの確認例は `Get-FileHash -Algorithm SHA256 -LiteralPath 'C:\data\code\package\sunshine.exe'`。
4. **Sunshine Customのプロパティ → 全般タブ → 開始** を押し、Moonlightから再接続する。サービスの再登録や新しいフォルダーでのexe直接起動は行わない。

失敗時は同じサービスを停止し、退避したexeと版情報を戻して開始する。設定保存先は更新・復元のどちらでもそのまま使用する。

## 今後の更新

更新先のフォルダーと設定保存先を固定する。本体だけの変更ならexeの差し替えで済む。DLL・Web画面・シェーダー・サービス用プログラムも変更した版では、必要な差分も一緒に更新する。バイナリ差分と個人設定を分離すれば、完全版の更新でもペアリングを引き継げる。

ZIPは作成・照合済み。2026-10-19にTANの本体を差し替え、同じサービスを再起動した。ホストID・TLS証明書・既存のMoonlight登録を保持した。[適用記録](tan-update-2026-10-19.md)。別PCへの適用は未実施。
