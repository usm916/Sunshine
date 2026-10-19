# 細い・不安定な回線での接続解除の診断

確認日: 2026-10-15。ローカル Sunshine: 9a7fda75、ブランチ codex/cursor-background-loop。
優先クライアントは iPad、Mac・Windows も対象とする。

## 結論と検証範囲

ソフト側で改善できる余地はある。特に、回線の実効速度より多く送る、短時間に送信が集中する、
回復不能な古いフレームが溜まる、数秒の通信停滞で両端のどちらかが切断する、という場合である。
完全な通信断の間も操作を成立させることはできないが、最終画像の維持や復帰の扱いは改善できる。

今回の切断原因は未確定。以下の3観点を順に照合した。実回線の切断を再現した3回の実験ではない。
今後この種の調査でも、最低3つの異なる観点で検証し、実測・コード上の事実・推測を分ける。

## 1回目: 両端の切断条件をコードで検証

| 見つかった条件・文言 | 意味 | 対処を判断する際の注意 |
| --- | --- | --- |
| Sunshine の Ping Timeout | 制御チャネルのクライアントイベントが指定時間届かずセッション終了 | 現行の既定値は10秒。帯域・制御通信・端末停止を切り分ける |
| Sunshine の Initial Ping Timeout | 接続開始時、音声・映像用UDPの初期通信を待てなかった | 接続後の画質不足とは別に、UDP到達性・経路・FWを確認する |
| Moonlight の lack of video traffic / -100 | 接続開始後に映像データが届かなかった | 単純な帯域不足以外にUDP遮断もあり得る |
| Moonlight の lack of a successful video frame / -101 | 接続開始後、完全な最初の映像フレームが組み上がらなかった | 初期キーフレームの大きさ・欠落・極端な帯域不足を確認する |
| Moonlight の ENet control timeout | クライアント側も制御接続を独立して切断する | ローカル共通ライブラリの通常設定は10秒。ホストだけ延長しても不十分 |
| Sunshine の Hang detected! Session failed to terminate in 10 seconds. | セッション終了処理が停止し、プロセス終了で復旧する経路 | GPU/NVENC等の終了処理。通常のネットワーク待ち時間とは区別する |
| Platform failed to initialize / シェーダー読み込み失敗 | ホスト側の起動・キャプチャ初期化の問題 | 回線対策で直ると決めつけない |

Moonlight の最初の映像に関する10秒条件を、「配信途中で映像が10秒止まれば必ず同じエラー」と解釈しない。
アプリ終了、キャプチャ失敗、エンコーダ異常、クライアントのデコーダ異常も別途あり得る。

ローカルで確認した箇所:

- src/config.cpp:819、src/stream.cpp:776、1396、2195、2292
- third-party/moonlight-common-c/src/VideoStream.c:142-180
- D:/data/code/moonlightCustom/moonlight-common-c/moonlight-common-c/src/ControlStream.c:1912
- D:/data/code/moonlightCustom/moonlight-common-c/moonlight-common-c/src/Limelight.h:410-440

## 2回目: このPCのログ・起動系統を検証

読み取り確認のみ実施した。ホストの停止・再起動・設定変更・回線制限は行っていない。

| 対象 | 確認できたこと |
| --- | --- |
| %LOCALAPPDATA%/SunshineCustom/config/sunshine.log | 最終更新 2026-10-14 21:35:55。今回の新しい切断の証拠には使えない |
| C:/Program Files/Sunshine/config/sunshine.log | 最終更新 2026-10-15 04:17:09。先頭のバージョンは v0.23.1 |
| インストール版の同ログ | 04:16:09に assets/shaders/directx/convert_yuv420_packed_uv_type0_ps.hlsl 読み込み失敗、Platform failed to initialize がある |
| SunshineService | Running。サービスの実行パスは C:/Program Files/Sunshine/tools/sunshinesvc.exe |
| sunshine.exe | PID 8060 を確認したが実行パスは取得できず。接続相手が旧版かカスタム版かは未確定 |

シェーダーの失敗は、資材不足や起動時の作業ディレクトリ等を調べる材料であり、今回の切断原因と断定しない。
サービスが動いているだけで、ユーザーが接続している実体まで旧版とは断定しない。

最初に「いま接続している Sunshine の WebUI のログ」を保存すること。
Custom共通ログという名前だけで現在のログだと判断せず、接続時刻の CLIENT CONNECTED と更新日時を照合する。

## 3回目: 公式クライアント・本流の改善案と照合

現在のマウス優先自動設定は、要求ビットレートによって背景処理を選ぶ仕組みで、
接続中の回線実効速度を測って追従する輻輳制御ではない。背景を粗くしても、
FEC・ヘッダー・制御通信・送信の集中までなくなるわけではない。

本流に [PR #5460: adaptive NVENC bitrate control](https://github.com/LizardByte/Sunshine/pull/5460)
がある。確認時点では未マージ、WindowsネイティブNVENC向け、既定で無効。
既存のMoonlight FEC報告・フレーム損失通知・ENet RTTから帯域を下げ、回復時に徐々に上げる設計。
新しいフィードバックプロトコルを要求せず、エンコーダを作り直さずに再設定する。
現在のローカル版には未導入。作者の試験報告を、このPC・iPadでの確認済み結果として扱わない。
Moonlightの世代による報告の有無、低帯域での最低パケットコスト、複数接続時の公平性は別途検証が必要。

また、確認した公式iOSソースではビットレート選択の下限は500kbps、FPS選択は30/60/対応機種120。
したがって250kbps程度の回線を、通常iPadクライアントの設定操作だけで十分に救えるとは言えない。
サーバー側の実効予算制御・フレーム送出削減は、通常iPadへの対応を維持するための重要な候補。
iPadのインストール済みバージョンがこのソースと一致することは未確認。

## まず取るログ: iPad優先

1. iPadのMoonlight設定で統計表示（Statistics / On-screen statistics）を有効にする。
   解像度、FPS、ビットレート、コーデック、HDR、アプリのバージョン、接続経路
   （LAN/外部回線/VPN/中継）を記録する。
2. PCとiPadの時刻を合わせる。接続開始と切断の時刻を秒単位・タイムゾーン付きでメモする。
3. PCの、接続相手に対応するSunshine WebUIで Troubleshooting / トラブルシューティングを開く。
   接続時刻の CLIENT CONNECTED があることを確認する。既定ポートなら https://localhost:47990 だが、
   複数の起動系統やポート変更がある場合は実際のWebUIを使う。
4. まず既定の info ログのまま1回再現する。統計画面を不調前・不調時にスクリーンショット保存し、
   切断ダイアログの文面とエラーコードも保存する。
   画面収録は端末負荷を変えるので、最初はスクリーンショットを優先する。
5. 切断後、Sunshineを再起動する前にログ全文を保存する。
   現在のカスタムWebUIではログのフィルターを解除し Copy Logs でコピーしてテキスト保存できる。
   Copy Logs はフィルター適用中だと表示範囲だけをコピーするため注意する。
   古いWebUIならログ欄のテキスト保存、または同じ認証済みWebUIの /api/logs を保存する。
6. infoで原因が不足する場合だけ、次の再現前に min_log_level = debug を設定する。
   保存・適用時は配信が切れる可能性があるため、ログを退避してから配信終了後に行う。
   debug/verboseは性能に影響する可能性があるので、最初から常用しない。調査後は info に戻す。

保存範囲は起動情報と接続開始から切断後まで。最低でも切断前60秒・後10秒は含める。
「エラー行だけ」では、どちらが先に終了したか分からなくなる。

標準の候補パス:

- Customランチャー: %LOCALAPPDATA%/SunshineCustom/config/sunshine.log
- インストール版: %ProgramFiles%/Sunshine/config/sunshine.log
- log_path を変更していればその設定先

現行Custom版は起動ごとに .1～.5 の前回ログも保持する。再起動済みなら該当する前回ログも確保する。
旧v0.23.1でも同じ保持動作だとは決めつけない。

### iPadの詳細ログが必要な場合

MacにUSB接続し、iPadをロック解除して接続を信頼する。
Macの Console / コンソールでデバイス一覧からiPadを選び、Start / 開始を押してから再現する。
Moonlight関連プロセス（Limelight等、実際に表示される名前）で絞り込み、必要なら情報メッセージを含める。
接続開始～終了周辺を選択してコピー・保存し、Connection terminated と番号を探す。
公式iOS実装はこの情報をNSLogで出している。

この手順は既存アプリのログ閲覧であり、アプリの再ビルド・有料開発者契約は必要ない。
Macがない場合でも、統計画像・終了メッセージ・時刻・PC側ログで最初の切り分けはできる。
iPadの「解析データ」はアプリ自体がクラッシュしたときの補助で、通常の通信切断ログの代わりにはしない。

### Mac・WindowsのMoonlight

PC版では統計表示は Ctrl+Alt+Shift+S（MacのAltはOption）。
Mac・Windowsとも、配信テストはウィンドウモードで行う。

通常のQt版リリースログ:

| 環境 | 保存先 |
| --- | --- |
| Windows通常インストール | %TEMP%/Moonlight-*.log |
| macOS通常アプリ | /tmp/Moonlight-*.log |
| ポータブル版 | 起動時の作業ディレクトリの Moonlight-*.log。通常はランチャー/実行ファイルのあるフォルダー |

Moonlightのログはアプリ起動からの経過時間表示なので、アプリ起動時の実時刻も記録する。
ファイル名はUnix秒。新しいログが増えると古いログは削除されるため、再起動を繰り返す前に退避する。
独自ビルド・stderrリダイレクトでは保存先/出力方法が異なることがある。

## 集まった記録からの判断と改善候補

| 同時に起きること | 優先して疑うもの | ソフト側の改善候補 |
| --- | --- | --- |
| RTT上昇→遅延増加→損失/切断 | 送信過多・回線キューの滞留 | 実総送信量の予算化、適応ビットレート、送出FPS削減、回線に合うペーシング |
| RTTは落ち着いているが損失が増える | 無線/経路の欠落など | 損失の連続性を計測し、予算内でFEC・復旧処理を調整 |
| 操作・映像が同時に停止、両端タイムアウト | 一時的通信断、経路変化、端末停止など | 両端で整合する猶予時間、最終画像保持、安全な再接続。待ち時間延長だけで映像は流れない |
| 最初の映像だけ届かず -100/-101 | UDP到達性、初期フレーム、帯域の不足 | 経路確認、低い解像度での比較、起動時の送信量/集中を抑制 |
| ホストログにGPU/キャプチャ致命的エラー、PID変更 | ホスト側の異常や再起動 | キャプチャ・エンコーダ・起動資材の問題を修正 |
| ネットワーク損失は小さくデコード時間だけ増える | クライアント処理負荷 | HDR/コーデック/解像度/FPSの比較。回線問題と分ける |

ホストの Ping Timeout は「最後に観測された結果」の可能性もある。
iPadが先に失敗して通信を止めた後に出るケースと区別するため、両端の時刻が必要。

比較は、同じ画面・同じ経路で1項目ずつ変更して行う。
まず低解像度・30fps・HDRオフで接続できるかを対照にし、目的の2.5K設定と比較する。
250～500kbpsでは要求ビットレートと実際の回線総量を分けて計測する。
既存の短いループバック計測は実回線の速度制限試験ではなく、安定動作保証には用いない。

再現の最小記録テンプレート:

~~~text
クライアント機種 / OS / Moonlight版:
接続先Sunshine版 / WebUI / 起動方式:
経路（LAN/外部/VPN/中継）:
設定（解像度、FPS、コーデック、HDR、要求kbps、音声）:
アプリ起動時刻 / 接続時刻 / 切断時刻（秒・タイムゾーン）:
切断メッセージ / エラーコード:
切断前のRTT、損失、受信FPS、デコード時間（表示される項目）:
Sunshineログ名 / 最終更新:
添付: 統計画像、切断画面、該当セッションのホスト/クライアントログ
~~~

共有するのは必要なログに絞り、認証ヘッダー・鍵・証明書・ペアリング情報は含めない。
sunshine_state.json や credentials/鍵ファイルを一括で渡す必要はない。

## 一次資料

- [Sunshine設定: min_log_level / log_path / ping_timeout](https://docs.lizardbyte.dev/projects/sunshine/latest/md_docs_2configuration.html)
- [Moonlight公式FAQ: 統計表示](https://github.com/moonlight-stream/moonlight-docs/wiki/Frequently-Asked-Questions#how-can-i-see-on-screen-statistics-about-my-streaming-performance)
- [iOS設定: bitrateTable とFPS選択](https://github.com/moonlight-stream/moonlight-ios/blob/master/Limelight/ViewControllers/SettingsViewController.m)
- [iOS接続終了表示](https://github.com/moonlight-stream/moonlight-ios/blob/master/Limelight/ViewControllers/StreamFrameViewController.m)
- [iOS Logger: NSLog出力](https://github.com/moonlight-stream/moonlight-ios/blob/master/Limelight/Utility/Logger.m)
- [Apple: Consoleで接続機器のログを表示](https://support.apple.com/en-ie/guide/console/cnsl1012/1.1/mac/27)
- [Moonlight Qtログ保存先](https://github.com/moonlight-stream/moonlight-qt/blob/master/app/path.cpp)
- [本流の適応NVENC提案 #5460](https://github.com/LizardByte/Sunshine/pull/5460)
- [前回の異なる2観点の回線対策調査](unstable-link-research.md)

## 追加確認: Sunshine側だけで混雑を判断できる範囲

2026-10-15追記。ホストの現在の処理、UDPの性質、公式iOSの既存通知の3観点を照合した。
実回線で検出精度を試験した結果ではない。

Sunshine側で「混雑らしさ」を推定する実装は可能だが、現在のカスタム版はそれを自動画質制御に
結びつけていない。要求ビットレート別の背景プリセットとは別機能。

利用候補は、制御接続のENet RTT/変動、映像のFEC受信状況・回復要求、実総送信量、
ホスト側のキュー待ち・キャプチャ/エンコード処理時間。
RTTの基準値からの増加と映像の欠落傾向を組み合わせる。
制御接続RTTは映像到着・表示時間そのものではなく、小さい制御パケットと映像では状況が異なり得る。
欠落だけでは輻輳、Wi-Fiエラー、経路変化を区別できない。

現行src/stream.cppには旧LOSS_STATSのログ処理があるが、
同梱の新しい共通クライアントはSunshine向けには別経路のFEC状況報告と定期pingを使う。
旧ハンドラーの存在だけで正確な損失率を取得済みとは解釈しない。
詳細FEC報告の解析・時系列集計は現行ホストに未実装で、ENet RTTも画質制御に未利用。
IDR要求は処理済みだが、デコーダ等の理由でも起きるため混雑の確証ではない。

公式iOS masterが参照するmoonlight-common-c
f900dd4767759c7b9d0e93bcea666b55c69ea62fにも、SunshineへのFEC状況報告を確認した。
ホスト側に解析を追加すれば、対応する通常iPad版から追加プロトコルなしで材料を得られる。
インストール済みiPadアプリの版と実際に届く通知は、接続時に確認する必要がある。
FEC報告はbest-effortで、全フレームが必ず報告されるものでもない。

UDPの送信成功は相手への到着保証ではない。送信APIが速く戻っても、ルーターや無線で詰まる。
また既存ログの Network: frame's overall network latency は送信処理内の経過時間であり、
iPadまでの往復時間や画面表示遅延ではない。

- [iOS参照共通ライブラリの通知実装](https://github.com/moonlight-stream/moonlight-common-c/blob/f900dd4767759c7b9d0e93bcea666b55c69ea62f/src/ControlStream.c)
- [UDPの制約・輻輳制御: RFC 8085](https://www.rfc-editor.org/rfc/rfc8085.html)
- [既存通知とRTTを使う本流提案 #5460](https://github.com/LizardByte/Sunshine/pull/5460)

## 追加確認: PR #5460はFPS間引きと超低帯域への追従を含まない

2026-10-15、PR HEAD 87cbef6f23a773704bc2dd551c23465f22381091で確認。
制御出力、エンコーダ変更、映像ループ・テストの3観点を照合した。ビルド・実接続試験は未実施。

- adaptive_bitrate::decision_tの出力はtarget_kbpsと理由。FPSの変更指示はない。
- NVENC再設定は平均/最大ビットレートとVBVバッファ等を変更する。FPSは変更しない。
- video.cppはエンコード直前に再設定要求を適用し、そのままencodeを呼ぶ。
  回線に応じた符号化前の間引きや映像送信の破棄は追加されていない。
- minimum_target_kbpsは3,000。floor_kbps_ = min(3,000, ceiling_kbps_)。
  したがって制御上限が500kbpsなら下限も500kbpsとなり、そこから250kbpsへ自動低下しない。
  client要求値と制御上限は、音声・FEC予約やホスト上限等により同一とは限らない。

前述のPRは実装構成の参考になるが、今回の250kbps～1Mbps向けの完成した対策とは扱わない。
利用する場合は超低帯域の下限、FEC込みの実転送量、FPSとの連動、復旧処理を追加検証する。

- [制御出力と3Mbps下限](https://github.com/Dragon-kanji/Sunshine/blob/87cbef6f23a773704bc2dd551c23465f22381091/src/adaptive_bitrate.h)
- [下限の計算](https://github.com/Dragon-kanji/Sunshine/blob/87cbef6f23a773704bc2dd551c23465f22381091/src/adaptive_bitrate.cpp)
- [NVENC設定変更](https://github.com/Dragon-kanji/Sunshine/blob/87cbef6f23a773704bc2dd551c23465f22381091/src/nvenc/nvenc_reconfigure.cpp)
- [映像ループへの組み込み](https://github.com/Dragon-kanji/Sunshine/blob/87cbef6f23a773704bc2dd551c23465f22381091/src/video.cpp)
