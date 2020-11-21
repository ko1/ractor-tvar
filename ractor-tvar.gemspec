# frozen_string_literal: true

require_relative "lib/ractor/tvar/version"

Gem::Specification.new do |spec|
  spec.name          = "ractor-tvar"
  spec.version       = Ractor::TVar::VERSION
  spec.authors       = ["Koichi Sasada"]
  spec.email         = ["ko1@atdot.net"]

  spec.summary       = "Ractor::TVar"
  spec.description   = "Ractor::TVar"
  spec.homepage      = "https://github.com/ko1/ractor-tvar"
  spec.license       = "MIT"
  spec.required_ruby_version = Gem::Requirement.new(">= 2.3.0")
  spec.extensions = %w(ext/ractor/tvar/extconf.rb)

  # spec.metadata["allowed_push_host"] = "TODO: Set to 'http://mygemserver.com'"

  spec.metadata["homepage_uri"] = spec.homepage
  spec.metadata["source_code_uri"] = "https://github.com/ko1/ractor-tvar"
  spec.metadata["changelog_uri"] = "https://github.com/ko1/ractor-tvar"

  # Specify which files should be added to the gem when it is released.
  # The `git ls-files -z` loads the files in the RubyGem that have been added into git.
  spec.files = Dir.chdir(File.expand_path(__dir__)) do
    `git ls-files -z`.split("\x0").reject { |f| f.match(%r{\A(?:test|spec|features)/}) }
  end
  spec.require_paths = ["lib"]
  spec.add_development_dependency "rake"
  spec.add_development_dependency "rake-compiler"
end
