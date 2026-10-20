"""A stand-in torch for the video driver's suite (49b): no tensors, only what
video_generate.py asks of it -- the devices, the two types it runs weights
at, and a seeded generator. STUB_VIDEO_DEVICE says which device "exists"
(cuda, mps); none means the CPU."""

import os

__version__ = "0.0-stub"

bfloat16 = "bfloat16"
float16 = "float16"
float32 = "float32"


class _Cuda:
    @staticmethod
    def is_available():
        return os.environ.get("STUB_VIDEO_DEVICE") == "cuda"


class _Mps:
    @staticmethod
    def is_available():
        return os.environ.get("STUB_VIDEO_DEVICE") == "mps"


class _Backends:
    mps = _Mps()


cuda = _Cuda()
backends = _Backends()


class Generator:
    def __init__(self, device="cpu"):
        self.device = device
        self.seed = None

    def manual_seed(self, seed):
        self.seed = seed
        return self
