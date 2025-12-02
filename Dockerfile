FROM ubuntu:22.04

RUN apt-get update && apt-get install -y \
    cmake \
    g++ \
    make \
    git \
    libpq-dev \
    postgresql-client \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY / /

RUN chmod +x /scripts/build.sh
RUN /scripts/build.sh

RUN chmod +x /scripts/run.sh

CMD ["/scripts/run.sh"]