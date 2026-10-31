"""A stub of mlx-video's `save_video` (53l), `mlx_video.models.wan_2.postprocess`
at the commit Apogee pins: the real one encodes the frames into an mp4 itself
(imageio, then OpenCV). Apogee's driver puts its own in the generator's place,
so a clip this one writes means that seam was missed -- it writes a file the
suite looks for and must never find."""


def save_video(frames, output_path, fps=16):
    with open(output_path, "wb") as out:
        out.write(b"an mp4 mlx-video encoded itself, %d frames at %d fps" % (len(frames), fps))
