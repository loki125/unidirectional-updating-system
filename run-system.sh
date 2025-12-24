#!/usr/bin/env bash
set -e

# starting NAa on network netA
NA_NAME=NA_A NA_NET=netA NA_SUBNET=172.20.0.0/16 NA_GATEWAY=172.20.0.1 NA_IP=172.20.0.2 \
docker compose -f network-agent/docker-compose.yml up -d

# starting NAb on network netB
NA_NAME=NA_B NA_NET=netB NA_SUBNET=172.21.0.0/16 NA_GATEWAY=172.21.0.1 NA_IP=172.21.0.2 \
docker compose -f network-agent/docker-compose.yml up -d
