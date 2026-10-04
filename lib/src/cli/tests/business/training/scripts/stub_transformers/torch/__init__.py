"""A stub `torch` for the protocol tests: tensors are lists, devices are
strings, nothing computes."""

import contextlib

from . import utils  # noqa: F401  (torch.utils.data.Dataset)

bfloat16 = "bfloat16"
float16 = "float16"


class _Cuda:
    @staticmethod
    def is_available():
        return False

    @staticmethod
    def is_bf16_supported():
        return False


cuda = _Cuda()


class _Tensor(list):
    def to(self, _device):
        return self


def tensor(value):
    return _Tensor(value)


@contextlib.contextmanager
def no_grad():
    yield
