class Base:
    def describe(self) -> str:
        return "base"


class User(Base):
    def __init__(self, name: str):
        self.name = name

    def greet(self) -> str:
        return "hello " + self.describe()
