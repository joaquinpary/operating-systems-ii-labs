FROM ubuntu:24.04 AS builder

# Install build dependencies
RUN apt-get update && apt-get install -y \
    ca-certificates \
    cmake \
    g++ \
    gcc \
    make \
    git \
    libsasl2-dev \
    libssl-dev \
    libpq-dev \
    pkg-config \
    zlib1g-dev \
    libomp-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# Copy source code
COPY CMakeLists.txt ./
COPY src/ ./src/
COPY include/ ./include/
COPY tests/ ./tests/

# Build argument to determine what to build (client or server)
ARG BUILD_TARGET=server
ARG ENABLE_OPENMP=OFF

# Build the project
RUN mkdir -p build && cd build && \
    cmake -DBUILD_TARGET=${BUILD_TARGET} -DENABLE_OPENMP=${ENABLE_OPENMP} .. && \
    make

# Runtime stage
FROM ubuntu:24.04

# Install runtime dependencies
RUN apt-get update && apt-get install -y \
    ca-certificates \
    libpq5 \
    libsasl2-2 \
    libssl3 \
    zlib1g \
    libomp5 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

ARG BUILD_TARGET=server

# Executable
COPY --from=builder /app/build/dhl_${BUILD_TARGET} /app/dhl_${BUILD_TARGET}

# Copy cjson library (needed by both client and server)
COPY --from=builder /app/build/_deps/cjson-build/libcjson*.so* /usr/local/lib/

# Copy emergency detection shared library (client only — harmless if absent on server)
RUN --mount=type=bind,from=builder,source=/app/build,target=/tmp/build \
    if ls /tmp/build/libemergency*.so* 2>/dev/null | grep -q .; then \
        cp /tmp/build/libemergency*.so* /usr/local/lib/; \
    fi

# Copy admin CLI shared library (server only — harmless if absent on client)
RUN --mount=type=bind,from=builder,source=/app/build,target=/tmp/build \
    if ls /tmp/build/libadmin_cli*.so* 2>/dev/null | grep -q .; then \
        cp /tmp/build/libadmin_cli*.so* /usr/local/lib/; \
    fi

# Create log directory following FHS
RUN mkdir -p /var/log/dhl

# Copy pqxx library only if it exists (server only)
RUN --mount=type=bind,from=builder,source=/app/build,target=/tmp/build \
    if [ -d /tmp/build/_deps/pqxx-build/src ]; then \
        cp /tmp/build/_deps/pqxx-build/src/libpqxx*.so* /usr/local/lib/ || true; \
    fi

# Copy MongoDB driver shared libraries built via CMake FetchContent (server only)
RUN --mount=type=bind,from=builder,source=/app/build,target=/tmp/build \
    find /tmp/build/_deps \( \
        -name 'libmongocxx*.so*' -o \
        -name 'libbsoncxx*.so*' -o \
        -name 'libmongoc2*.so*' -o \
        -name 'libbson2*.so*' -o \
        -name 'libmongoc-1.0*.so*' -o \
        -name 'libbson-1.0*.so*' \
    \) -exec cp -P {} /usr/local/lib/ \; 2>/dev/null || true

RUN ldconfig
