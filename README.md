# SokuWinTracker

東方非想天則(Hisoutensoku) 用 SWRSToys モジュール。対戦中のみ、1P/2Pの累計勝利数を画面左上・右上に表示する。

## ビルド方法

`src/main.cpp` は [SokuDev/SokuMods](https://github.com/SokuDev/SokuMods) の `modules/` 配下に配置してビルドする前提のファイル。

```powershell
./build.ps1
```

上記スクリプトが以下を自動で行う。

1. `SokuMods` を(未取得なら)`git clone --recursive` で取得
2. `modules/SokuWinTracker/main.cpp` として `src/main.cpp` を配置
3. ルートの `CMakeLists.txt` に SokuWinTracker モジュール登録を追記(未登録の場合のみ)
4. cmake configure + build (MSVC, Win32, Release)
5. 生成された `SokuWinTracker.dll` を `dist/SokuWinTracker_full/Modules/SokuWinTracker/` にコピーし、`SokuWinTracker.zip` として再パッケージ

主なオプション:

```powershell
./build.ps1 -Clean      # SokuMods の build ディレクトリを削除してから configure し直す
./build.ps1 -Reclone    # 既存の SokuMods チェックアウトを削除して clone し直す
./build.ps1 -Deploy     # ビルド後、実機の th123 インストール先に DLL を上書き配置(ini は保持)
```

手動でビルドする場合:

1. `git clone --recursive https://github.com/SokuDev/SokuMods.git`
2. `modules/SokuWinTracker/main.cpp` としてこのリポジトリの `src/main.cpp` を配置
3. ルートの `CMakeLists.txt` に以下を追加(`UPnPNat` モジュール登録の直前あたり)

```cmake
module(SokuWinTracker)
target_sources(SokuWinTracker PRIVATE modules/SokuWinTracker/main.cpp)
target_link_libraries(
        SokuWinTracker
        SokuLib
        shlwapi
        ws2_32
        user32
        "${CMAKE_SOURCE_DIR}/lib/d3d9.lib"
        "${CMAKE_SOURCE_DIR}/lib/d3dx9.lib"
)
```

4. Visual Studio Build Tools (MSVC, MinGW不可) + CMake で以下を実行

```
cmake -A Win32 -B build -DCMAKE_POLICY_VERSION_MINIMUM=3.5 .
cmake --build build --config Release --target SokuWinTracker
```

生成物: `build/Release/SokuWinTracker.dll`

## インストール方法

ビルド済みの配布物(`SokuWinTracker.zip`)を使う場合は [Releases](../../releases) から取得してください。

`(天則フォルダ)/Modules/SokuWinTracker/SokuWinTracker.dll` に配置し、
`SWRSToys.ini` の `[Module]` に以下を追記して有効化する。

```
SokuWinTracker=Modules/SokuWinTracker/SokuWinTracker.dll
```

## 仕様

- `SokuLib::VTable_BattleManager` の `onRender` のみをフックし、対戦中のみ動作する
  (`onProcess` は giuroll 等のロールバックネットコードにより実フレームあたり複数回呼ばれうるため、
  勝敗判定・相手変更検知・ini書き込みは必ず実フレームに1回だけ呼ばれる `onRender` 側で行う)
- 先取2ラウンドで試合勝利とみなし、`leftCharacterManager.score` / `rightCharacterManager.score` を毎フレーム監視
- 累計は DLL と同じフォルダの `SokuWinTracker.ini` に保存(`[Wins] P1=`, `P2=`)。
  ただしこれはセッション中の値の記録用で、天則の exe を起動するたび(`Initialize` 呼び出し時)に
  0-0 にリセットしてから書き込むため、前回起動時の値を次回起動に持ち越すことはない
- `mainMode` がネット対戦系(VSCLIENT/VSSERVER/VSWATCH)とそれ以外の間を跨いだ瞬間(どちら向きでも)に、
  自動で 0-0 にリセットする。部屋を新しく立てる/入り直すたびに新しい対戦相手とみなしてリセットされ、
  逆にネット対戦からローカル/CPU戦(メニュー経由)に移った時もリセットされるので、オンラインの累計が
  無関係なローカル戦に持ち越されることはない。ローカル/CPU戦同士の切り替え(アーケード↔VSプレイヤー等)
  では自動リセットしない、そのままセッション中ずっと加算され続ける
  (以前は相手のIPアドレスや `NetObject.profile1name`/`profile2name` の変化を見て相手が変わったかどうかを
  判定していたが、`NetObject` 側の未文書化なメモリレイアウトに依存していて実機では信頼できなかったため、
  モード遷移ベースのより単純で確実な方式に変更した。この方式では同一相手との連戦でも部屋を出入りすれば
  リセットされる)
- リプレイ観戦中(`BATTLE_SUBMODE_REPLAY`)とプラクティスモード(`BATTLE_MODE_PRACTICE`)では
  バッジを表示せず、勝敗判定・ini書き込みも行わない
- `mainMode` が切り替わった直後は、両者のスコアが実際に 0-0 になるのを一度確認するまでバッジ表示・
  カウントを保留する(モード切り替え中に残る古いスコア値を勝敗として誤検知しないようにするため)
