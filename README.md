# Ractor::TVar

**As of 0.5.0 this gem is a compatibility shim.** `Ractor::TVar` moved into
[ractor-sharing](https://github.com/ko1/ractor-sharing), together with its
siblings (`Ractor::LockVar`, `Ractor::LockHash`, `Ractor::ActiveObject`,
`Ractor::ActorHash`). This gem now contains no code of its own: it depends on
ractor-sharing, and `require "ractor/tvar"` loads the implementation from
there. Nothing changes for existing users:

```ruby
gem "ractor-tvar"        # keeps working, now pulls in ractor-sharing
require "ractor/tvar"    # keeps working, served by ractor-sharing
```

On Ruby 3.x the resolver stays on ractor-tvar 0.4.0, the last version with an
implementation of its own; 0.5.0 and later require Ruby 4.0.

Documentation: [ractor-sharing's docs/tvar.md](https://github.com/ko1/ractor-sharing/blob/master/docs/tvar.md).

## License

MIT. See [LICENSE.txt](LICENSE.txt).
