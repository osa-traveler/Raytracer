# サードパーティのコード

このリポジトリには、他人のソースコードは含めていない。ビルド時に取得する・システムのものを使う、次のものに依存している。

| 名前 | 用途 | ライセンス | 入手方法 |
|---|---|---|---|
| [Dear ImGui](https://github.com/ocornut/imgui) v1.91.9 | 操作パネル(GUI) | MIT | CMake の設定時に、GitHub のリリースアーカイブ `https://github.com/ocornut/imgui/archive/refs/tags/v1.91.9.tar.gz` を自動取得し、`build/_deps/` に展開する。SHA-256 を `CMakeLists.txt` で固定して検証する(下記)。リポジトリには含めない |
| [SDL2](https://www.libsdl.org/) | ウィンドウ、描画の土台、入力 | zlib | Homebrew(`brew install sdl2`)。動的リンク |

## Dear ImGui の取得と検証

- 固定している SHA-256: `3872a5f90df78fced023c1945f4466b654fd74573370b77b17742149763a7a7c`(`imgui-1.91.9.tar.gz`、1,846,168 バイト)。
- この値は、**初回にダウンロードしたアーカイブから計算したもの**(2026-10-15)。公式が公開しているハッシュとの照合まではしていない。以後は、同じ内容でなければ CMake が失敗する(改ざんや差し替えの検出)。
- 使っているのは、コア(`imgui.cpp`、`imgui_draw.cpp`、`imgui_tables.cpp`、`imgui_widgets.cpp`)と、SDL2 / SDL_Renderer 用の部品(`backends/imgui_impl_sdl2.cpp`、`backends/imgui_impl_sdlrenderer2.cpp`)だけ。
- バージョンを上げるときは、`CMakeLists.txt` の URL とハッシュを一緒に更新し、`./build/raytrace --gui-selftest` が通ることを確認する(1.92 以降は、テクスチャの扱いが変わるので注意)。

## ライセンス表記

MIT ライセンス(Dear ImGui)は、ソースまたは実行ファイルを再配布するときに、著作権表示とライセンス文を同梱する必要がある。現在は、リポジトリにも実行ファイルの配布物にも ImGui を含めていないので、同梱は不要。実行ファイルを配布するようになったら、ここを更新する。
