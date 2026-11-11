# SF Tune Player

微分音に対応した SoundFont プレイヤー（VST3 / AU / Standalone）。JUCE 8 で作っています。

## 機能

| 機能 | 内容 |
|---|---|
| **SF2** | プリセット・インストゥルメント・サンプルの階層、グローバルゾーン、キー/ベロシティレンジ、ループ（連続・リリースまでのループ）、ルートキー、coarse/fine tune、scaleTuning、減衰量、パン、ボリュームエンベロープ、exclusive class、24bit（sm24）。MIDI のバンク/プログラムチェンジに対応 |
| **SFZ** | `<control> <global> <master> <group> <region>`、`#define` / `#include`、`default_path`、音名表記（`c#4` など）。主なオペコード：`sample lokey hikey key pitch_keycenter lovel hivel tune transpose pitch_keytrack volume amplitude pan offset end loop_mode loop_start loop_end ampeg_* trigger=release group off_by`。WAV / AIFF / FLAC / Ogg を読み込み、WAV に埋め込まれたループ点も使います |
| **ADSR** | Attack / Decay / Sustain / Release のつまみ。「Use envelope from file」をオンにすると SF2/SFZ ファイルに書かれたエンベロープを使います |
| **微分音** | **.scl**（Scala：セント値・比率の両方）、**.kbm**（キーボードマッピング）、**.tun**（AnaMark：`[Tuning]` と `[Exact Tuning]`）。全 128 キーそれぞれに任意の周波数を割り当てられます |
| その他 | ピッチベンド（幅 0〜48 半音）、サスティンペダル、CC7/CC11、最大 96 ボイス、4 点 Hermite 補間。読み込んだファイルのパスは DAW のプロジェクトに保存されます |

### チューニングの扱い
- **.scl**：スケールの 0 度を「Scale Base Key」のキーに置き、そのキーを「Base Freq」の周波数で鳴らします（初期値は C4 = 261.626 Hz）。
- **.scl と同じ名前の .kbm** が同じフォルダにあれば自動で読み込みます。あとから .kbm を単独で読み込むこともできます。.kbm を使っている間は、基準キーと基準周波数は .kbm の設定が優先されます。
- **.tun** は各キーの絶対的な音高をそのまま使います。
- どのサンプルを鳴らすかは MIDI のキー番号で決まり、音高はチューニング表から計算します。そのため、どんな音律でも各キーにいちばん近いサンプルが使われます。ドラムのように `scaleTuning=0` / `pitch_keytrack=0` が指定された音は、チューニングを変えても音高が変わりません。
- 「12-TET」ボタンで平均律に戻ります。

## ビルド方法

必要なもの：CMake 3.22 以上と C++17 コンパイラ（Windows は Visual Studio 2022、macOS は Xcode）。JUCE は初回の configure で自動的にダウンロードされます。

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

できあがるファイル：
- `build/SFTunePlayer_artefacts/Release/VST3/SF Tune Player.vst3`
- `build/SFTunePlayer_artefacts/Release/Standalone/`（単体アプリ。動作確認用）
- macOS の場合は `AU/SF Tune Player.component` も作られます

VST3 のインストール先：
- Windows：`C:\Program Files\Common Files\VST3\`
- macOS：`~/Library/Audio/Plug-Ins/VST3/`（AU は `~/Library/Audio/Plug-Ins/Components/`）
- Linux：`~/.vst3/`

Linux では先に依存パッケージを入れてください：
`sudo apt install libasound2-dev libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libfreetype-dev libfontconfig1-dev libcurl4-openssl-dev`

JUCE がすでに手元にある場合は `-DJUCE_DIR=/path/to/JUCE` を指定すると、ダウンロードせずにそれを使います。

## Windows インストーラーの配布とアップデート

GitHub にリポジトリを作ると、Windows 用のインストーラー（`SFTunePlayer-Setup.exe`）が自動でビルドされ、配布と更新までできるようになります。

### 最初の 1 回だけ
1. GitHub で **公開（Public）リポジトリ** を作ります（例：`SFTunePlayer`）。非公開にするとアップデートの確認が動きません。
2. このフォルダをそのリポジトリに push します。
   ```bat
   git init
   git add -A
   git commit -m "first"
   git branch -M main
   git remote add origin https://github.com/<ユーザー名>/SFTunePlayer.git
   git push -u origin main
   ```
3. 最初のバージョンを公開します：`release.bat 1.0.0`

10〜15 分ほどで、次のリンクからインストーラーをダウンロードできるようになります（このリンクはずっと変わりません）。

`https://github.com/<ユーザー名>/SFTunePlayer/releases/latest/download/SFTunePlayer-Setup.exe`

インストーラーは VST3 を `C:\Program Files\Common Files\VST3\` に、単体アプリを `Program Files` に入れます。

### 修正版を出すとき
コードを直したら、バージョン番号を 1 つ上げて実行するだけです。
```bat
release.bat 1.0.1
```
GitHub がインストーラーをビルドし直して、最新の Release として公開します。

### ユーザー側での更新
- プラグインを開くと、右上で新しいバージョンがあるかを自動で確認します（DAW を起動してから 1 回だけ）。
- 新しいバージョンがあれば **「Update now」** ボタンが表示されます。押すと、インストーラーをダウンロードして起動します。
- プラグインを DAW が読み込んでいる場合、インストーラーが DAW を閉じるよう案内します。インストールが終わったら DAW を起動し直してください。

### 注意
- インストーラーにはコード署名をしていないため、最初に「Windows によって PC が保護されました」という警告が出ます。「詳細情報」→「実行」で進めてください。警告を出さないようにするには、有料のコード署名証明書が必要です。
- タグを付けずに push した場合も、Actions のページからビルドしたファイルをダウンロードできます。その場合のバージョンは 0.0.0 になります。

## テスト

```bash
python3 Tests/make_assets.py Tests/assets
cmake -B build-test -DSFTP_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build-test --target EngineTest
./build-test/EngineTest_artefacts/Release/EngineTest Tests/assets
```

テストでは実際に音を書き出し、その周波数を測ります。確認している内容は次のとおりです。
- SF2 と SFZ で鳴らした音の高さ
- 19 平均律の .scl と、基準キー・基準周波数を変えたとき
- 純正律の .scl に白鍵だけを割り当てる .kbm を組み合わせたとき（割り当てのないキーが鳴らないことも確認）
- .tun（`[Exact Tuning]` が優先されること）
- ADSR のサスティンレベルとリリース、ループ、ファイル側のエンベロープ

`HostTest` は、ビルドした .vst3 を DAW と同じ方法で読み込み、状態を復元して音を鳴らすテストです。

## 未対応のもの
SF2 のモジュレーター・フィルター・LFO・モジュレーションエンベロープ・リバーブ／コーラスのセンド、SFZ のフィルター・LFO・`<curve>`・`*sine` などの内蔵ジェネレーター、.tun の `[Functional Tuning]`、MTS-ESP。
