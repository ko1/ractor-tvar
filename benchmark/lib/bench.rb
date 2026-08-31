# frozen_string_literal: true
#
# このディレクトリ共通の計測レイヤ。規約は ~/ruby/src/rlgc/benchmark/ と同じ:
# **1 ワークロードにつき harness は 1 本、その 1 本が wall / 呼び手 CPU /
# プロセス CPU / RSS / GC を全部出す。** 指標ごとにベンチを分けない。
#
#   m = bmeasure(n) { n.times { obj.find("x") } }
#   puts brate("sync", m, unit: "call")

def bscale(n)
  [(n * (Float(ENV["BENCH_SCALE"] || 100) / 100)).round, 1].max
end

# warm-up も縮める。縮めないと BENCH_SCALE=1 で warm-up が本測定を上回る。
def bwarm(n) = [bscale(n), [n, 200].min].max

# 並行度の既定値。BENCH_C が全 harness 共通のノブ。
def bconc(default) = Integer(ENV["BENCH_C"] || default)

def brss_kb
  File.foreach("/proc/self/status") { |l| return l.split[1].to_i if l.start_with?("VmRSS:") }
  0
end

BMeasure = Struct.new(:n, :wall, :caller_cpu, :proc_cpu, :rss_delta_kb, :gc_ms, :gc_count, :value) do
  def per_op_us = wall / n * 1e6
  def caller_us = caller_cpu / n * 1e6
  def proc_us   = proc_cpu / n * 1e6
  def per_sec   = n / wall
  def kb_per_op = rss_delta_kb.to_f / n
  def cores     = proc_cpu / wall
end

def bmeasure(n = 1)
  GC.start
  rss0 = brss_kb
  gc_t0 = GC.stat(:time)
  gc_n0 = GC.stat(:count)
  c0 = Process.clock_gettime(Process::CLOCK_THREAD_CPUTIME_ID)
  p0 = Process.clock_gettime(Process::CLOCK_PROCESS_CPUTIME_ID)
  t0 = Process.clock_gettime(Process::CLOCK_MONOTONIC)
  value = yield
  t1 = Process.clock_gettime(Process::CLOCK_MONOTONIC)
  p1 = Process.clock_gettime(Process::CLOCK_PROCESS_CPUTIME_ID)
  c1 = Process.clock_gettime(Process::CLOCK_THREAD_CPUTIME_ID)
  BMeasure.new(n, t1 - t0, c1 - c0, p1 - p0, brss_kb - rss0,
               GC.stat(:time) - gc_t0, GC.stat(:count) - gc_n0, value)
end

def bline(label, m, unit: "call", extra: nil)
  format("%-24s n=%-8d %9.3f us/%s  caller %8.3f  proc %5.2f cores  rss %+7d KB  gc %4d ms/%-4d%s",
         label, m.n, m.per_op_us, unit, m.caller_us, m.cores, m.rss_delta_kb,
         m.gc_ms, m.gc_count, extra ? "  #{extra}" : "")
end

def brate(label, m, unit: "call", extra: nil)
  format("%-24s n=%-8d %11.0f %s/s  %9.3f us/%s  caller %8.3f  proc %5.2f cores  rss %+7d KB%s",
         label, m.n, m.per_sec, unit, m.per_op_us, unit, m.caller_us, m.cores,
         m.rss_delta_kb, extra ? "  #{extra}" : "")
end
