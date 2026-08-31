# frozen_string_literal: true
#
# **同じ** TVar を C 個の worker が奪い合う。STM の本題はここ: 競合すると
# トランザクションはやり直しになるので、並行度を上げるとスループットが伸びずに
# 落ちることがある。落ちるかどうかを実際に見る。
#
#   WORKERS=16 MODE=ractor ruby benchmark/contention.rb
#   MODE=thread ...        GVL 側（1:1）の対照
#
# 最後に必ず合計値を検算する（WORKERS*PER でなければ STM が壊れている）。
Warning[:experimental] = false
require_relative "lib/bench"
require "ractor/tvar"

WORKERS = bconc(Integer(ENV.fetch("WORKERS", 8)))
PER     = bscale(Integer(ENV.fetch("PER", 20_000)))
MODE    = ENV.fetch("MODE", "ractor")   # ractor | thread
OP      = ENV.fetch("OP", "atomically") # atomically | increment

puts RUBY_DESCRIPTION
tv = Ractor::TVar.new(0)
total = WORKERS * PER

work = lambda do |t, n, op|
  if op == "increment"
    n.times { t.increment(1) }
  else
    n.times { Ractor.atomically { t.value += 1 } }
  end
end

m =
  if MODE == "thread"
    bmeasure(total) { WORKERS.times.map { Thread.new { work.(tv, PER, OP) } }.each(&:join) }
  else
    gate = Ractor::Port.new
    done = Ractor::Port.new
    # 生成した瞬間に走り出すと仕事の大半が測定窓の外に出る。全員が ready を
    # 返してから go を配るまで待たせる。
    rs = WORKERS.times.map do
      Ractor.new(tv, gate, done, PER, OP) do |t, g, d, n, op|
        g << :ready
        Ractor.receive
        if op == "increment"
          n.times { t.increment(1) }
        else
          n.times { Ractor.atomically { t.value += 1 } }
        end
        d << :done
      end
    end
    WORKERS.times { gate.receive }
    bmeasure(total) do
      rs.each { _1.send(:go) }
      WORKERS.times { done.receive }
    end.tap { rs.each { _1.value rescue nil } }
  end

got = tv.value
puts brate(format("%s workers=%-3d %s", MODE, WORKERS, OP), m, unit: "txn",
           extra: format("sum=%d %s", got, got == total ? "OK" : "MISMATCH(expected #{total})"))
abort "STM lost updates" unless got == total
