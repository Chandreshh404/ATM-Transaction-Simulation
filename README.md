# Nexus Bank - ATM Transaction Simulation

Educational ATM simulation (no real money). C++17 backend (cpp-httplib + SQLite + nlohmann/json), vanilla HTML/CSS/JS frontend.

Features: registration, PIN login with lockout, balance, deposit, withdrawal, transfer, transaction history, logout.

## Run locally
```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/backend/atm_backend        # serves on http://127.0.0.1:8080 (or $PORT)
```
Open `frontend/index.html` in a browser. Tests: `ctest --test-dir build`.

## Environment variables (all optional)
| Variable | Default | Purpose |
|---|---|---|
| `PORT` | 8080 | Port to listen on (Render sets this) |
| `DATABASE_PATH` | `database/atm.db` | SQLite file location |
| `SCHEMA_PATH` | `database/schema.sql` | Schema file |
| `SEED_DEMO_USER` | 0 | `1` creates `demo@nexusbank.test` / PIN `123456` with INR 10,000 |

See `RENDER_DEPLOYMENT_NOTES.md` for deployment.
