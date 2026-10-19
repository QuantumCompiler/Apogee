"""The stand-in's `diffusers.utils.load_image`: the path, held."""


class _Loaded:
    def __init__(self, path):
        self.path = path


def load_image(path):
    return _Loaded(path)
