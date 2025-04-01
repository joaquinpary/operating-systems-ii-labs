FROM ubuntu:22.04

# Install tools
RUN apt update && apt install -y \
    python3-pip \
    cmake \
    g++ \
    make \
    git \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY . .

RUN mkdir -p build && \
    cd build && \
    cmake .. -DBUILD_TARGET=server && \
    make

RUN cd build && \
    cmake .. -DBUILD_TARGET=client && \
    make

COPY /scripts/run.sh /scripts/run.sh

RUN chmod +x /scripts/run.sh

CMD ["/scripts/run.sh"]


