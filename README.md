# chapter_exe for AviSynth+ and FFmpeg
## 概要
AviSynth+はVer3.5.0からNative Linuxをサポートした。  
これは[sogaani氏][1]がLinuxに移植された[chapter_exe][2]を  
Avisynth+3.5.xを使用するようにしたもの。  
また、WindowsとLinuxの両環境にてビルドおよび使用できる。

`future/dtvindex`ブランチでは従来のAVS入力に加えて、FFmpegが対応する動画ファイルを直接入力できる。  動画ファイルには[dtvindex][3]が作成する共通フレーム番号を使用する。

従来AviSynth+を使う利点は、デコード、フィルタ、フレーム単位のランダムアクセスをフレームサーバーへ任せられることにあった。  
本ブランチの映像は[dtvindex][3]の永続インデックスとフレーム読み込み API、音声は`FFmpegSource`の16bit PCM変換とPTS同期を使用する。  
既存の無音検索およびシーンチェンジ検出処理は変更していない。

dtvindex直接入力では、L-SMASH Works入力と同様に内部の`thin_audio_read`を既定で`0`にし、音声を連続して読み込む。  
`--thin`または`--serial`を明示した場合は、その指定を優先する。

[1]:https://github.com/sogaani
[2]:https://github.com/sogaani/JoinLogoScp/tree/master/chapter_exe
[3]:https://github.com/tobitti0/dtvindex

## 機能
無音検索＋シーンチェンジ(SC)検索を行い、無音・SC位置の情報を出力する。

## FFmpeg機能のAviSynth+入力との互換性

FFmpeg直接入力とdtvindex入力は、従来の無音検索およびシーンチェンジ検出を変更せず、入力部分だけを拡張している。

MPEG-2放送TS 3本をAviSynth+／L-SMASH Works入力と比較した場合、無音・シーンチェンジ位置は±30フレームの範囲で85.2%から95.5%が対応し、最終フレーム番号の差は0から3フレームだった。  
LogoframeおよびJoinLogoScpまで組み合わせた最終Trimは、放送TS 5本すべてでAviSynth+入力と同一になった。

この数値はMPEG-2放送TSに対する互換性の目安であり、異なるコーデック、破損状態、タイムスタンプ構成で同じ結果を保証するものではない。

## 使用方法
`chapter_exe`と`dtvindex`を同じ親ディレクトリへ配置し、srcで`make`を実行するとビルドできる。  
AviSynth+およびFFmpegの開発用ライブラリが必要です。
FFmpeg開発ライブラリで必要なものは次のとおり。  
```
libavformat-dev libavcodec-dev libavutil-dev libswscale-dev libswresample-dev
```
実行方法は次のとおり。
````
chapter_exe -v "画像ソースファイル" -a "音声ソースファイル" -o "出力先txt" -m 無音閾値 -s 連続フレーム数
````
`-v`には従来の`.avs`ファイル、またはTS、MP4、MKVなどの動画ファイルを指定できる。`.avs`は従来どおりAvisynth+で、それ以外はdtvindexを経由してFFmpegで読み込む。  
動画ファイルの初回読み込み時には同じ場所へ`.dtvi`を作成し、2回目以降は元ファイルとの整合性を検証して再利用する。

例:
````
chapter_exe -v "input.ts" -o "output.txt"
chapter_exe -v "input.avs" -o "output.txt"
````

詳細は[オリジナルのreadme][4]、[改造版のreadme][5]を参照してください。

[4]:https://github.com/tobitti0/chapter_exe/blob/master/chapter_exe%E8%AA%AD%E3%82%93%E3%81%A7%E3%81%AD.txt
[5]:https://github.com/tobitti0/chapter_exe/blob/master/%E6%94%B9%E9%80%A0%E7%89%88_%E8%AA%AD%E3%82%93%E3%81%A7%E3%81%AD.txt

## 著作表示

- オリジナルのchapter_exe: [ru氏][6]
- シーンチェンジ検出などの改造: [Yobi氏][8]、sysuzu氏
- Linux／AvxSynth対応: [sogaani氏][1]
- AviSynth+対応、FFmpeg直接入力、dtvindex連携などの拡張: tobitti0

`avisynth/avs_internal.c`はx264 projectの`avs.c`を基礎としている。  
ARM向けの`extras/sse2neon.h`は[DLTcollab/sse2neon][7]によるMIT Licenseのコードである。

[6]:https://github.com/rutice
[7]:https://github.com/DLTcollab/sse2neon
[8]:https://github.com/yobibi
