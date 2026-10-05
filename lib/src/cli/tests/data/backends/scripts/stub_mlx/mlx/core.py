"""The stub's `mlx.core`: an array is the list it was made from, and a seed
is recorded rather than used."""

from mlx_lm._stub import record


class Array(list):
    """A list that takes `[None]`, as a prompt is batched for a forward pass."""

    def __getitem__(self, index):
        if index is None:
            return [list(self)]
        return list.__getitem__(self, index)


def array(values):
    return Array(values)


def eval(*_):  # noqa: A001 -- mlx's name
    pass


class _Random:
    @staticmethod
    def seed(value):
        record(f"seed {value}")


random = _Random()
