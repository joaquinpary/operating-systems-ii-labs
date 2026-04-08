FROM golang:1.23-alpine AS builder

ARG TARGETOS=linux
ARG TARGETARCH=amd64

WORKDIR /src

COPY go.mod go.sum ./
RUN go mod download

COPY . .

RUN CGO_ENABLED=0 GOOS=${TARGETOS} GOARCH=${TARGETARCH} go build \
	-trimpath \
	-ldflags="-s -w" \
	-o /out/api_gateway \
	./cmd/server

FROM alpine:3.20

RUN apk add --no-cache ca-certificates wget

WORKDIR /app

COPY --from=builder /out/api_gateway /app/api_gateway

RUN mkdir -p /var/log/dhl

ENV HTTP_PORT=8081
ENV LOG_DIR=/var/log/dhl

EXPOSE 8081

ENTRYPOINT ["/app/api_gateway"]
