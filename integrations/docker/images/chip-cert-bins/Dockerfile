# Stage 1: Setup dependencies (based on chip-build).
FROM ubuntu:24.04 AS chip-build-cert
LABEL org.opencontainers.image.source=https://github.com/project-chip/connectedhomeip
ARG TARGETPLATFORM
# COMMITHASH defines the target commit to build from. May be passed in using --build-arg.
ARG COMMITHASH=c1ec2d777456924dcaa59b53351b00d73caf378f

# Ensure TARGETPLATFORM is set
RUN case ${TARGETPLATFORM} in \
    "linux/amd64") \
    echo "Building for linux/amd64" \
    ;; \
    "linux/arm64") \
    echo "Building for linux/arm64" \
    ;; \
    *) \
    if [ -z "$TARGETPLATFORM" ] ;\
    then \
    echo "TARGETPLATFORM not defined! Please run from buildkit (buildx)." \
    && return 1 ;\
    else \
    echo "Unsupported platform ${TARGETPLATFORM}." \
    && return 1 ;\
    fi \
    ;; \
    esac

# Below should be the same as chip-build except arm64 logic for cmake and node.

# base build and check tools and libraries layer
RUN set -x \
    && apt-get update \
    && apt-get upgrade -y \
    && DEBIAN_FRONTEND=noninteractive apt-get install -fy \
    autoconf \
    automake \
    bison \
    bluez \
    bridge-utils \
    clang \
    clang-format \
    clang-tidy \
    cmake \
    curl \
    flex \
    g++ \
    gcc \
    git \
    git-lfs \
    gperf \
    iproute2 \
    jq \
    lcov \
    libavahi-client-dev \
    libavahi-common-dev \
    libcairo2-dev \
    libdbus-1-dev \
    libdbus-glib-1-dev \
    libdmalloc-dev \
    libgif-dev \
    libgirepository1.0-dev \
    libglib2.0-dev \
    libical-dev \
    libjpeg-dev \
    libmbedtls-dev \
    libncurses5-dev \
    libncursesw5-dev \
    libnspr4-dev \
    libpango1.0-dev \
    libpixman-1-dev \
    libpcsclite1 \
    libreadline-dev \
    libssl-dev \
    libtool \
    libudev-dev \
    libusb-1.0-0 \
    libusb-dev \
    libxml2-dev \
    make \
    net-tools \
    ninja-build \
    openjdk-8-jdk \
    pkg-config \
    pcscd \
    python3 \
    python3-dev \
    python3-pip \
    python3-venv \
    rsync \
    shellcheck \
    software-properties-common \
    strace \
    systemd \
    udev \
    unzip \
    wget \
    zlib1g-dev \
    && git lfs install \
    && : # last line

RUN set -x \
    && pip3 install --break-system-packages \
    attrs coloredlogs PyGithub pygit future portpicker mobly click cxxfilt ghapi pandas tabulate \
    && : # last line

# build and install gn
RUN set -x \
    && git clone https://gn.googlesource.com/gn \
    && cd gn \
    && python3 build/gen.py \
    && ninja -C out \
    && cp out/gn /usr/local/bin \
    && cd .. \
    && rm -rf gn \
    && : # last line

# Install bloat comparison tools
RUN set -x \
    && git clone https://github.com/google/bloaty.git \
    && mkdir -p bloaty/build \
    && cd bloaty/build \
    && cmake -G Ninja ../ \
    && ninja \
    && ninja install \
    && cd ../.. \
    && rm -rf bloaty \
    && : # last line

# Stage 1.5: Bootstrap Matter.
WORKDIR mkdir /root/connectedhomeip
RUN git init .
RUN git remote add origin https://github.com/project-chip/connectedhomeip.git
RUN git fetch --depth 1 ${COMMITHASH}
RUN git checkout ${COMMITHASH}
RUN ./scripts/checkout_submodules.py --allow-changing-global-git-config --shallow --platform linux
RUN bash scripts/bootstrap.sh

# Stage 2: Build.
FROM chip-build-cert AS chip-build-cert-bins

SHELL ["/bin/bash", "-c"]

# Records Matter SDK commit hash to include in the image.
RUN git rev-parse HEAD > /root/.sdk-sha-version

RUN case ${TARGETPLATFORM} in \
    "linux/amd64") \
    set -x \
    && source scripts/activate.sh \
    && scripts/build/build_examples.py \
   --target linux-x64-all-clusters-ipv6only \
    build \
    && mv out/linux-x64-all-clusters-ipv6only/chip-all-clusters-app out/chip-all-clusters-app \
    ;; \
    "linux/arm64")\
    set -x \
    && source scripts/activate.sh \
    && scripts/build/build_examples.py \
    --target linux-arm64-all-clusters-ipv6only \
    build \
    && mv out/linux-arm64-all-clusters-ipv6only/chip-all-clusters-app out/chip-all-clusters-app \
      ;; \
    *) ;; \
    esac

RUN source scripts/activate.sh && scripts/build_python.sh -m platform -d true -i out/python_env

# Stage 3: Copy relevant cert bins to a minimal image to reduce size.
FROM ubuntu:24.04
ENV TZ=Etc/UTC
RUN ln -snf /usr/share/zoneinfo/$TZ /etc/localtime && echo $TZ > /etc/timezone
RUN apt-get update -y
RUN apt-get install -y libssl-dev libdbus-1-dev libglib2.0-dev libavahi-client-dev avahi-utils iproute2 libcairo2-dev libgirepository1.0-dev python3-pip
WORKDIR /root/
COPY --from=chip-build-cert-bins /root/.sdk-sha-version .sdk-sha-version
RUN mkdir apps
COPY --from=chip-build-cert-bins /root/connectedhomeip/out/chip-all-clusters-app apps/chip-all-clusters-app

# Create symbolic links for now since this allows users to use existing configurations
# for running just `app-name` instead of `apps/app-name`
RUN ln -s apps/* .


# Stage 3.1: Setup the Matter Python environment
COPY --from=chip-build-cert-bins /root/connectedhomeip/out/python_lib python_lib
COPY --from=chip-build-cert-bins /root/connectedhomeip/out/python_env python_env
COPY --from=chip-build-cert-bins /root/connectedhomeip/src/python_testing python_testing/scripts/sdk
COPY --from=chip-build-cert-bins /root/connectedhomeip/data_model python_testing/data_model

COPY --from=chip-build-cert-bins /root/connectedhomeip/scripts/tests/requirements.txt /tmp/requirements.txt
RUN pip install --break-system-packages -r /tmp/requirements.txt && rm /tmp/requirements.txt

COPY --from=chip-build-cert-bins /root/connectedhomeip/src/python_testing/requirements.txt /tmp/requirements.txt
RUN pip install --break-system-packages -r /tmp/requirements.txt && rm /tmp/requirements.txt

# Stage 3.2: Setup the Mock Server
COPY --from=chip-build-cert-bins /root/connectedhomeip/integrations/mock_server mock_server

# PIP requires MASON package compilation, which seems to require a JDK
RUN set -x && DEBIAN_FRONTEND=noninteractive apt-get update; apt-get install -fy openjdk-8-jdk

# TODO: remove this dependency conflict workaround --> issue: #37975
# python3-gi is being installed as a dependency by 'openjdk-8-jdk'. python3-gi is not wanted since it installs PyGObject 3.48.2 (which we are not using),
# This issue showed up when we pinned pygobject==3.50.0 in the chip-repl in https://github.com/project-chip/connectedhomeip/pull/37948
# having pygobject ==3.50.0 being installed through pip creates a conflict, and pip can not unintstall the PyGObject (python3-gi's version) because the system APT installed it
# Error log:
#   ERROR: Cannot uninstall 'PyGObject'. It is a distutils installed project and thus we cannot accurately determine which files belong to it which would lead to only a partial uninstall.
RUN apt-get remove -y python3-gi

RUN pip install --break-system-packages --no-cache-dir \
    python_lib/obj/src/python_testing/matter_testing_infrastructure/chip-testing._build_wheel/chip_testing-*.whl \
    python_lib/controller/python/chip*.whl

# Copy device attestation revocation set and device attestation test vectors
RUN mkdir -p credentials/test/revoked-attestation-certificates
COPY --from=chip-build-cert-bins /root/connectedhomeip/credentials/test/revoked-attestation-certificates/dac-provider-test-vectors credentials/test/revoked-attestation-certificates/dac-provider-test-vectors
COPY --from=chip-build-cert-bins /root/connectedhomeip/credentials/test/revoked-attestation-certificates/revocation-sets credentials/test/revoked-attestation-certificates/revocation-sets
