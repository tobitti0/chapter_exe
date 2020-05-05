# chapter_exe for AviSynth+ 3.5.x & Linux
## 概要
AviSynth+はVer3.5.0からNative Linuxをサポートした。  
これは[sogaani氏][1]がLinuxに移植された[chapter_exe][2]をAvisynth+3.5.xで動作するように改造したもの。

[1]:https://github.com/sogaani
[2]:https://github.com/sogaani/JoinLogoScp/tree/master/chapter_exe

## 機能
無音検索＋シーンチェンジ(SC)検索を行い、無音・SC位置の情報を出力する。

## 使用方法
AviSynth+3.5.xを導入の上srcでmakeしてください。  
導入していないとmakeに失敗し、動作しません。  
実行は次のとおりです。
````
chapter_exe -v "画像ソースファイル" -a "音声ソースファイル" -o "出力先txt" -m 無音閾値 -s 連続フレーム数
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
