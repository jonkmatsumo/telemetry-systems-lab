# Makefile for Telemetry Generator

COMPOSE_INFRA = docker-compose.infra.yml
COMPOSE_APP = docker-compose.app.yml
COMPOSE_BASE = docker-compose.yml

.PHONY: infra-up infra-down dev-up dev-shell run-bin clean

# Start Infrastructure (Postgres)
infra-up:
	docker compose -f $(COMPOSE_INFRA) up -d

infra-down:
	docker compose -f $(COMPOSE_INFRA) down

# Start Dev Environment (Infra + App Container)
dev-up: infra-up
	docker compose -f $(COMPOSE_INFRA) -f $(COMPOSE_APP) up -d --build

# Shell into Dev Container
dev-shell:
	docker compose -f $(COMPOSE_INFRA) -f $(COMPOSE_APP) exec telemetry-generator bash

# OS Detection
NPROC := $(shell sysctl -n hw.logicalcpu 2>/dev/null || nproc)

# Helper to build inside container
build:
	docker compose -f $(COMPOSE_INFRA) -f $(COMPOSE_APP) exec telemetry-generator cmake -B build -S .
	docker compose -f $(COMPOSE_INFRA) -f $(COMPOSE_APP) exec telemetry-generator make -C build -j$(NPROC)


# Helper to run binary inside container
run:
	docker compose -f $(COMPOSE_INFRA) -f $(COMPOSE_APP) exec telemetry-generator ./build/telemetry-generator

run-api:
	docker compose -f $(COMPOSE_INFRA) -f $(COMPOSE_APP) exec telemetry-generator ./build/telemetry-api

# Clean everything
clean:
	docker compose -f $(COMPOSE_INFRA) -f $(COMPOSE_APP) down -v
	docker compose -f $(COMPOSE_BASE) down -v
	docker volume rm telemetry-systems-lab_build_cache || true
