# Ractor::TVar

[Software transactional memory](https://en.wikipedia.org/wiki/Software_transactional_memory) implementation for Ractor and Thread on Ruby 3.0.

```ruby
require 'ractor/tvar'

tv = Ractor::TVar.new(0)

N = 10_000

r = Ractor.new tv do |tv|
  N.times do
    Ractor.atomically do
      tv.value += 1
    end
   end
end

N.times do
  Ractor.atomically do
    tv.value += 1
  end
end

r.take # wait for the ractor

p tv.value #=> 20000 (= N * 2)
```

This script shows that there is no race between ractors.

## Installation

You need recent Ruby 3.0 (development).

Add this line to your application's Gemfile:

```ruby
gem 'ractor-tvar'
```

And then execute:

    $ bundle install

Or install it yourself as:

    $ gem install ractor-tvar

## Development

After checking out the repo, run `bin/setup` to install dependencies. Then, run `rake test-unit` to run the tests. You can also run `bin/console` for an interactive prompt that will allow you to experiment.

To install this gem onto your local machine, run `bundle exec rake install`. To release a new version, update the version number in `version.rb`, and then run `bundle exec rake release`, which will create a git tag for the version, push git commits and the created tag, and push the `.gem` file to [rubygems.org](https://rubygems.org).

## Contributing

Bug reports and pull requests are welcome on GitHub at https://github.com/ko1/ractor-tvar.


## License

The gem is available as open source under the terms of the [MIT License](https://opensource.org/licenses/MIT).
