#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
INIT_SQL="${ROOT_DIR}/db/init.sql"
MIGRATIONS_DIR="${ROOT_DIR}/db/migrations"

if [[ ! -f "${INIT_SQL}" ]]; then
  echo "init.sql not found at ${INIT_SQL}" >&2
  exit 1
fi

if [[ ! -d "${MIGRATIONS_DIR}" ]]; then
  echo "migrations directory not found at ${MIGRATIONS_DIR}" >&2
  exit 1
fi

: "${PGHOST:=localhost}"
: "${PGPORT:=5432}"
: "${PGUSER:=postgres}"
: "${PGPASSWORD:=password}"
: "${PGDATABASE:=postgres}"
: "${PGOPTIONS:=--client-min-messages=warning}"

export PGPASSWORD
export PGOPTIONS

stamp="$(date +%s)_$RANDOM"
init_db="telemetry_schema_init_${stamp}"
migrated_db="telemetry_schema_migrated_${stamp}"

cleanup() {
  psql -v ON_ERROR_STOP=1 -h "${PGHOST}" -p "${PGPORT}" -U "${PGUSER}" -d "${PGDATABASE}" \
    -c "DROP DATABASE IF EXISTS ${init_db};" >/dev/null 2>&1 || true
  psql -v ON_ERROR_STOP=1 -h "${PGHOST}" -p "${PGPORT}" -U "${PGUSER}" -d "${PGDATABASE}" \
    -c "DROP DATABASE IF EXISTS ${migrated_db};" >/dev/null 2>&1 || true
}
trap cleanup EXIT

psql -v ON_ERROR_STOP=1 -h "${PGHOST}" -p "${PGPORT}" -U "${PGUSER}" -d "${PGDATABASE}" \
  -c "CREATE DATABASE ${init_db};" >/dev/null
psql -v ON_ERROR_STOP=1 -h "${PGHOST}" -p "${PGPORT}" -U "${PGUSER}" -d "${PGDATABASE}" \
  -c "CREATE DATABASE ${migrated_db};" >/dev/null

psql -v ON_ERROR_STOP=1 -h "${PGHOST}" -p "${PGPORT}" -U "${PGUSER}" -d "${init_db}" -f "${INIT_SQL}" >/dev/null

# Apply canonical snapshot first, then replay migrations to catch drift introduced by new migration columns/types.
psql -v ON_ERROR_STOP=1 -h "${PGHOST}" -p "${PGPORT}" -U "${PGUSER}" -d "${migrated_db}" -f "${INIT_SQL}" >/dev/null
while IFS= read -r migration; do
  psql -v ON_ERROR_STOP=1 -h "${PGHOST}" -p "${PGPORT}" -U "${PGUSER}" -d "${migrated_db}" -f "${migration}" >/dev/null
done < <(find "${MIGRATIONS_DIR}" -maxdepth 1 -type f -name '*.sql' | sort)

schema_query=$(cat <<'SQL'
SELECT table_name, column_name, data_type, udt_name
FROM information_schema.columns
WHERE table_schema = 'public'
ORDER BY table_name, ordinal_position;
SQL
)

init_schema_file="$(mktemp)"
migrated_schema_file="$(mktemp)"

psql -v ON_ERROR_STOP=1 -h "${PGHOST}" -p "${PGPORT}" -U "${PGUSER}" -d "${init_db}" \
  -At -F $'\t' -c "${schema_query}" > "${init_schema_file}"
psql -v ON_ERROR_STOP=1 -h "${PGHOST}" -p "${PGPORT}" -U "${PGUSER}" -d "${migrated_db}" \
  -At -F $'\t' -c "${schema_query}" > "${migrated_schema_file}"

if ! diff -u "${init_schema_file}" "${migrated_schema_file}"; then
  echo "Schema drift detected between db/init.sql and db/migrations (tables/columns/types)." >&2
  rm -f "${init_schema_file}" "${migrated_schema_file}"
  exit 1
fi

rm -f "${init_schema_file}" "${migrated_schema_file}"
echo "Schema drift check passed."
