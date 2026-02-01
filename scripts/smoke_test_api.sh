#!/usr/bin/env bash
#
# Smoke test for critical API routes
# Verifies that /train, /train/{id}, and /models endpoints are registered
#
set -euo pipefail

API_BASE="${API_BASE:-http://localhost:8280}"
FAILED=0

check_route() {
    local method=$1
    local path=$2
    local expected_not=$3
    local description=$4

    local status
    status=$(curl -s -o /dev/null -w "%{http_code}" -X "$method" "${API_BASE}${path}" \
        -H "Content-Type: application/json" \
        -d '{}' 2>/dev/null || echo "000")

    if [ "$status" = "$expected_not" ]; then
        echo "FAIL: $description - got $status (expected NOT $expected_not)"
        FAILED=1
    else
        echo "PASS: $description - got $status"
    fi
}

echo "=== API Route Smoke Test ==="
echo "Target: $API_BASE"
echo ""

# These routes should NOT return 404
check_route "GET" "/models" "404" "GET /models is registered"
check_route "POST" "/train" "404" "POST /train is registered"
check_route "GET" "/train/00000000-0000-0000-0000-000000000000" "404" "GET /train/{id} is registered"

# Also verify core routes still work
check_route "GET" "/datasets" "404" "GET /datasets is registered"
check_route "GET" "/schema/metrics" "404" "GET /schema/metrics is registered"
check_route "GET" "/healthz" "404" "GET /healthz is registered"

echo ""
if [ $FAILED -eq 0 ]; then
    echo "All smoke tests passed!"
    exit 0
else
    echo "Some smoke tests failed!"
    exit 1
fi
