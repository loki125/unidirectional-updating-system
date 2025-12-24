# ================= C++ BUILD STAGE =================
FROM ubuntu:22.04 AS cpp-builder

RUN apt-get update && apt-get install -y \
    git \
    g++-12 \
    build-essential \
    cmake \
    ninja-build \
    pkg-config \
    libssl-dev \
    libcurl4-openssl-dev \
    libboost-all-dev \
    libspdlog-dev \
    libtinyxml2-dev  \
    libconfig++-dev \
    clang-tidy  \
    clang

# ---------------- deps ----------------
WORKDIR /build/deps
RUN git clone https://github.com/5G-MAG/rt-libflute.git

# ---------------- build libflute ----------------
WORKDIR /build/deps/rt-libflute
RUN cmake -S . -B build -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_TESTING=OFF \
    -DCMAKE_INSTALL_PREFIX=/usr/local \
    && cmake --build build \
    && cmake --install build

# ---------------- build your app ----------------
WORKDIR /build
COPY app/flute_broadcast/broadcaster .

RUN cmake -S . -B build -G Ninja && cmake --build build

# ================= PYTHON RUNTIME STAGE =================
FROM python:3.12-slim

RUN apt-get update && \
    apt-get install -y --no-install-recommends \
        net-tools \
        curl \
        wget \
        vim \
        libstdc++6 \
        libssl3 \
        libcurl4 \
    && rm -rf /var/lib/apt/lists/*

# Set working directory
WORKDIR /app

COPY requirements.txt .

# Install Python deps
RUN pip install --no-cache-dir -r requirements.txt

# copy broadcaster's binary
COPY --from=cpp-builder /build/ /usr/local/bin/broadcaster

ENV PYTHONUNBUFFERED=1

COPY app/ /app

EXPOSE 8000

#CMD ["uvicorn", "app.main:app", "--host", "0.0.0.0", "--port", "8000"]
ENTRYPOINT ["/usr/bin/tini", "--"]
CMD ["run.sh"]