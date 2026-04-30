FROM debian:bookworm-slim

# Install procps (for sysctl) along with your other networking tools
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

# Copy your local start.sh script
COPY start.sh .

# Make it executable
RUN chmod +x start.sh

CMD ["./start.sh"]