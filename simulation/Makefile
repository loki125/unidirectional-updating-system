CMD ?= up
OPTS ?= -d
NET_MODE ?= --dynamic
VM_PATH ?= ./default-vm-path

.PHONY: all networks isolated-net outside-net vm clean
all: networks isolated-net outside-net vm

networks:
	./compose/scripts/net.sh $(NET_MODE)
	docker compose -f compose/networks.yml $(CMD) $(if $(filter up,$(CMD)),$(OPTS),)

isolated-net:
	docker compose -f compose/isolated-net.yml $(CMD) $(if $(filter up,$(CMD)),$(OPTS),)

outside-net:
	docker compose -f compose/outside-net.yml $(CMD) $(if $(filter up,$(CMD)),$(OPTS),)

vm:
	./compose/scripts/static-vm.sh $(VM_PATH)

# Helper for a full cleanup
down:
	$(MAKE) CMD=down networks
	$(MAKE) CMD=down isolated-net
	$(MAKE) CMD=down outside-net