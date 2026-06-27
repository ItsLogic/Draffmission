# Seed Viewer

A modern web UI for visualizing Minecraft mushroom island seeds found by the Commission seed finder.

## Features

- **Infinite scroll grid** of all seeds with thumbnail previews
- **Zoomable biome maps** — click any seed to see a full biome map with pan/zoom
- **Search, sort, and filter** by seed, size, coordinates, mode (SB/LB/USB/ULB), and bounds
- **Biome-accurate rendering** using cubiomes at scale 4 (1 pixel = 4 blocks)
- **Docker-based** — one command to spin up the full stack
- **SQLite backend** — no external database needed

## Quick Start

```bash
# From the seedviewer/ directory:
docker compose up --build
```

This starts:
- **Frontend**: http://localhost:3000 (modern React UI)
- **Backend API**: http://localhost:8000/docs (FastAPI)

## Importing Seeds

### Via CLI script

```bash
# Import output.txt in SB mode
./import.sh ../output.txt sb

# Import output1m.txt in USB mode with auto-render
./import.sh ../output1m.txt usb --render
```

### Via API

```bash
# Import from a file path accessible to the backend container
curl -X POST "http://localhost:8000/api/import/path?path=/data/output.txt&mode=sb"

# Upload a file directly
curl -X POST "http://localhost:8000/api/import?mode=usb" -F "file=@output.txt"
```

### Import output.txt into the Docker container

The backend container only sees files in the `./data/` volume. To import:

```bash
# Copy output.txt into the data volume
cp ../output.txt data/output.txt

# Import via API (path is relative to backend container)
curl -X POST "http://localhost:8000/api/import/path?path=/data/output.txt&mode=sb"
```

## Data Format

The expected file format (one seed per line):
```
<seed> <x> <z> <size>
```

Example:
```
56582305831 -26559824 18257664 5241395
```

## Architecture

```
seedviewer/
├── docker-compose.yml      # Orchestrates backend + frontend
├── Dockerfile.backend      # Multi-stage: builds renderer + Python backend
├── Dockerfile.frontend     # Multi-stage: builds React + nginx
├── renderer/
│   ├── render_map.cpp      # C++ biome map renderer (links cubiomes)
│   └── makefile
├── backend/
│   ├── main.py             # FastAPI app with all endpoints
│   ├── db.py               # SQLite database layer
│   ├── render.py           # Renderer process management
│   └── requirements.txt
├── frontend/
│   ├── src/
│   │   ├── App.tsx         # Main app with routing
│   │   ├── api.ts          # API client
│   │   ├── types.ts        # TypeScript types
│   │   └── components/
│   │       ├── SeedGrid.tsx       # Infinite scroll grid
│   │       ├── SeedCard.tsx       # Individual seed card
│   │       ├── SeedDetail.tsx     # Detail page with map
│   │       ├── BiomeMapViewer.tsx # Zoomable map component
│   │       ├── FilterBar.tsx      # Search/filter/sort controls
│   │       └── StatsBar.tsx       # Header statistics
│   ├── package.json
│   └── nginx.conf
├── data/                   # Docker volume (DB + images)
│   ├── seeds.db
│   ├── thumbnails/
│   └── maps/
└── import.sh               # Convenience import script
```

## Map Rendering

Thumbnails (256x256) and full maps (1024x1024) are rendered at scale 4 (1 pixel = 4 blocks) using the cubiomes library. The renderer is a standalone C++ binary compiled against the cubiomes source in `../cubiomes/`.

Each map covers a 4096x4096 block area centered on the island center (1024 cells at scale 4, with zoom=4 downsampling from 4096x4096 effective).

Maps are rendered on-demand when first requested and cached as PNG files in `data/maps/` and `data/thumbnails/`.

## API Endpoints

| Method | Path | Description |
|--------|------|-------------|
| GET | `/api/seeds` | List seeds (paginated, filterable) |
| GET | `/api/seeds/:id` | Get single seed details |
| GET | `/api/stats` | Database statistics |
| POST | `/api/import` | Import from uploaded file |
| POST | `/api/import/path` | Import from file path |
| POST | `/api/render/:id` | Trigger map rendering |
| GET | `/api/images/thumb/:id` | Get thumbnail image |
| GET | `/api/images/map/:id` | Get full map image |
| DELETE | `/api/seeds/:id` | Delete a seed |

## Filter Parameters

- **sort**: `size`, `seed`, `distance`, `created_at`
- **order**: `asc`, `desc`
- **mode**: `sb`, `lb`, `usb`, `ulb`
- **in_bounds**: within 2M blocks of origin (true/false)
- **min_size / max_size**: block count range
