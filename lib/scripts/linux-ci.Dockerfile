# The Linux the unit-test runners are (CI's ubuntu-24.04 and ubuntu-24.04-arm),
# for lib/scripts/linux-unit-tests.sh: Ubuntu 24.04's GCC, libstdc++ and
# glibc, and the one package the workflow installs on its Linux jobs
# (libcurl4-openssl-dev). The rest is what the runner image already has and
# the build reaches for: cmake, git, pkg-config, python3.
FROM ubuntu:24.04

RUN apt-get update \
    && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
        build-essential ca-certificates cmake git libcurl4-openssl-dev pkg-config \
        python3 python3-venv \
    && rm -rf /var/lib/apt/lists/*
