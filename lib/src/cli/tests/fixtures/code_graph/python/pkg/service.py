import os

from .models import User


def make_user(name: str) -> User:
    return User(name)


def run(path: str) -> str:
    user: User = make_user(os.path.basename(path))
    return user.greet()
