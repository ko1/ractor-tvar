# frozen_string_literal: true

require "test_helper"

class Ractor::TVarTest < Test::Unit::TestCase
  test "VERSION" do
    assert do
      ::Ractor::TVar.const_defined?(:VERSION)
    end
  end

  test 'Ractor::TVar can has a value' do
    tv = Ractor::TVar.new(1)
    assert_equal 1, tv.value
  end

  test 'Ractor::TVar without initial value will return nil' do
    tv = Ractor::TVar.new
    assert_equal nil, tv.value
  end

  test 'Ractor::TVar can change the value' do
    tv = Ractor::TVar.new
    assert_equal nil, tv.value
    Ractor::atomically do
      tv.value = :ok
    end
    assert_equal :ok, tv.value
  end

  test 'Ractor::TVar update without atomically will raise an exception' do
    tv = Ractor::TVar.new
    assert_raise  Ractor::TransactionError do
      tv.value = :ng
    end
  end

  test 'Ractor::TVar#increment increments the value' do
    tv = Ractor::TVar.new(0)
    tv.increment
    assert_equal 1, tv.value

    tv.increment 2
    assert_equal 3, tv.value

    Ractor::atomically do
      tv.increment 3
    end
    assert_equal 6, tv.value

    Ractor::atomically do
      tv.value = 1.5
    end
    tv.increment(-1.5)
    assert_equal 0.0, tv.value
  end

  test 'Ractor::TVar can not set the unshareable value' do
    assert_raise ArgumentError do
      Ractor::TVar.new [1]
    end
  end

  ## with Ractors
  N = 10_000
  test 'Ractor::TVar consistes with other Ractors' do
    tv = Ractor::TVar.new(0)
    rs = 4.times.map{
      Ractor.new tv do |tv|
        N.times{ Ractor::atomically{ tv.increment } }
      end
    }
    rs.each{|r| r.take}
    assert_equal N * 4 , tv.value
  end
end
