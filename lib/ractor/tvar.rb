# frozen_string_literal: true

require "ractor/tvar/version"
require 'ractor/tvar/ractor_tvar.so'

class Ractor
  class TVar
    def __increment__ inc
      Ractor::atomically do
        self.value += inc
      end
    end
  end
end
