# iPadでタスクマネージャー選択時に入力が止まる問題と入力位置への画質優先

調査日: 2026-10-15。稼働中ホストの再起動・権限変更はこの調査では行っていない。

## 確認した症状と原因

利用者から、iPad接続中にタスクマネージャーを選ぶとマウス・キーボードが効かなくなり、
ホストのローカル操作で別ウィンドウへフォーカスを移すと復帰するとの報告があった。

### 観点1: 実環境

Windowsのプロセストークンを読み取り、配信中のカスタムSunshine (PID 41520) は非昇格、
Taskmgr (PID 20492) は昇格済みであることを確認した。
AnyDeskサービスは稼働を継続している。通常アプリへ戻すと入力が復帰する挙動は、下記のUIPI制限と一致する。
今回の条件による操作不能を説明できるが、他の条件の切断・入力不調まで同じ原因と断定しない。

### 観点2: 実装とWindows仕様

Windowsのlibvirtualhidバックエンドには、ドライバー経由の入力が利用できない際、
マウス・キーボードをSendInputへフォールバックする経路がある。
このPCではVirtual HID Brokerサービスと対象ドライバーを確認できなかった。
既存ログには入力デバイス作成失敗の記録がなく、Broker不在だけを原因とする根拠はない。

Microsoftの[SendInput仕様](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-sendinput)では、
呼び出し元と同じか低い整合性レベルのアプリにだけ入力を注入できる。
通常権限のSunshineから高い権限のTaskmgrへの入力は、この制限に該当する。
UIPIによる失敗かどうかは、戻り値やGetLastErrorだけでは特定できない。

対策は、同じWindowsユーザーの管理者権限でカスタム版を起動して、Taskmgr操作を再試験すること。
配布フォルダーのStart Sunshine.cmdは共通設定を使うため、パスワードを再設定する必要はない。
先に通常権限のインスタンスを終了し、同ポートで二重起動しない。
管理者起動への切り替え・その状態でのiPad実機試験は、この調査では未実施。
UACの保護されたデスクトップやロック画面の制御まで保証するものではない。

### 観点3: 入力処理のテスト

MSYS2 UCRT64でInput*とVirtualHid*の既存テストを実行した。
34件中33件成功、ライセンスと実Brokerを必要とする1件はスキップ。
パケット検証、入力キュー、再接続、マウス・キーボードの変換とルーティングを確認するテストであり、
iPad実機やUIPI制限を再現したテストではない。
ログ: cmake-build-custom/input-investigation-tests.log / input-investigation-tests.xml。

過去の接続ログにはUDP受信エラーによる切断もあるが、Taskmgr選択で操作だけ止まる症状と同一原因とは扱わない。

## カーソルまたは文字入力位置への画質優先

追記: この調査後に切替機能を実装した。[現行の設定と検証結果](caret-focus.md)を参照。以下は調査時点の記録。

Sunshine側だけで追加実装できる。通常の映像として送るため、Moonlight側の変更は不要。
現在の実装はGetPhysicalCursorPosからマウス位置だけを取得しており、文字入力位置の検出は未実装。

候補となる取得経路は次のとおり。

1. 標準のキャレット: [GetGUIThreadInfo](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getguithreadinfo)
   のhwndCaret/rcCaretを取得し、画面座標へ変換する。
2. 対応アプリ: [UI Automation GetCaretRange](https://learn.microsoft.com/en-us/windows/win32/api/uiautomationclient/nf-uiautomationclient-iuiautomationtextpattern2-getcaretrange)
   で有効なキャレット位置を取得する。
3. 取得できないアプリ、非表示・古い座標、配信対象モニター外では、マウス位置へ戻す。

提案する選択ルールは、マウス操作時はカーソル、文字入力時はキャレットを優先する方式。
入力終了後もしばらく位置を保持し、再びマウスを操作すると切り替える。
DPI・画面回転・複数モニター座標の変換と、境界で頻繁に切り替わらない処理が必要。
UI Automationは専用ワーカーで取得し、遅いアプリの応答待ちが映像処理を止めない設計にする。
独自描画の入力欄や一部アプリではキャレット取得が保証されず、全アプリ対応とは言えない。
