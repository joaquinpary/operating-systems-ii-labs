FROM ubuntu:24.04 AS builder

# Install build dependencies
RUN apt-get update && apt-get install -y \
    cmake \
    g++ \
    gcc \
    make \
    git \
    libpq-dev \
    pkg-config \
    zlib1g-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# Copy source code
COPY CMakeLists.txt ./
COPY src/ ./src/
COPY include/ ./include/
COPY tests/ ./tests/

# Build argument to determine what to build (client or server)
ARG BUILD_TARGET=server

# Build the project
RUN mkdir -p build && cd build && \
    cmake -DBUILD_TARGET=${BUILD_TARGET} .. && \
    make

# Runtime stage
FROM ubuntu:24.04

# Install runtime dependencies
RUN apt-get update && apt-get install -y \
    libpq5 \
    zlib1g \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

ARG BUILD_TARGET=server

# Executable
COPY --from=builder /app/build/dhl_${BUILD_TARGET} /app/dhl_${BUILD_TARGET}

# Libs
COPY --from=builder /app/build/_deps/pqxx-build/src/libpqxx*.so* /usr/local/lib/
COPY --from=builder /app/build/_deps/cjson-build/libcjson*.so* /usr/local/lib/

RUN ldconfig
