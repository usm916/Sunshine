# Mac開発とPC間共有の引き継ぎ

更新日：2026-10-19 JST。共有元は **usm916アカウントのフォーク**。通常の開発は `custom/main` から始める。GitHubの既定ブランチが `master` のままでも、下記は明示して `custom/main` を取得する。

## 最初の目標と現在の対応

最初は **Mac版Moonlight → TANのWindows版Sunshine Custom** を確認する。次にMac版Sunshineをビルドし、OS固有の実装を移植する。Mac上でのビルド・配信・クリップボード往復は、この引き継ぎ時点では未確認。

| 機能 | Windowsホストの現状 | Macで必要な作業 |
| --- | --- | --- |
| 通常の映像・音声・キー・マウス | Windowsで接続確認済み | Macクライアントのビルドと接続、Macホストは別途検証 |
| テキストクリップボード | 独自v1、既定OFF、方向指定あり | Qt/SDLクライアント実装をMacで検証。Macホスト側は未実装 |
| クロップ・カーソル周辺の細部保持・背景粗粒化 | Windows D3D経路に実装 | Macホストの画面取得・変換経路への移植が必要。Macクライアントは処理済み映像を受け取る |
| 低帯域のFPS・予算・音声・回復制御 | Windows/NVENCで検証、第2修正あり | 制御ポリシーとMacのVideoToolbox再設定対応を分けて確認 |
| 端中央でのモニター切り替え | Windows用。複数画面の実機検証は未完了 | Macの画面・座標・再取得処理が必要 |
| LocalSystemサービス・UAC・Windowsランチャー | Windows専用 | Macで実行しない。Macの権限・起動方法は別実装 |

## 取得

Xcode 15以降、Git、Python 3を準備する。依存関係の詳細は、取得した版の [Sunshineビルド資料](../building.md) と [Moonlight README](https://github.com/usm916/moonlight-qt/blob/custom/main/README.md#building) を優先する。

```sh
mkdir -p "$HOME/Developer/moonlight-custom"
cd "$HOME/Developer/moonlight-custom"
git clone --branch custom/main --recurse-submodules https://github.com/usm916/Sunshine.git sunshineCustom
git clone --branch custom/main --recurse-submodules https://github.com/usm916/moonlight-qt.git moonlightCustom
```

共通ライブラリは両リポジトリのsubmoduleとして取得される。通信部自体を変更する時だけ、追加で `https://github.com/usm916/moonlight-common-c.git` の `custom/main` を別作業フォルダーへcloneする。

## 組み合わせの確認

[compatibility.json](compatibility.json) の `client_commit` は、このホストの引き継ぎ時点のクライアントを指定する。共通ライブラリは両側とも `ae67fe009386d536569fecc34ba156d581309b54`。`.gitmodules` にブランチ指定があっても、通常の `submodule update` は親リポジトリが記録したコミットを取得する。`--remote` で勝手に最新版へ進めない。

次は上の2フォルダーがある親ディレクトリで実行する。通常配信やクリップボード往復の成功を判定するものではない。

```sh
python3 - <<'PY'
import json, subprocess
from pathlib import Path
host, client = Path('sunshineCustom'), Path('moonlightCustom')
record = json.loads((host / 'docs/custom/compatibility.json').read_text())
client_record = json.loads((client / 'docs/custom/compatibility.json').read_text())
def git(path, *args):
    return subprocess.check_output(['git', '-C', str(path), *args], text=True).strip()
assert git(client, 'rev-parse', 'HEAD') == record['client_commit'], 'Client revision mismatch'
assert client_record['common_commit'] == record['common_commit'], 'Shared records mismatch'
for root, module in [(host, 'third-party/moonlight-common-c'), (client, 'moonlight-common-c/moonlight-common-c')]:
    assert git(root, 'ls-tree', 'HEAD', '--', module).split()[2] == record['common_commit'], 'Committed pin mismatch'
    assert git(root / module, 'rev-parse', 'HEAD') == record['common_commit'], 'Shared checkout mismatch'
    assert not git(root / module, 'status', '--porcelain'), 'Dirty shared checkout'
print('Pair pins verified; host:', git(host, 'rev-parse', 'HEAD'))
PY
```

Windowsでは `scripts/check-custom-pair.ps1` が取得元URLも含めて検査する。Macでクライアント側を変更した後にこの検査が止まるのは、固定した組み合わせから進んだことを示す。検証済みの変更をcommit・pushした後、新しいクライアントのコミットIDをホストの記録へ反映する。

## Mac版Moonlightのビルド

1. Xcodeを起動して初期設定・ライセンスを完了する。Command Line Toolsがなければ `xcode-select --install` を実行する。
2. Qt 6.11以降のmacOS SDKを用意する。Core、Network、Quick、QuickControls2、SVGが必要。Homebrewなら `brew install qt` を利用できるが、`qmake` の版と実体を確認する。Apple Siliconではarm64、Intelではx86_64のSDKとツールを揃える。
3. 次を実行する。`QMAKE` はこのシェルで保持する。

```sh
cd "$HOME/Developer/moonlight-custom/moonlightCustom"
export PATH="$(brew --prefix qt)/bin:$PATH"  # HomebrewのQtを使う場合
QMAKE="$(command -v qmake6 || command -v qmake)"
"$QMAKE" -query QT_VERSION
python3 setup-deps.py
mkdir -p cmake-build-macos-release
cd cmake-build-macos-release
"$QMAKE" ../moonlight-qt.pro
make -j4 release
```

生成先は通常 `cmake-build-macos-release/app/Moonlight.app`。リポジトリ直下へ戻り、`find cmake-build-macos-release -name Moonlight.app -type d` で実際の場所を確認する。SDKインストーラーを使う場合はその `macos/bin` をPATHへ追加し、HomebrewのPATH行を置き換える。`setup-deps.py` は同じリポジトリ内の `libs/mac` を更新する。

配布用DMGは、開発用ビルドと接続を確認してから `scripts/generate-dmg.sh` で作る。署名・公証・権限の扱いも配布時に別途確認する。

### クライアントの小さなテスト

Moonlightリポジトリ直下で、プロトコルテストとQt Coreの状態遷移テストを実行できる。

```sh
mkdir -p cmake-build-macos-tests
cc -std=c11 -I moonlight-common-c/moonlight-common-c/src   moonlight-common-c/moonlight-common-c/tests/clipboard.c   -o cmake-build-macos-tests/clipboard-protocol-test
./cmake-build-macos-tests/clipboard-protocol-test
mkdir -p cmake-build-macos-tests/state
cd cmake-build-macos-tests/state
"$QMAKE" ../../tests/custom/clipboard.pro CONFIG+=debug
make -j4
./clipboard-state-test
```

上記のQtテストはassertを利用するためdebugで実行する。実際のMacクリップボード、接続・切断時の動作は次の実機試験で確認する。

## TANとの接続試験

- TANの現在のWindows常用本体はSHA-256 `e256765a81f36540ab406a427df36d874a5cfa4b2e51a658f3fbdeb04ce6efc4`。詳細は [第2修正](mobile-link-recovery-v2-2026-10-19.md) と [TAN適用記録](tan-update-2026-10-19.md)。Macビルドに同じバイナリハッシュを期待しない。
- MacのMoonlightでTANを登録し、初回のPIN登録を行う。Mac側は新しいクライアントなので初回の登録が必要。ホストの更新では既存の登録を保持する。
- 最初はウィンドウモード、1280×720、30fps要求程度で、表示・音声・キー・クリック・スクロール・切断再接続を確認する。VPN等を使う場合はMacからTANへ到達できる既存の経路を使う。
- クリップボードのホスト設定は **Sunshine Web UI → Configuration → Inputタブ → Text Clipboard Sharing → Both directions**。保存・再起動後、Mac側の **Moonlight → Settings → Sync text clipboard** を有効にする。日本語・改行・絵文字を両方向で確認し、OFF、片方向、切断後も確認する。上限はUTF-8で32,755バイト。空のクリップボードや画像・ファイルは対象外。
- 低帯域設定は **Sunshine Web UI → Configuration → Audio/Videoタブ → Low bandwidth mode**。通常モードを確認してから **Adaptive bitrate, FPS and background** を試す。移動試験の観測待ち時間・再接続の有無・接続先を記録する。
- ログの画面は **Sunshine Web UI → Troubleshooting → Logs**。`low_bandwidth_state`、`low_bandwidth_decision`、`low_bandwidth_apply`、`cursor_focus_render` を使う。実回線での第2修正と、低帯域時の入力欠落／遅延は未確認事項である。

## Mac版Sunshineを始める

以下は現在のCMake構成に合わせた開始手順で、Macで成功を確認した手順ではない。画面取得・エンコーダー・OS入力・権限の順に不具合を切り分ける。

```sh
brew install boost cmake ninja node openssl@3 opus miniupnpc pkg-config qt doxygen graphviz uv
cd "$HOME/Developer/moonlight-custom/sunshineCustom"
uv sync --locked
cmake -S . -B cmake-build-macos -G Ninja   -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=ON   -DSUNSHINE_LLVM_COVERAGE=ON -DCMAKE_PREFIX_PATH="$(brew --prefix qt)"
cmake --build cmake-build-macos --target sunshine test_sunshine --parallel 4
./cmake-build-macos/tests/test_sunshine --gtest_filter='LowBandwidth*:VideoLowBandwidth*:CursorFocus*'
```

本体は標準の.app構成では `cmake-build-macos/Sunshine.app`。起動前に、同じMacで既存Sunshineが動いていないか確認し、開発用の設定保存先を決める。TANのWindows用設定・認証ファイルをMacへコピーしない。macOSの画面収録とアクセシビリティなどの権限は **システム設定 → プライバシーとセキュリティ** の該当項目で確認する。OSの版・署名・入力方式によって必要な許可が変わるため、Macでの実動作とログを優先する。

### 移植する場所

| 対象 | ファイル |
| --- | --- |
| プロトコル・方向・UTF-8・上限・ライフサイクル | `third-party/moonlight-common-c/docs/custom/clipboard-v1.md`、`src/stream.cpp`、`src/input.cpp` |
| Macホストの未実装クリップボード | `src/platform/macos/input.cpp` の `clipboard_set` / `clipboard_subscribe` / `clipboard_unsubscribe`、必要に応じObjective-C++を追加 |
| 低帯域ポリシーと観測 | `src/video_low_bandwidth.h`、`src/network_metrics.*`、`src/stream.cpp` |
| 再設定の共通APIとNVENC実装 | `src/video.*`、`src/nvenc/nvenc_reconfigure.*`。Macで動的再設定が非対応なら成功扱いしない |
| Windowsの背景・クロップ実装の参考 | `src/platform/windows/display_vram.cpp`、`src/video_cursor_focus*.h`、`src/video_viewport.h`、`src_assets/windows/assets/shaders/directx/include/cursor_focus.hlsl` |
| Macの映像・音声・入力 | `src/platform/macos/`、VideoToolboxのエンコーダー設定は `src/video.cpp` も確認 |
| Qt/SDLクライアントのテキスト同期 | Moonlightの `app/streaming/session.cpp`、`app/streaming/clipboardstate.h`、`app/nvhttp.cpp`、`app/gui/SettingsView.qml` |

UIで設定できてもMacの描画・エンコーダー経路が対応済みとは限らない。Windows専用機能をMacで有効として宣言せず、能力確認・未対応時の動作を明確にする。既存クライアントとの通常配信も維持する。

## PC間の更新手順

作業ごとに `codex/macos-client` や `codex/macos-host` のような機能ブランチを `custom/main` から作る。作業前に `git fetch origin`、変更を確認して `git merge --ff-only origin/custom/main`。作業が残っている場合は先にcommitし、別PCの変更へ合わせるためのresetや作業フォルダーの丸ごと同期はしない。

検証した変更をcommit・pushし、レビュー後に `custom/main` へ統合する。取り込み後は両方で次を行う。

```sh
git submodule sync --recursive
git submodule update --init --recursive
```

Sunshineではブランチ変更後に `uv sync --locked`。通信部を変える場合は、共通ライブラリを先にcommit・pushし、その取得可能なコミットをホストとクライアントのsubmoduleへ固定する。最後に互換性記録と接続試験結果を更新する。公開済みブランチをforce-pushしない。

共有対象はコード・仕様・テスト・手順。ビルドフォルダー、ログ、設定、PIN登録情報、証明書、秘密鍵は各PCで管理する。大きな配布物を必要とする場合はソースのGit履歴と分ける。

## 検証の境界と引き継ぎ文

このスナップショットのWindowsネイティブ関連テストは426件／54スイート成功。第2修正の変更実行行155/155、GPU読み戻し、隔離したWindowsクライアントとの同一接続中の粗粒化有効→無効を確認。Web 72件／11ファイル、Windowsスクリプトの隔離試験56件（インポートされた重複ケースを含む）、互換性検査の隔離試験9件も成功。[検証概要](validation-2026-10-19.json) に記録する。Macの結果を示すものではない。詳細は [第2修正](mobile-link-recovery-v2-2026-10-19.md)、[入力調査](input-low-bandwidth-audit-2026-10-19.md)、[互換性記録](compatibility.json)。過去の日付の資料より本書と最新の修正記録を優先する。

Mac側での作業開始時には、次の内容を開発担当へ渡せる。

> このリポジトリのAGENTS.md、docs/custom/mac-development.mdとcompatibility.jsonを読んでください。まずMac版Moonlightをビルドし、WindowsのTANにあるSunshine Customとの通常配信・入力・音声・テキスト同期を確認してください。共通ライブラリの固定コミットとv1プロトコルを維持してください。Macホストのクリップボード、粗粒化、クロップ、動的エンコーダー再設定は未検証／未移植です。実際に行った検証と未確認事項を分けて記録してください。
