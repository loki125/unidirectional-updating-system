FROM debian:bookworm-slim

RUN apt-get update && apt-get install -y \
    iptables \
    iputils-ping \
    iputils-arping \
    ethtool \
    iproute2 \
    smcroute \
    procps \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY start.sh .

RUN chmod +x start.sh

CMD ["./start.sh"]