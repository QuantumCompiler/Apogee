module Greeting
  class Base
    def prefix
      "hello"
    end
  end

  class Greeter < Base
    def greet(name)
      format_name(prefix, name)
    end

    def format_name(head, name)
      "#{head} #{name.capitalize}"
    end
  end
end
