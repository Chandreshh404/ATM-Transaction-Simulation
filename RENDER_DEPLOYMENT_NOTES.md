# Render deployment notes (free demonstration)

## Important data limitation

The free Render web service has an ephemeral filesystem. The SQLite database at `database/atm.db` can be lost when the service spins down, restarts, or redeploys. Use synthetic demo data only. This is not production banking infrastructure.

## Backend service

- Create a Render **Web Service** connected to this GitHub repository and the `main` branch.
- Choose **Docker** as the runtime and the **Free** instance type.
- Leave the Dockerfile path as `./Dockerfile` and the Docker build context at the repository root.
- Do not set a hardcoded port. The C++ server reads Render's `PORT` environment variable and binds to `0.0.0.0`.
- Set the health check path to `/api/health` if the dashboard offers that setting.

## Frontend static site

- Create a Render **Static Site** from the same repository and `main` branch.
- Set the publish directory to `frontend`.
- No build command is required.
- After the backend deploys, copy its public `https://...onrender.com` URL.
- Edit `frontend/js/config.js` and set `window.ATM_API_BASE_URL` to that backend URL (no trailing slash).
- Commit and push the config change. Render should redeploy the static site automatically.

## Verification

1. Open `https://BACKEND-URL.onrender.com/api/health` and confirm an HTTP success response.
2. Open `https://BACKEND-URL.onrender.com/api/health/db` and confirm database health.
3. Open the frontend static-site URL and test registration, login, balance, deposit, withdrawal, transfer, transaction history, and logout with synthetic accounts.
4. Wait for the free backend to spin down, then make a request again. Expect a cold start and remember the local SQLite database may have reset.

## Troubleshooting

- **Browser says "Cannot connect" / first login is slow:** free services sleep after ~15 minutes. The first request can take up to a minute; the page now pings the backend on load and retries automatically.
- **"Invalid email or PIN" after a while:** the free filesystem was reset, so accounts were wiped. The Dockerfile sets `SEED_DEMO_USER=1`, which recreates `demo@nexusbank.test` / PIN `123456` on every start. For real persistence use a Render persistent disk and set `DATABASE_PATH=/var/data/atm.db` (paid plan).
- **Frontend can't reach backend:** `frontend/js/config.js` must contain the exact backend URL from your Render dashboard.
- **Build runs out of memory:** the Dockerfile compiles with `-j1` to keep peak memory low.
- Optional: `render.yaml` in the repo root lets you create both services at once via New > Blueprint.
