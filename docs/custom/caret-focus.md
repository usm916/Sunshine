# 文字入力位置とマウス位置への画質優先

Windows D3D11のマウス優先領域を、入力操作に合わせて切り替える。
通常版Moonlightを利用でき、クライアント側の変更は不要。
現在の配信画面内で鮮明にする中心の選択を変更する機能であり、画面の切り出しやモニター横断は含まない。

## 動作

- 文字入力・編集キー操作後は、Windowsのキャレット位置を優先する。
- マウスを一定距離動かすとマウス中心に戻る。クリックとホイールは移動距離に関係なく即座に戻る。
- 小さな揺れではキャレット優先を解除しない。移動距離は最後のキー操作時のマウス位置から測る。
- キー入力を再開すると再びキャレット優先になる。Shift/Ctrl/Alt/Windows等の修飾キーだけでは切り替えない。
- 位置が取得できない、古い、別ウィンドウの情報、配信モニター外の場合はマウス中心を使う。

標準Win32キャレットを先に取得し、取得できなければUI Automation TextPattern2を試す。
入力文字やパスワードの内容は取得・保存しない。
UI Automationは専用ワーカーで実行し、映像処理は取得を待たず、直近の有効位置だけを使う。
300msより遅い取得結果や古いキャッシュは採用しない。

## 設定

Audio/Videoのマウス優先設定にあるFollow Text Input Locationを有効にする。
既定は有効だが、親の画質優先機能自体は従来どおり既定OFF。
低帯域モードが画質優先を有効化する場合にも適用される。

- cursor_focus_follow_text = enabled / disabled。既定enabled。
- cursor_focus_mouse_move_threshold = 6。1〜100、ホスト物理ピクセル。クリックはこの設定にかかわらず切り替える。

設定変更は保存・適用後の新しいセッションで確認する。
管理者権限のアプリへの入力には、管理者確認対応のStart Sunshine.cmdを使って起動する。

## 対応範囲

アプリが位置情報を公開しない独自描画入力欄や保護された画面では、キャレットを追跡できない場合がある。
その場合は最後のキャレットに固定せず、マウスへ戻す。
Windowsの実カーソルとキーボードフォーカスを観測するため、ローカル操作や別のリモート接続の操作も影響する。

## ビルドと検証（2026-10-15）

Windows MSYS2 UCRT64で本体・テスト・Web UIをビルドした。

- ネイティブ152件中151件成功。実Brokerとライセンスが必要な1件をスキップ。
- 切替とキャッシュの共通ポリシーは実行可能行43/43を通過。
- Win32の非アクティブ・画面外の専用テストウィンドウに実キャレットを作り、座標取得・移動・非表示時の拒否を確認。
- Web UI全71件成功。CursorFocus.vueは行37/37、分岐41/41、関数15/15を通過。
- 標準Moonlight 4.3.1のウィンドウモードで2560×1440/HEVCを受信。別ポートの試験用ホストで入力監視フックの起動をログで確認。
- 上記の接続試験は、iPad実機での文字入力や各アプリでのUI Automation追従を検証したものではない。
  iPadと利用アプリでの操作感・キャレット公開状況は実機確認が必要。

起動元はリポジトリ直下のStart Sunshine Custom.cmd、またはcmake-build-custom/package-caret-focus/Start Sunshine.cmd。
どちらも管理者確認対応・共通パスワード対応。稼働中の旧インスタンスを終了してから起動する。
今回の整備では既存配信ホストの停止・再起動は行っていない。

ログはcmake-build-custom/caret-tests.log、caret-web-tests.log、caret-live.log。
配布ZIPに認証ファイルや接続ログは含めない。

位置取得API: [GetGUIThreadInfo](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getguithreadinfo)、
[GetCaretRange](https://learn.microsoft.com/en-us/windows/win32/api/uiautomationclient/nf-uiautomationclient-iuiautomationtextpattern2-getcaretrange)。


## ブラウザー入力欄への対応

ChatGPT等のChromium入力欄でTextPatternが返らない場合に備え、Win32標準キャレットに続いてMSAAのOBJID_CARETを確認する。
UI AutomationではTextPattern2だけでなくTextPatternの空選択範囲も使用し、空範囲の矩形が返らない場合は隣接文字の幾何情報で位置を補う。
テキスト内容は取得しない。無効・非表示・別ウィンドウ・古い結果は破棄してマウスへ戻る。
参考: [Microsoft Caret/MSAA](https://learn.microsoft.com/en-us/windows/win32/winauto/caret)、
[UIA空範囲の矩形](https://learn.microsoft.com/en-us/dotnet/api/system.windows.automation.text.textpatternrange.getboundingrectangles)。
