FROM ubuntu:24.04

RUN apt-get update && apt-get install -y \
    cmake \
    g++ \
    make \
    git \
    pkg-config \
    libpq-dev \
    zlib1g-dev \
    libcjson-dev \
    lsof \
    net-tools \
    && rm -rf /var/lib/apt/lists/*

RUN git clone https://github.com/jtv/libpqxx.git /tmp/libpqxx && \
    cd /tmp/libpqxx && \
    mkdir build && cd build && \
    cmake .. -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_STANDARD=20 && \
    make -j$(nproc) && \
    make install && \
    ldconfig && \
    rm -rf /tmp/libpqxx

WORKDIR /app

COPY . .

RUN chmod +x scripts/build.sh
RUN ./scripts/build.sh

RUN chmod +x scripts/run.sh

CMD ["./scripts/run.sh"]