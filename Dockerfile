FROM ubuntu:24.04

ARG TARGET

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
    valgrind \
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

COPY src/ /app/src/
COPY include/ /app/include/
COPY CMakeLists.txt /app/
COPY scripts/ /app/scripts/

RUN mkdir -p /var/log/dhl_client \
             /etc/dhl_client \
             /var/log/dhl_server \
             /etc/dhl_server

COPY config/clients_credentials.json /etc/dhl_client/
COPY config/server_credentials.json /etc/dhl_server/
COPY config/server_parameters.json /etc/dhl_server/

RUN chmod +x scripts/build.sh
RUN ./scripts/build.sh $TARGET

RUN chmod +x scripts/run.sh

ENTRYPOINT ["sh", "-c"]
CMD ["./scripts/run.sh \"$TARGET\""]
