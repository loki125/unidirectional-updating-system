# Use bash instead of the stricter sh
SHELL := /bin/bash

.PHONY: up down run

up:
	docker compose up -d

down:
	docker compose down

run:
	@$(MAKE) up
	@echo "Launching browser..."
	@python3 -c "import webbrowser; webbrowser.open('http://localhost:8000')"
	@echo ""
	@echo "  SYSTEM STATUS: ONLINE"
	@echo "  Press [ENTER] to shut down all services."
	@read -p ""
	@$(MAKE) down