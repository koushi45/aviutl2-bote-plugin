# Pawnhub

AviUtl2（ExEdit2）のレイアウトに「Pawnhub」を追加する汎用プラグインです。
キャラクターごとのalias、音声素材、画像・動画素材を登録し、現在フレームの指定レイヤーへ配置できます。

## 主な機能

- ポーンのアイコンを複数行表示（縦スクロール・上下表示領域のサイズ変更に対応）
- アイコンメニューからalias一括設置、VC一覧、画像・動画サムネイル一覧を表示
- ファイル名の部分一致検索
- 画像・動画サムネイルの全体表示と25〜300%の表示倍率変更
- 同種の画像・動画オブジェクトを選択中なら、新規配置せずファイルリンクを差し替え
- 音声のプレビュー再生
- Windowsのダークモードに追従するダーク表示
- 静止画を標準5秒、音声・動画を素材長準拠で配置
- GUI設定画面からポーン、素材フォルダ、配置レイヤー、aliasを編集
- 画像・動画・音声・テキスト・図形など、種類を問わず複数aliasを一括登録
- `alias-list` と旧案の `alias` の両方を読込可能

## ビルド

Visual Studio 2022の「C++によるデスクトップ開発」とCMakeを使用します。

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
```

生成物は `build/Release/Pawnhub.aux2` です。AviUtl2のプレビュー画面へD&Dするか、
アプリケーションデータフォルダ内の `Plugin` 以下へ配置してください。

## 設定

初回起動時、プラグインと同じ場所に `Pawnhub.json` が作成されます。
旧版の `porns_yagi.json` があれば設定内容を自動移行します。
「Pawnhub」ウィンドウのシステムメニューに追加される「設定」からGUI設定画面を開けます。
ポーンの追加・削除、各パスの選択、配置レイヤー、複数aliasを画面上で編集できます。

設定例は [sample.porns.json](sample.porns.json) を参照してください。レイヤー番号は画面表示と同じ1始まりです。

音声一覧では「▶ 再生」をクリックするとプレビューし、再生中は「■ 停止」で途中停止できます。ファイル名をクリックすると配置します。
画像・動画一覧ではサムネイルをクリックすると配置します。

## JSON形式

```json
{
  "porns": [
    {
      "name": "表示名",
      "icon": "C:\\path\\icon.png",
      "vc-directory": "C:\\path\\voice",
      "img-directory": "C:\\path\\images",
      "vc-recursive": false,
      "img-recursive": false,
      "vc-layer": 5,
      "img-layer": 4,
      "alias-list": [
        {
          "layer": 3,
          "alias": "C:\\path\\alias.object"
        }
      ]
    }
  ]
}
```

## 注意

- プレビュー再生可能な形式はWindowsのMCI対応コーデックに依存します。
- メディア配置可能な形式はAviUtl2側の入力プラグイン構成に依存します。
- 配置先に既存オブジェクトが重なる場合、AviUtl2 SDKの仕様により配置は失敗します。
