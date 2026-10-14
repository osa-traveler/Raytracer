# Raytracer

教科書の最初に出てくるような、シンプルな Whitted 式レイトレーサです。
glTF バイナリ(`.glb`)のシーンを読み込み、640x480 のフレームバッファに描画して SDL2 のウィンドウに表示します。

![render](docs/render.png)

## 機能

- GLB(glTF 2.0)ローダ(JSON パーサも自前実装。外部ライブラリは SDL2 のみ)
  - メッシュ、ノード変換(TRS / matrix / 子ノード)、PBR 材質(baseColor / metallic / roughness)、`KHR_materials_transmission`、カメラ
- BVH による高速化(約31万三角形のシーンを数秒で描画)
- レイトレーシング
  - 拡散反射 + Blinn-Phong、点光源、影(ガラスは薄い影)
  - 金属・低 roughness 材質の鏡面反射(フレネル)
  - ガラスの屈折(IOR 1.5、フレネル、全反射)
- マルチスレッドで走査線ごとに描画し、描画の進行をウィンドウに表示

## ビルドと実行

必要なもの: C++17 コンパイラ、CMake、SDL2(macOS なら `brew install sdl2 cmake`)

```sh
cmake -S . -B build
cmake --build build
./build/raytrace                    # 既定: Scene/Test/Untitled.glb
./build/raytrace Scene/Test/Beetle.glb   # 1967年式ビートルのシーン
./build/raytrace path/to/scene.glb  # 別のシーン
```

リポジトリのルートで実行してください。描画が終わると `output.ppm` が保存されます。
Esc キーまたはウィンドウを閉じると終了します。

## カメラとライトについて

- カメラ: GLB にカメラがあればそれを使います(横方向の視野を保ったまま 4:3 に描画)。なければ [src/main.cpp](src/main.cpp) の固定値を使います。
- ライト: GLB のライトはまだ読み込まず、点光源を固定位置に置いています。Blender のエリアライトは glTF に書き出されません。

## シーン

- `Scene/Test/Untitled.glb`: Blender で作成した検証用シーン(部屋、金属球、ガラス球)
- `Scene/Test/Beetle.glb`: 上のシーンの中央に、Blender で手作りした1967年式フォルクスワーゲン・ビートル風のモデルを置いたもの(簡易的な形状で、正確な再現ではありません)

## 今後の予定

- GLB のライトの読み込み
- 距離減衰、アンチエイリアシング、クリアコートなど材質の拡張
