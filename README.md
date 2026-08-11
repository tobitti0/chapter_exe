# chapter_exe for AviSynth+ and dtvindex

## 概要

AviSynth+はVer3.5.0からNative Linuxをサポートした。  
これは[sogaani氏][1]がLinuxに移植された[chapter_exe][2]をAviSynth+3.5.xで動作するように改造し、Windows版のAviSynth入力にも対応したものである。
また、WindowsとLinuxの両環境にてビルドおよび使用できる。

従来のAVS入力に加えて、FFmpegが対応する動画ファイルを
[dtvindex][3]経由で入力できる。dtvindexがFFmpegで映像をデコードし、
作成した共通フレーム番号を使用する。

従来AviSynth+を使う利点は、デコード、フィルタ、フレーム単位のランダムアクセスをフレームサーバーへ任せられることにあった。  
動画ファイル入力の映像は[dtvindex][3]の永続インデックスとフレーム読み込みAPI、
音声は内部のFFmpeg音声リーダーによる16bit PCM変換とPTS同期を使用する。
既存の無音検索およびシーンチェンジ検出処理は変更していない。

dtvindex経由の動画ファイル入力では、L-SMASH Works入力と同様に
内部の`thin_audio_read`を既定で`0`にし、音声を連続して読み込む。
`--thin`または`--serial`を明示した場合は、その指定を優先する。

[1]:https://github.com/sogaani
[2]:https://github.com/sogaani/JoinLogoScp/tree/master/chapter_exe
[3]:https://github.com/tobitti0/dtvindex

## 機能

無音検索＋シーンチェンジ(SC)検索を行い、無音・SC位置の情報を出力する。

## 使用方法

`src`で`make`を実行すると、利用可能な入力機能を自動検出してビルドする。

```console
make
```

AviSynth入力に必要なC APIヘッダーと動的ロード処理はソースツリーに同梱している。
ビルド時にAviSynthの開発用ヘッダーやライブラリは不要で、実行時にWindowsでは
`avisynth.dll`、LinuxではAviSynth+の共有ライブラリを読み込む。
dtvindex経由の動画ファイル入力は次の順序で検出する。

1. `DTVINDEX_DIR`で指定したソースツリー
2. `src/libdtvindex.a`と`src/include/dtvindex/dtvindex.hpp`
3. `pkg-config`で検出できるインストール済みdtvindex
4. `chapter_exe`と同じ親ディレクトリにあるdtvindexソースツリー

dtvindex経由の動画ファイル入力に必要なFFmpeg開発ライブラリは次のとおり。

```
libavformat-dev libavcodec-dev libavutil-dev libswscale-dev libswresample-dev
```

入力機能は明示的に有効化または無効化できる。

```console
make WITH_AVISYNTH=yes WITH_DTVINDEX=no
make WITH_AVISYNTH=no WITH_DTVINDEX=yes
make DTVINDEX_DIR=/path/to/dtvindex
```

### モーション検索のSIMD

モーション検索はビルド対象に応じて、x86/x86_64ではSSE2、
ARM/ARM64ではNEONを使用する。どちらも使用できない環境では
同じ計算を行うscalar実装へフォールバックする。
既定の`auto`以外を明示してビルドすることもできる。

```console
make SIMD_BACKEND=auto
make SIMD_BACKEND=sse2
make SIMD_BACKEND=neon
make SIMD_BACKEND=scalar
```

実際に選ばれたバックエンドは起動時の`Motion SIMD :`表示で確認できる。

### Windows版のビルド

64 bit版MinGW-w64、`mingw32-make`、Windows向けに静的ビルドした
FFmpegの開発ファイルを用意し、その`pkg-config`定義を検索できる状態で
`src`ディレクトリから次を実行する。

```bat
compile.cmd
```

このコマンドは64 bit Windows向けの`chapter_exe.exe`を作成する。
MinGWのランタイム、FFmpeg、dtvindexは静的リンクされるため、実行時に
別途必要になるのはWindowsのシステムDLLと、AVS入力時のAviSynth+だけである。
AviSynth+は64 bit版をインストールするか、`AviSynth.dll`を
`chapter_exe.exe`と同じディレクトリへ配置する。

起動時には、ビルドで有効になった入力機能を表示する。

```text
chapter_exe
  Input       : AviSynth=enabled, dtvindex=enabled
  Motion SIMD : SSE2
```

ヘッダー、設定、進捗、警告、エラーは標準エラー出力へ表示する。
通常実行時の標準出力は使用せず、Chapter解析結果は`-o`で指定した
ファイルへ出力する。

実行方法は次のとおり。

```console
chapter_exe -v 動画またはAVSファイル名 [-a 音声ソースファイル名] -o 出力ファイル名 他オプション
```

`-v`には従来の`.avs`ファイル、またはTS、MP4、MKVなどの動画ファイルを
指定できる。`.avs`は従来どおりAviSynth+で読み込み、それ以外は
dtvindex経由の動画ファイル入力となる。dtvindexはFFmpegで映像をデコードする。
動画ファイルの初回読み込み時には同じ場所へ`.dtvi`を作成し、2回目以降は
元ファイルとの整合性を検証して再利用する。
放送途中のサービス切替などで複数の映像・音声ストリームが含まれるTSでは、
dtvindexが映像パケット数の多い本体タイムラインを選び、対応する放送音声を読み込む。
FFmpegが破損または不完全と判定した音声フレームは警告を表示して読み飛ばし、
後続の有効な音声から解析を継続する。

```console
chapter_exe -v input.ts -o output.txt
chapter_exe -v input.avs -o output.txt
```

詳細は[オリジナルのreadme][4]、[改造版のreadme][5]を参照してください。

[4]:https://github.com/tobitti0/chapter_exe/blob/master/chapter_exe%E8%AA%AD%E3%82%93%E3%81%A7%E3%81%AD.txt
[5]:https://github.com/tobitti0/chapter_exe/blob/master/%E6%94%B9%E9%80%A0%E7%89%88_%E8%AA%AD%E3%82%93%E3%81%A7%E3%81%AD.txt

## 著作表示

- オリジナルのchapter_exe: [ru氏][6]
- シーンチェンジ検出などの改造: [Yobi氏][7]、sysuzu氏
- Linux／AvxSynth対応: [sogaani氏][1]
- AviSynth+対応、dtvindex経由の動画ファイル入力などの拡張: tobitti0

`avisynth/avs_internal.c`はx264 projectの`avs.c`を基礎としている。  

[6]:https://github.com/rutice
[7]:https://github.com/yobibi
