# Makefile for Telemetry Generator

COMPOSE_INFRA = docker-compose.infra.yml
COMPOSE_APP = docker-compose.app.yml
COMPOSE_BASE = docker-compose.yml

.PHONY: up infra-up infra-down down dev-up dev-shell api-shell build clean run run-api

# Unified startup (Infra + Dev App Containers)
up: dev-up

# Stop all containers
down:
	docker compose -f $(COMPOSE_INFRA) -f $(COMPOSE_APP) down

# Start Infrastructure (Postgres)
infra-up:
	docker compose -f $(COMPOSE_INFRA) up -d

infra-down:
	docker compose -f $(COMPOSE_INFRA) down

# Start Dev Environment (Infra + App Containers)
dev-up:
	docker compose -f $(COMPOSE_INFRA) -f $(COMPOSE_APP) up -d --build

# Shell access
dev-shell:
	docker compose -f $(COMPOSE_INFRA) -f $(COMPOSE_APP) exec telemetry-generator bash

api-shell:
	docker compose -f $(COMPOSE_INFRA) -f $(COMPOSE_APP) exec telemetry-api bash

# OS Detection
NPROC := $(shell sysctl -n hw.logicalcpu 2>/dev/null || nproc)

# Helper to build all C++ targets inside container
build:
	docker compose -f $(COMPOSE_INFRA) -f $(COMPOSE_APP) exec telemetry-generator cmake -B build -S .
	docker compose -f $(COMPOSE_INFRA) -f $(COMPOSE_APP) exec telemetry-generator make -C build -j$(NPROC)

# Helper to run binaries inside container
run:
	docker compose -f $(COMPOSE_INFRA) -f $(COMPOSE_APP) exec telemetry-generator ./build/telemetry-generator

run-api:
	docker compose -f $(COMPOSE_INFRA) -f $(COMPOSE_APP) exec telemetry-api ./build/telemetry-api

# Clean everything
clean:
	docker compose -f $(COMPOSE_INFRA) -f $(COMPOSE_APP) down -v
	docker compose -f $(COMPOSE_BASE) down -v
	docker volume rm telemetry-systems-lab_build_cache || true
