# Score Analyzer — HPC Project

A full-stack student score management and analytics platform that demonstrates **OpenMP parallel processing** versus serial computation on MongoDB data.

- **Backend** — C (CivetWeb HTTP server, MongoDB C Driver, OpenMP)
- **Frontend** — Next.js 16 / React 19 / TypeScript / Tailwind CSS

---

## Architecture

```
Browser (Next.js :3000)
    │
    ├─ /api/classes        → Next.js proxy → C backend :8090 /api/classes
    ├─ /api/subjects       → Next.js proxy → C backend :8090 /api/subjects
    └─ /analytics          → direct fetch → C backend :8090 /api/…
                                                │
                                           MongoDB Atlas
```

### Directory layout

```
hpc_project/
├── backend/                      # C / CivetWeb server
│   ├── score_analyzer_backend.c  # main – route registration, server loop
│   ├── db.c / include/db.h       # MongoDB CRUD helpers
│   ├── config.c / include/config.h
│   ├── config.env                # ← your real credentials (git-ignored)
│   ├── config.env.example        # template
│   ├── Makefile
│   └── controllers/
│       ├── class_controller.c/h  # GET/POST/DELETE /api/classes & /api/subjects
│       ├── student_controller.c/h
│       ├── score_controller.c/h
│       ├── data_controller.c/h
│       ├── calc_engine.c/h       # OpenMP parallel sort + statistics
│       ├── health_controller.c/h
│       └── response_helper.c/h
│
└── frontend/                     # Next.js app
    ├── app/
    │   ├── page.tsx              # Dashboard (classes + subjects)
    │   ├── analytics/page.tsx    # OpenMP analytics page
    │   ├── login/page.tsx        # Login form
    │   ├── api/
    │   │   ├── auth/login/       # JWT sign & set httpOnly cookie
    │   │   ├── auth/logout/      # Clear cookie
    │   │   ├── classes/          # Proxy → backend /api/classes
    │   │   └── subjects/         # Proxy → backend /api/subjects
    │   └── lib/
    ├── middleware.ts             # JWT auth guard (all pages except /login)
    ├── .env.local                # env vars (git-ignored)
    └── package.json
```

---

## Prerequisites

| Tool | Version tested | Notes |
|------|---------------|-------|
| **WSL 2 + Ubuntu** | 22.04 LTS | Backend builds/runs on Linux |
| **GCC** | ≥ 11 | `sudo apt install build-essential` |
| **libmongoc-1.0** | ≥ 1.23 | see [MongoDB C Driver](#install-mongodb-c-driver) |
| **OpenMP** (`libgomp`) | included with GCC | — |
| **pkg-config** | any | `sudo apt install pkg-config` |
| **Node.js** | 18 – 22 | [nodejs.org](https://nodejs.org) |
| **npm** | ≥ 9 | bundled with Node.js |
| **MongoDB Atlas** | free tier | or local `mongod` |

### Install MongoDB C Driver

```bash
sudo apt update
sudo apt install -y libmongoc-dev libbson-dev pkg-config
# verify
pkg-config --modversion libmongoc-1.0
```

---

## 1 · Backend Setup (WSL / Linux)

### 1.1 Configure credentials

```bash
cd backend
cp config.env.example config.env   # if not already present
```

Edit `config.env`:

```dotenv
MONGODB_URI=mongodb+srv://<user>:<password>@<cluster>.mongodb.net/?appName=score-analyzer
DB_NAME=score_analyzer
PORT=8090
```

> The existing `config.env` already contains the project Atlas credentials — no changes needed for local dev.

### 1.2 Build

```bash
# WSL cannot run ELF binaries from /mnt/c/ — build and run from WSL home instead:
cp -r '/mnt/c/7 Sem/hpc_project/backend' ~/hpc_backend
cd ~/hpc_backend
make clean && make
```

Expected output:

```
gcc -o score_analyzer_backend ...
```

### 1.3 Run

```bash
cd ~/hpc_backend
make run
# or directly:
./score_analyzer_backend
```

The server starts on **http://localhost:8090** and prints a banner of all endpoints.

### 1.4 Stop

```bash
# Ctrl-C in the terminal, OR
make stop          # sends SIGTERM to background instance
# OR visit:
curl http://localhost:8090/exit
```

### 1.5 Makefile targets

| Target | Purpose |
|--------|---------|
| `make` / `make all` | Build binary |
| `make run` | Build + start server (foreground) |
| `make test` | Start server in background |
| `make stop` | Kill background instance |
| `make clean` | Remove build artifacts + logs |
| `make watch` | Auto-rebuild on file change (requires `entr`) |

---

## 2 · Frontend Setup (Windows / any OS)

```powershell
cd frontend
npm install          # first time only
```

### 2.1 Environment variables

`frontend/.env.local` is pre-configured:

```dotenv
NEXT_PUBLIC_API_URL=http://localhost:8090
JWT_SECRET=openmp-score-analyzer-secret-key-2026
ADMIN_USERNAME=admin
ADMIN_PASSWORD=admin123
```

Change `ADMIN_USERNAME` / `ADMIN_PASSWORD` for your own login.

### 2.2 Start dev server

```powershell
cd frontend
npm run dev
```

Open **http://localhost:3000** in your browser.

---

## 3 · Running Everything Together

Open **two terminals** side-by-side:

**Terminal 1 (WSL — backend)**

> ⚠️ **Important:** WSL cannot execute ELF binaries directly from `/mnt/c/` (the Windows drive is mounted `noexec`).  
> You must copy the backend to the WSL home directory first.

```bash
# One-time: copy backend to WSL home and build there
cp -r '/mnt/c/7 Sem/hpc_project/backend' ~/hpc_backend
cd ~/hpc_backend
make clean && make

# Every subsequent run (no need to copy again unless source files change):
cd ~/hpc_backend
make run
```

**Terminal 2 (PowerShell — frontend)**

```powershell
cd "c:\7 Sem\hpc_project\frontend"
npm install    # first time only
npm run dev
```

Then visit **http://localhost:3000**, log in with `admin` / `admin123`, and start using the app.

### After changing backend source files

Since the backend runs from `~/hpc_backend` (a copy), re-sync and rebuild whenever you edit C files on the Windows side:

```bash
# In WSL
cp -r '/mnt/c/7 Sem/hpc_project/backend/.' ~/hpc_backend/
cd ~/hpc_backend && make clean && make run
```

---

## 4 · API Reference

All backend endpoints are served on **http://localhost:8090**.

### Health

| Method | Path | Description |
|--------|------|-------------|
| GET | `/health` | Server + DB health check |

### Classes

| Method | Path | Body / Query | Description |
|--------|------|-------------|-------------|
| GET | `/api/classes` | — | List all classes |
| POST | `/api/classes` | `{ "name": "Class A" }` | Create a class |
| DELETE | `/api/classes/{name}` | — | Delete a class |

### Subjects

| Method | Path | Body / Query | Description |
|--------|------|-------------|-------------|
| GET | `/api/subjects` | `?class=Class+A` (optional) | List subjects (filtered by class) |
| POST | `/api/subjects` | `{ "name": "Math", "class_name": "Class A" }` | Create a subject |
| DELETE | `/api/subjects/{name}?class={class_name}` | — | Delete a subject |

### Students

| Method | Path | Body | Description |
|--------|------|------|-------------|
| GET | `/api/students` | — | List all students |
| POST | `/api/students` | `{ "student_id":"S001","name":"Alice","email":"a@b.com" }` | Create student |
| DELETE | `/api/students/{student_id}` | — | Delete student |

### Analytics (OpenMP)

| Method | Path | Body | Description |
|--------|------|------|-------------|
| POST | `/api/seed` | `{ "num_students": 100, "scores_per_student": 10 }` | Seed random data |
| GET | `/api/calculate/serial` | — | Serial statistics |
| GET | `/api/calculate/parallel` | — | Parallel (OpenMP) statistics |
| GET | `/api/calculate/compare` | — | Side-by-side comparison |

---

## 5 · Frontend Pages

| URL | Description | Auth required |
|-----|-------------|:---:|
| `/login` | Login form | No |
| `/` | Dashboard — manage classes & subjects | ✅ |
| `/analytics` | OpenMP performance analytics + student CRUD | ✅ |

---

## 6 · Authentication

The frontend uses a simple **JWT-based session**:

1. `POST /api/auth/login` — validates credentials from `.env.local`, signs a HS256 JWT, stores it as an `httpOnly` cookie (`auth_token`, 8 h TTL).
2. `middleware.ts` — verifies the cookie on every request except `/login` and `/api/auth/*`.
3. `POST /api/auth/logout` — clears the cookie.

---

## 7 · Troubleshooting

### Backend won't start — "Database connection test failed"

- Check your `MONGODB_URI` in `config.env`.
- Ensure your IP is whitelisted in MongoDB Atlas → **Network Access**.
- Test connectivity: `mongosh "<your-uri>"`.

### pkg-config: libmongoc-1.0 not found

```bash
sudo apt install -y libmongoc-dev libbson-dev pkg-config
```

### Frontend shows "Backend unreachable"

- Confirm the backend is running: `curl http://localhost:8090/health`
- Check `NEXT_PUBLIC_API_URL` in `frontend/.env.local`.
- WSL port-forwarding: if running the frontend on Windows and the backend on WSL, use the WSL 2 host IP instead of `localhost` in `.env.local`:
  ```powershell
  # Find WSL IP
  wsl hostname -I
  # Then set in .env.local:
  NEXT_PUBLIC_API_URL=http://<wsl-ip>:8090
  ```
  > Note: The Next.js **server-side** proxy routes (`/api/classes`, `/api/subjects`) use `NEXT_PUBLIC_API_URL` at server runtime, so this change is enough.

### Port 3000 / 8090 already in use

```powershell
# Windows – find and kill process on a port
netstat -ano | findstr :3000
taskkill /PID <pid> /F
```

```bash
# Linux / WSL
fuser -k 8090/tcp
```

### CORS errors in the browser

All analytics API calls from `analytics/page.tsx` go directly to `http://localhost:8090`. CivetWeb does not send CORS headers by default. If you see CORS errors:

- Use the Next.js proxy routes instead of the direct URL (change `const API` in `analytics/page.tsx`).
- Or add `Access-Control-Allow-Origin: *` headers in the C response helpers.

---

## 8 · Environment Variables Summary

### Backend — `backend/config.env`

| Variable | Default | Description |
|----------|---------|-------------|
| `MONGODB_URI` | *(see file)* | MongoDB Atlas / local connection string |
| `DB_NAME` | `score_analyzer` | Database name |
| `PORT` | `8090` | Listening port |

### Frontend — `frontend/.env.local`

| Variable | Default | Description |
|----------|---------|-------------|
| `NEXT_PUBLIC_API_URL` | `http://localhost:8090` | Backend base URL |
| `JWT_SECRET` | *(see file)* | HMAC secret for JWT signing |
| `ADMIN_USERNAME` | `admin` | Login username |
| `ADMIN_PASSWORD` | `admin123` | Login password |

---

## 9 · Production Notes

- Set `NODE_ENV=production` and run `npm run build && npm start` for the frontend.
- Rotate `JWT_SECRET` and use a strong, random value.
- Whitelist only specific origins in backend CORS headers.
- Never commit `config.env` or `.env.local` with real credentials.

---

*OpenMP Score Analyzer — HPC Project © 2026*
