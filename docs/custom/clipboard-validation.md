# Windowsテキスト同期の実機確認

初期対象はWindows SunshineとWindows Moonlight Qtです。共通仕様と固定コミットは`compatibility.json`を参照してください。テスト結果はビルド・単体・Windows API・別端末間の配信を区別して記録します。

## 2台での確認手順

1. カスタムSunshineを利用者のWindowsセッションで起動し、Web UIのInputにある`Text Clipboard Sharing`を`Both directions`へ変更して再起動します。
2. 別のWindows PCでカスタムMoonlightの設定`Sync text clipboard`を有効にし、通常どおりペアリングしてDesktop配信に接続します。
3. 接続前にホストでコピーしたテキストがクライアントに届くことを確認します。この初期コピーはホスト→クライアントを許可した場合だけ送ります。
4. 両端でASCII、日本語、絵文字、改行を順にコピーし、相手のローカルエディタに貼り付けて完全一致を確認します。
5. 同じ内容を連続コピーし、無限の反射や配信の遅延が起きないことを確認します。
6. UTF-8で32,755バイトと32,756バイトの内容を用意します。上限までは完全に同期し、超過した内容は切り詰めず相手の直前の内容を維持することを確認します。空・画像だけのクリップボードも相手を消しません。
7. ホストの設定を片方向に変更して再起動し、許可した方向だけが動くことを確認します。
8. クライアントOFF、ホストOFF、未対応の標準MoonlightまたはSunshineでも通常配信できることを確認します。
9. 他アプリでクリップボードを短時間占有し、解放後に反映されること、2秒超の占有は処理を打ち切ることを確認します。
10. 切断して別の内容をコピーし、再接続時に以前の保留内容を適用しないことを確認します。2つ目の同時接続は共有しないことも確認します。

実行日時、OS、GPU、3リポジトリのコミット、許可方向、成功・失敗と再現条件を記録します。クリップボードの実内容をログやGitへ保存しないでください。

## ビルドと自動テスト

ホストは`C:\msys64\msys2_shell.cmd -defterm -here -no-start -ucrt64 -c 'bash scripts/custom/build-sunshine.sh'`でビルドと対象gtest・共通Cテストを実行します。gtestの結果は`cmake-build-custom/clipboard-tests.xml`へ出力します。

クライアントはMoonlightリポジトリで`& .\scripts\build-custom.ps1`を実行します。共通Cテスト、Qt状態管理テストを実行し、`build/deploy-custom-x64-release`へDLLを含むポータブル版を配置します。

Windows APIテストは私用Window Stationのクリップボードを使います。この成功は別端末間の配信テストの代わりではありません。他OSも実機確認後に対応状況を更新します。
