# chapter_exe for AviSynth+ and FFmpeg
## 概要
AviSynth+はVer3.5.0からNative Linuxをサポートした。  
これは[sogaani氏][1]がLinuxに移植された[chapter_exe][2]を  
Avisynth+3.5.xを使用するようにしたもの。  
また、WindwosとLinuxの両環境にてビルドおよび使用できる。

`future/dtvindex`ブランチでは従来のAVS入力に加えて、FFmpegが対応する
動画ファイルを直接入力できる。動画ファイルには兄弟プロジェクトの
`dtvindex`が作成する共通フレーム番号を使用する。

従来AviSynth+を使う利点は、デコード、フィルタ、フレーム単位の
ランダムアクセスをフレームサーバーへ任せられることにあった。
FFmpeg直接入力ではこれらに加え、映像・音声PTSの同期、固定フレームレート化、
破損パケット、後方アクセスを入力側で扱う必要がある。本ブランチの
映像は`dtvindex`の永続インデックスとフレーム読み込み API、音声は
`FFmpegSource`の16bit PCM変換とPTS同期を使用する。既存の無音検索および
シーンチェンジ検出処理は変更していない。

dtvindex直接入力では、L-SMASH Works入力と同様に内部の
`thin_audio_read`を既定で`0`にし、音声を連続して読み込む。`--thin`または
`--serial`を明示した場合は、その指定を優先する。

[1]:https://github.com/sogaani
[2]:https://github.com/sogaani/JoinLogoScp/tree/master/chapter_exe

## 機能
無音検索＋シーンチェンジ(SC)検索を行い、無音・SC位置の情報を出力する。

## 使用方法
`chapter_exe`と`dtvindex`を同じ親ディレクトリへ配置し、srcで
makeしてください。
AviSynth+およびFFmpegの開発用ライブラリが必要です。
実行方法は次のとおりです。
````
chapter_exe -v "画像ソースファイル" -a "音声ソースファイル" -o "出力先txt" -m 無音閾値 -s 連続フレーム数
````
`-v`には従来の`.avs`ファイル、またはTS、MP4、MKVなどの動画ファイルを
指定できる。`.avs`は従来どおりAvisynth+で、それ以外はFFmpegで読み込む。
動画ファイルの初回読み込み時には同じ場所へ`.dtvi`を作成し、2回目以降は
元ファイルとの整合性を検証して再利用する。

例:
````
chapter_exe -v "input.ts" -o "output.txt"
chapter_exe -v "input.avs" -o "output.txt"
````

詳細は[オリジナルのreadme][3]、[改造版のreadme][4]を参照してください。

[3]:https://github.com/tobitti0/chapter_exe/blob/master/chapter_exe%E8%AA%AD%E3%82%93%E3%81%A7%E3%81%AD.txt
[4]:https://github.com/tobitti0/chapter_exe/blob/master/%E6%94%B9%E9%80%A0%E7%89%88_%E8%AA%AD%E3%82%93%E3%81%A7%E3%81%AD.txt

## 謝辞
オリジナルの作成者である[ru氏][5]、  
改造されたYobi氏、sysuzu氏、  
Linuxに移植されたsogaani氏  
に深く感謝いたします。

[5]:https://github.com/rutice

## メモ
avs\_internalはx264 projectのavs.cから一部抜粋し微修正したものである。  

ARM向けで使用しているsse2neonは下記のリポジトリのものである。  
[DLTcollab/ss2neon][6]  

[6]:https://github.com/DLTcollab/sse2neon
