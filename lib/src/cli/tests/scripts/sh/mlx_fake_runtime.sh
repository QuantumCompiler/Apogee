# Sourced, not run: a fake MLX runtime for the scripts that drive the real
# binary through the mlx backend (27a) with nothing installed.
#
# The backend's ladder asks files: an interpreter under training/venv, and
# mlx-lm's package beneath its site-packages. This lays out exactly that --
# the interpreter a shim exec'ing the host's python3 with the stub `mlx` and
# `mlx_lm` packages (tests/data/backends/scripts/stub_mlx) on its path -- so
# the REAL seeded driver runs against the stubs. Nothing is installed and no
# model is loaded.

# mlx_fake_runtime_supported: the host can run it -- Apple silicon, the only
# target the backend runs on, and a python3 to run the driver with.
mlx_fake_runtime_supported() {
    [ "$(uname -s)" = "Darwin" ] && [ "$(uname -m)" = "arm64" ] &&
        command -v python3 >/dev/null 2>&1
}

# mlx_fake_runtime <apogee-home> <stub-scripts-dir>
mlx_fake_runtime() {
    local home="$1" stubs="$2"
    local site="$home/training/venv/lib/python3.99/site-packages"
    mkdir -p "$home/training/venv/bin" "$site"
    cp -R "$stubs/stub_mlx/." "$site/"
    cat >"$home/training/venv/bin/python" <<EOF
#!/bin/sh
PYTHONPATH="$site" PYTHONDONTWRITEBYTECODE=1 exec python3 "\$@"
EOF
    chmod +x "$home/training/venv/bin/python"
}

# mlx_fake_vlm <apogee-home> <stub-scripts-dir>: lays the stub `mlx_vlm`
# (27c) beside the stub `mlx_lm` -- what `train setup --with mlx-vlm` would
# install -- so a vision model's turns load through it.
mlx_fake_vlm() {
    local home="$1" stubs="$2"
    cp -R "$stubs/stub_mlx_vlm/." "$home/training/venv/lib/python3.99/site-packages/"
}

# mlx_descendants <pid>: every process below <pid>, one per line.
mlx_descendants() {
    local child
    for child in $(pgrep -P "$1" 2>/dev/null); do
        echo "$child"
        mlx_descendants "$child"
    done
}
