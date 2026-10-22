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


def default_device():
    """Where arrays are made: the GPU, as on Apple silicon (53l)."""
    return "Device(gpu, 0)"


def save_safetensors(path, arrays, metadata=None):
    """A real SafeTensors file (53m): each array its numbers, as F32 -- or
    I64 when every one is an integer -- laid end to end."""
    import json
    import struct

    def flat(values):
        for value in values:
            if isinstance(value, (list, tuple)):
                yield from flat(value)
            else:
                yield value

    header = {}
    if metadata:
        header["__metadata__"] = dict(metadata)
    data = b""
    for name, values in arrays.items():
        numbers = list(flat(values))
        integers = all(isinstance(value, int) for value in numbers)
        packed = struct.pack(("<%dq" if integers else "<%df") % len(numbers), *numbers)
        header[name] = {"dtype": "I64" if integers else "F32", "shape": [len(numbers)],
                        "data_offsets": [len(data), len(data) + len(packed)]}
        data += packed
    text = json.dumps(header).encode()
    with open(path, "wb") as out:
        out.write(struct.pack("<Q", len(text)))
        out.write(text)
        out.write(data)
