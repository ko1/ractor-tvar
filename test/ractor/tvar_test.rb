# frozen_string_literal: true

# The shim ships no implementation, so what these tests prove is the wiring:
# requiring this gem yields a working Ractor::TVar out of ractor-sharing.
require "test_helper"

class Ractor::TVarTest < Test::Unit::TestCase
  test "VERSION survives the move" do
    assert Ractor::TVar.const_defined?(:VERSION)
  end

  test "the implementation comes in through ractor-sharing" do
    tv = Ractor::TVar.new(1)
    assert_equal 1, tv.value
    Ractor.atomically { tv.value = 2 }
    assert_equal 2, tv.value
    assert_equal 3, tv.increment
  end

  test "it still works across Ractors" do
    tv = Ractor::TVar.new(0)
    rs = 4.times.map { Ractor.new(tv) { |t| 1000.times { Ractor.atomically { t.value += 1 } }; :ok } }
    rs.each(&:join)
    assert_equal 4000, tv.value
  end
end
