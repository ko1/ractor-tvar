# frozen_string_literal: true
#
# トランザクション 1 回の値段を、分母つきで出す。
#
#   ruby benchmark/txn_cost.rb
#
# 分母（これが無いと「STM は高いのか」に答えられない）:
#   plain ivar    トランザクション無しの読み書き = 言語の床
#   mutex         Mutex で同じ更新を守る（競合の無い単独スレッドでの取得コスト）
#   tvar read     atomically の中で読むだけ
#   tvar write    atomically の中で 1 変数を read-modify-write
#   increment     ライブラリ側の #increment（同じ更新の専用経路）
Warning[:experimental] = false
require_relative "lib/bench"
require "ractor/tvar"

N = bscale(Integer(ENV.fetch("N", 200_000)))
VARS = Integer(ENV.fetch("VARS", 1))   # 1 トランザクションが触る TVar の数

puts RUBY_DESCRIPTION
puts "1 トランザクションの値段  N=#{N}  VARS=#{VARS}  (競合なし・単独)"

box = Struct.new(:v).new(0)
bwarm(2000).times { box.v += 1 }
puts bline("plain ivar", bmeasure(N) { N.times { box.v += 1 } }, unit: "txn")

mtx = Mutex.new
bwarm(2000).times { mtx.synchronize { box.v += 1 } }
puts bline("mutex", bmeasure(N) { N.times { mtx.synchronize { box.v += 1 } } }, unit: "txn")

tvs = VARS.times.map { Ractor::TVar.new(0) }
tv = tvs.first

bwarm(2000).times { Ractor.atomically { tv.value } }
puts bline("tvar read", bmeasure(N) { N.times { Ractor.atomically { tv.value } } }, unit: "txn")

bwarm(2000).times { Ractor.atomically { tv.value += 1 } }
puts bline("tvar write", bmeasure(N) { N.times { Ractor.atomically { tv.value += 1 } } }, unit: "txn")

if VARS > 1
  bwarm(2000).times { Ractor.atomically { tvs.each { _1.value += 1 } } }
  m = bmeasure(N) { N.times { Ractor.atomically { tvs.each { _1.value += 1 } } } }
  puts bline("tvar write x#{VARS}", m, unit: "txn", extra: format("%.3f us/var", m.per_op_us / VARS))
end

bwarm(2000).times { tv.increment(1) }
puts bline("increment", bmeasure(N) { N.times { tv.increment(1) } }, unit: "txn")
