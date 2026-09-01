# frozen_string_literal: true

require_relative "lib/ractor/tvar/version"

Gem::Specification.new do |spec|
  spec.name          = "ractor-tvar"
  spec.version       = Ractor::TVar::VERSION
  spec.authors       = ["Koichi Sasada"]
  spec.email         = ["ko1@atdot.net"]

  spec.summary       = "Ractor::TVar (now part of ractor-sharing)"
  spec.description   = "Compatibility shim: Ractor::TVar moved into the ractor-sharing gem. " \
                       "Depending on ractor-tvar keeps working; the code comes from ractor-sharing."
  spec.homepage      = "https://github.com/ko1/ractor-tvar"
  spec.license       = "MIT"

  # ractor-sharing needs 4.0; Ruby 3.x resolves to ractor-tvar 0.4.0, the last
  # version with its own implementation.
  spec.required_ruby_version = ">= 4.0"

  spec.metadata["homepage_uri"] = spec.homepage
  spec.metadata["source_code_uri"] = "https://github.com/ko1/ractor-tvar"

  # Deliberately no lib/ractor/tvar.rb here: that file ships in ractor-sharing,
  # and require "ractor/tvar" must resolve there, not race two copies.
  spec.files = %w[lib/ractor-tvar.rb lib/ractor/tvar/version.rb README.md LICENSE.txt]
  spec.require_paths = ["lib"]

  spec.add_dependency "ractor-sharing", ">= 0.1.0"
end
