FROM gcc:12

RUN apt-get update && apt-get install -y \
    git \
    curl \
    iproute2 \
    build-essential \
    cmake \
    ninja-build \
    libboost-system-dev \
    libnl-3-dev \
    libnl-genl-3-dev \
    libspdlog-dev \
    libtinyxml2-dev \
    libconfig++-dev \
    clang-tidy \
    clang \
    libssl-dev \
    nlohmann-json3-dev \
    libstdc++-12-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /service

# Clone rt-libflute library directly into service workspace
RUN mkdir -p deps && git clone https://github.com/5G-MAG/rt-libflute.git deps/rt-libflute

#Clone httplib's header file
RUN mkdir -p deps/httplib \
 && curl -L \
  https://raw.githubusercontent.com/yhirose/cpp-httplib/master/httplib.h \
  -o deps/httplib/httplib.h


COPY service/ .

# Build everything
RUN cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
    && cmake --build build

RUN cp build/broadcaster /usr/local/bin/

# Set the command to run the executable when the container starts.
ENTRYPOINT ["/usr/local/bin/broadcaster"]