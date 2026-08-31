# frozen_string_literal: true
#
# contention.rb と対で読む。こちらは worker ごとに **別の** TVar を持たせるので
# 競合がゼロ。伸びなければ天井は STM ではなくスケジューラ側にある。
#
#   WORKERS=16 MODE=ractor ruby benchmark/scaling.rb
Warning[:experimental] = false
require_relative "lib/bench"
require "ractor/tvar"

WORKERS = bconc(Integer(ENV.fetch("WORKERS", 8)))
PER     = bscale(Integer(ENV.fetch("PER", 20_000)))
MODE    = ENV.fetch("MODE", "ractor")

puts RUBY_DESCRIPTION
tvs = WORKERS.times.map { Ractor::TVar.new(0) }
total = WORKERS * PER

m =
  if MODE == "thread"
    bmeasure(total) do
      tvs.map { |t| Thread.new { PER.times { Ractor.atomically { t.value += 1 } } } }.each(&:join)
    end
  else
    gate = Ractor::Port.new
    done = Ractor::Port.new
    rs = tvs.map do |t|
      Ractor.new(t, gate, done, PER) do |tv, g, d, n|
        g << :ready
        Ractor.receive
        n.times { Ractor.atomically { tv.value += 1 } }
        d << :done
      end
    end
    WORKERS.times { gate.receive }
    bmeasure(total) do
      rs.each { _1.send(:go) }
      WORKERS.times { done.receive }
    end.tap { rs.each { _1.value rescue nil } }
  end

sum = tvs.sum(&:value)
puts brate(format("%s tvars=%-3d (no conflict)", MODE, WORKERS), m, unit: "txn",
           extra: format("sum=%d %s", sum, sum == total ? "OK" : "MISMATCH"))
abort "STM lost updates" unless sum == total
