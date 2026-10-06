# ── Build stage: fetch the Anubis binary from GitHub releases ──
FROM alpine:3.20 AS builder
RUN apk add --no-cache wget tar
RUN wget -qO /tmp/anubis.tar.gz \
      https://github.com/TecharoHQ/anubis/releases/download/v1.27.0/anubis-1.27.0-linux-amd64.tar.gz \
    && tar -xzf /tmp/anubis.tar.gz -C /tmp \
    && mv /tmp/anubis-1.27.0-linux-amd64/bin/anubis /usr/local/bin/anubis \
    && chmod +x /usr/local/bin/anubis

# ── Runtime stage ──
FROM alpine:3.20
RUN apk add --no-cache ca-certificates
COPY --from=builder /usr/local/bin/anubis /usr/local/bin/anubis
ENTRYPOINT ["anubis"]
