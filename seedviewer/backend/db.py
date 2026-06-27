import sqlite3
import os
from pathlib import Path
from contextlib import contextmanager

DB_PATH = os.environ.get("DB_PATH", "/data/seeds.db")

def init_db():
    Path(DB_PATH).parent.mkdir(parents=True, exist_ok=True)
    with get_conn() as conn:
        conn.executescript("""
            CREATE TABLE IF NOT EXISTS seeds (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                seed INTEGER NOT NULL,
                x INTEGER NOT NULL,
                z INTEGER NOT NULL,
                size INTEGER NOT NULL,
                mode TEXT NOT NULL DEFAULT 'sb',
                has_thumb INTEGER NOT NULL DEFAULT 0,
                created_at TEXT NOT NULL DEFAULT (datetime('now')),
                UNIQUE(seed, x, z)
            );
            CREATE INDEX IF NOT EXISTS idx_seeds_size ON seeds(size DESC);
            CREATE INDEX IF NOT EXISTS idx_seeds_mode ON seeds(mode);
            CREATE INDEX IF NOT EXISTS idx_seeds_seed ON seeds(seed);
        """)

@contextmanager
def get_conn():
    conn = sqlite3.connect(DB_PATH, timeout=30)
    conn.row_factory = sqlite3.Row
    conn.execute("PRAGMA journal_mode=WAL")
    conn.execute("PRAGMA synchronous=NORMAL")
    try:
        yield conn
        conn.commit()
    finally:
        conn.close()
