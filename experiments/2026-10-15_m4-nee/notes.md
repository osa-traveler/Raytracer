# M4: NEE の効果(Cornell Box、8回反射、ジッタ)

`run.sh` で再生成(基準画像は NEE 1024 spp、シード 99。RMSE の計算には、`.pfm` が必要で、`run.sh` が出力する)。
画像は `docs/screenshots/2026-10-15_cornell-path-nee-*` と `...-no-nee-*`。

| spp | RMSE(NEE あり) | RMSE(NEE なし) |
|---|---|---|
| 1 | 0.179 | 0.819 |
| 4 | 0.093 | 0.410 |
| 16 | 0.047 | 0.205 |
| 64 | 0.025 | 0.103 |
| 256 | 0.013 | 0.052 |

同じ spp で、誤差は約 1/4。NEE は1サンプルあたり約 1.5 倍遅い(Release で 256 spp: 約 26 s / 17 s)。NEE の 16 spp は、NEE なしの 256 spp と同等で、時間は約 1/10。
