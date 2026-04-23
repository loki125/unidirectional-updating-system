FROM python:3.12-slim

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    git curl iproute2 build-essential cmake ninja-build \
    libboost-system-dev libnl-3-dev libnl-genl-3-dev \
    libspdlog-dev libfmt-dev libtinyxml2-dev libconfig++-dev \
    libarchive-dev libssl-dev zlib1g-dev nlohmann-json3-dev \
    pybind11-dev python3-dev clang-tidy clang \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /backend_src

# Build rt-libflute (Static with PIC)
RUN mkdir -p deps && git clone https://github.com/5G-MAG/rt-libflute.git deps/rt-libflute
RUN cd deps/rt-libflute \
    && cmake -S . -B build -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_SHARED_LIBS=OFF \
    -DBUILD_TESTING=OFF \
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
    && cmake --build build

# Get httplib
RUN mkdir -p deps/httplib \
 && curl -L https://raw.githubusercontent.com/yhirose/cpp-httplib/master/httplib.h -o deps/httplib/httplib.h

# Copy Backend Source and Build C++ Module
COPY backend/ .
RUN cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
    && cmake --build build -j 2

# Install the compiled C++ module so Python can import it
RUN cp build/core*.so /usr/local/lib/python3.12/site-packages/

WORKDIR /app

# Copy Frontend (Python) Code
COPY frontend/requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt
COPY frontend/ . 

ENV PYTHONUNBUFFERED=1
ENV PYTHONPATH=/app

EXPOSE 8080

CMD ["uvicorn", "main:app", "--host", "0.0.0.0", "--port", "8080"]