require_relative 'lib/greeter'
require 'json'

def run
  greeter = Greeting::Greeter.new
  puts greeter.greet('x').to_json
end

run
