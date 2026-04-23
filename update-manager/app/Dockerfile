FROM python:3.12-slim

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
    zlib1g-dev \
    nlohmann-json3-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /service

# Clone and build flute as STATIC
RUN mkdir -p deps && git clone https://github.com/5G-MAG/rt-libflute.git deps/rt-libflute
RUN cd deps/rt-libflute \
    && CC=gcc CXX=g++ cmake -S . -B build -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_SHARED_LIBS=OFF \
    -DBUILD_TESTING=OFF \
    && cmake --build build

# Clone httplib's header file
RUN mkdir -p deps/httplib \
 && curl -L \
  https://raw.githubusercontent.com/yhirose/cpp-httplib/master/httplib.h \
  -o deps/httplib/httplib.h


# Copy the source code
COPY backend/ . 

# Build application
RUN cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
    && cmake --build build

# Install
RUN cp build/core*.so /usr/local/lib/python3.12/site-packages/

# Install Python deps
COPY frontend/requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt

ENV PYTHONUNBUFFERED=1

COPY app/ /app

EXPOSE 8000

CMD ["uvicorn", "app.main:app", "--host", "0.0.0.0", "--port", "8000"]