from typing import Optional
import math
import threading

from fastapi import FastAPI, Query, HTTPException, BackgroundTasks
from fastapi.responses import FileResponse
from fastapi.middleware.cors import CORSMiddleware
from fastapi.staticfiles import StaticFiles
from pathlib import Path
import os

from db import init_db, get_conn
from render import render_thumb, THUMB_DIR

app = FastAPI(title="MC Seed Viewer", version="1.0.0")

app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)

WORLD_BORDER = 29_999_984
# Island centers within this many blocks of (0,0) are treated as "at origin".
# Matches the GPU --origin filter (4096 quarter-blocks = 16384 blocks).
ORIGIN_RADIUS = 16384

render_lock = threading.Lock()

@app.on_event("startup")
def startup():
    init_db()
    threading.Thread(target=_batch_render_pending, daemon=True).start()

def _batch_render_pending():
    import time
    while True:
        with get_conn() as conn:
            rows = conn.execute("SELECT id, seed, x, z, mode FROM seeds WHERE has_thumb = 0 LIMIT 50").fetchall()
        if not rows:
            break
        for row in rows:
            with render_lock:
                ok = render_thumb(row["seed"], row["x"], row["z"], row["mode"])
            if ok:
                with get_conn() as conn:
                    conn.execute("UPDATE seeds SET has_thumb = 1 WHERE id = ?", [row["id"]])
        time.sleep(0.1)

@app.get("/api/stats")
def stats():
    with get_conn() as conn:
        row = conn.execute("SELECT COUNT(*) as count, COALESCE(AVG(size),0) as avg_size, COALESCE(MAX(size),0) as max_size, COALESCE(MIN(size),0) as min_size FROM seeds").fetchone()
        modes = conn.execute("SELECT mode, COUNT(*) as cnt FROM seeds GROUP BY mode").fetchall()
        rendered = conn.execute("SELECT SUM(has_thumb) as thumbs FROM seeds").fetchone()
    return {
        "total": row["count"],
        "avg_size": int(row["avg_size"]),
        "max_size": row["max_size"],
        "min_size": row["min_size"],
        "modes": {m["mode"]: m["cnt"] for m in modes},
        "thumbnails": rendered["thumbs"] or 0,
    }

@app.get("/api/seeds")
def list_seeds(
    page: int = Query(1, ge=1),
    limit: int = Query(50, ge=1, le=200),
    sort: str = Query("size", pattern="size|seed|distance|created_at"),
    order: str = Query("desc", pattern="asc|desc"),
    search: str = Query("", description="Search by seed number"),
    min_size: Optional[int] = Query(None),
    max_size: Optional[int] = None,
    mode: Optional[str] = Query(None, pattern="sb|lb|usb|ulb"),
    biome: Optional[str] = Query(None, pattern="small|large"),
    in_bounds: Optional[bool] = None,
    at_origin: Optional[bool] = None,
):
    where_parts = []
    params = []

    if search.strip():
        where_parts.append("CAST(seed AS TEXT) LIKE ?")
        params.append(f"%{search.strip()}%")

    if min_size is not None:
        where_parts.append("size >= ?")
        params.append(min_size)
    if max_size is not None:
        where_parts.append("size <= ?")
        params.append(max_size)
    if mode:
        where_parts.append("mode = ?")
        params.append(mode)
    if biome:
        if biome == "small":
            where_parts.append("(mode = 'sb' OR mode = 'usb')")
        else:
            where_parts.append("(mode = 'lb' OR mode = 'ulb')")
    if in_bounds is not None:
        if in_bounds:
            where_parts.append("mode NOT LIKE 'u%'")
        else:
            where_parts.append("mode LIKE 'u%'")
    if at_origin is not None:
        cond = f"(abs(x) <= {ORIGIN_RADIUS} AND abs(z) <= {ORIGIN_RADIUS})"
        where_parts.append(cond if at_origin else f"NOT {cond}")

    where_clause = (" WHERE " + " AND ".join(where_parts)) if where_parts else ""

    sort_col = {
        "size": "size",
        "seed": "seed",
        "distance": "(abs(x)+abs(z))",
        "created_at": "created_at",
    }[sort]

    offset = (page - 1) * limit

    with get_conn() as conn:
        total = conn.execute(f"SELECT COUNT(*) FROM seeds{where_clause}", params).fetchone()[0]
        rows = conn.execute(
            f"SELECT * FROM seeds{where_clause} ORDER BY {sort_col} {order.upper()} LIMIT ? OFFSET ?",
            params + [limit, offset]
        ).fetchall()

    seed_list = []
    for r in rows:
        d = dict(r)
        d["in_bounds"] = not d["mode"].startswith("u")
        d["at_origin"] = abs(d["x"]) <= ORIGIN_RADIUS and abs(d["z"]) <= ORIGIN_RADIUS
        d["seed"] = str(d["seed"])
        seed_list.append(d)

    return {
        "total": total,
        "page": page,
        "limit": limit,
        "pages": math.ceil(total / limit) if total > 0 else 0,
        "seeds": seed_list,
    }

@app.get("/api/seeds/{seed_id}")
def get_seed(seed_id: int):
    with get_conn() as conn:
        row = conn.execute("SELECT * FROM seeds WHERE id = ?", [seed_id]).fetchone()
        if not row:
            raise HTTPException(404, "Seed not found")
        d = dict(row)
        d["in_bounds"] = not d["mode"].startswith("u")
        d["seed"] = str(d["seed"])
        return d

@app.post("/api/seeds/single")
def add_single_seed(
    background_tasks: BackgroundTasks,
    seed: int = Query(...),
    x: int = Query(...),
    z: int = Query(...),
    size: int = Query(...),
    mode: str = Query("sb", pattern="sb|lb"),
):
    if seed >= 2**63:
        seed = seed - 2**64
    line_mode = mode
    if abs(x) > WORLD_BORDER or abs(z) > WORLD_BORDER:
        line_mode = "u" + line_mode
    try:
        with get_conn() as conn:
            conn.execute(
                "INSERT INTO seeds (seed, x, z, size, mode) VALUES (?, ?, ?, ?, ?)",
                [seed, x, z, size, line_mode]
            )
        background_tasks.add_task(_render_one, seed, x, z, line_mode)
        return {"status": "ok", "mode": line_mode}
    except Exception:
        return {"status": "duplicate"}

def _render_one(seed: int, x: int, z: int, mode: str):
    with render_lock:
        ok = render_thumb(seed, x, z, mode)
    if ok:
        with get_conn() as conn:
            conn.execute("UPDATE seeds SET has_thumb = 1 WHERE seed = ? AND x = ? AND z = ?", [seed, x, z])

@app.delete("/api/seeds/{seed_id}")
def delete_seed(seed_id: int):
    with get_conn() as conn:
        row = conn.execute("SELECT * FROM seeds WHERE id = ?", [seed_id]).fetchone()
        if not row:
            raise HTTPException(404, "Seed not found")
        for f in THUMB_DIR.glob(f"{row['seed']}_{row['x']}_{row['z']}_thumb.png"):
            f.unlink(missing_ok=True)
        conn.execute("DELETE FROM seeds WHERE id = ?", [seed_id])
    return {"status": "deleted"}

@app.get("/api/images/thumb/{seed_id}")
def get_thumb(seed_id: int):
    with get_conn() as conn:
        row = conn.execute("SELECT * FROM seeds WHERE id = ?", [seed_id]).fetchone()
        if not row:
            raise HTTPException(404, "Seed not found")
    thumb_name = f"{row['seed']}_{row['x']}_{row['z']}_thumb.png"
    thumb_path = THUMB_DIR / thumb_name
    if not thumb_path.exists():
        with render_lock:
            ok = render_thumb(row["seed"], row["x"], row["z"], row["mode"])
        if not ok:
            raise HTTPException(404, "Thumbnail not available")
        with get_conn() as conn:
            conn.execute("UPDATE seeds SET has_thumb = 1 WHERE id = ?", [seed_id])
    return FileResponse(str(thumb_path), media_type="image/png")

frontend_dist = Path(os.environ.get("FRONTEND_DIST", "/app/frontend/dist"))
if frontend_dist.exists():
    app.mount("/", StaticFiles(directory=str(frontend_dist), html=True), name="frontend")
