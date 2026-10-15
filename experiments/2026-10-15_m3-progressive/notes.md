# M3: プログレッシブ描画のタイムラプス

`run.sh` で再生成。Cornell Box、NEE、8回反射、ジッタ、シード 1。連番画像は `docs/screenshots/2026-10-15_cornell-progressive-nee-*.png`。

パスの完了時刻(累積、Debug ビルド): 1 spp 1.0 s / 4 spp 4.3 s / 16 spp 16.9 s / 64 spp 68.4 s / 256 spp 286.3 s。一括描画(256 spp)は 345.6 s。
**段階描画と一括描画の PNG は、バイト単位で同一**(確認済み)。
