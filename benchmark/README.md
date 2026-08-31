# benchmark/

`Ractor::TVar`（Ractor / Thread 向け STM）のベンチマーク。Ractor / M:N そのものを
測る汎用ベンチは `~/ruby/src/rlgc/benchmark/` にあり、このディレクトリはそこの
`apps.tsv` に登録してある（`runner/apps.sh` で一覧が出る）。

規約は汎用側と同じ: **1 ワークロードにつき harness は 1 本、その 1 本が
wall / 呼び手 CPU / プロセス CPU / RSS を全部出す**（`lib/bench.rb` の `bmeasure`）。
パラメータは env、`BENCH_SCALE=%` で仕事量、`BENCH_C` で並行度。

拡張のビルドが要ります（`rake compile` か `gem install ractor-tvar`）。ruby を
切り替えたら必ずビルドし直すこと。

## harness

| ファイル | 問い | パラメータ（既定） |
|---|---|---|
| `txn_cost.rb` | トランザクション 1 回の値段（競合なし・単独） | `N=200000` `VARS=1` |
| `contention.rb` | **同じ** TVar を C 個の worker が奪い合う。STM の本題 | `WORKERS=8`(BENCH_C) `PER=20000` `MODE=ractor\|thread` `OP=atomically\|increment` |
| `scaling.rb` | worker ごとに**別の** TVar。競合ゼロの対照 | `WORKERS=8`(BENCH_C) `PER=20000` `MODE=ractor\|thread` |

## txn_cost.rb — 分母つき

| 行 | 何の床か |
|---|---|
| `plain ivar` | トランザクション無しの読み書き = 言語の床 |
| `mutex` | 同じ更新を Mutex で（競合の無い単独スレッドでの取得コスト） |
| `tvar read` | `atomically` の中で読むだけ |
| `tvar write` | `atomically` の中で 1 変数を read-modify-write |
| `tvar write xN` | `VARS>1` のとき。1 変数あたりの値段も出す（トランザクションの固定費と可変費の分離） |
| `increment` | ライブラリの `#increment` = 同じ更新の専用経路 |

## contention.rb と scaling.rb は対で読む

STM は競合するとトランザクションがやり直しになるので、**並行度を上げるとスループットが
伸びずに落ちることがある**。落ちるかどうかを実際に見るのが `contention.rb`、
競合ゼロならどこまで伸びるかが `scaling.rb` です。`proc cores` 列を必ず一緒に読んで
ください（CPU だけ食って仕事が増えていない、が STM のやり直しの見え方）。

`MODE=thread` は GVL 側（1:1）の対照。同じ STM が Ractor と Thread でどう違うか。

両方とも**最後に合計値を検算し、合わなければ `abort`** します（更新が失われる STM は
速くても意味がないので、正しさの oracle を測定に埋め込んである）。
