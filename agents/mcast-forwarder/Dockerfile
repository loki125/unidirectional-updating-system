FROM gcc:12 as builder

WORKDIR /build
COPY router.cpp .

RUN g++ -O3 -static-libgcc -static-libstdc++ -o mcast_router router.cpp


FROM debian:bookworm-slim

# Install iptables for the firewall rules
RUN apt-get update && apt-get install -y iptables && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# Copy the compiled binary from the builder stage
COPY --from=builder /build/mcast_router .

# Copy your local start.sh script
COPY start.sh .

# Make it executable
RUN chmod +x start.sh

CMD ["./start.sh"]