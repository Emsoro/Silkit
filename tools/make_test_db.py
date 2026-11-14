#!/usr/bin/env python3
"""生成 data/silkit_test.db 测试库（SQLite）。

库内容覆盖 M6 设计器需要的元数据：自增主键、唯一/普通索引、外键、
默认值、BLOB、视图与触发器。重复运行会重建该文件。
"""

from pathlib import Path
import sqlite3

ROOT = Path(__file__).resolve().parent.parent
DB_PATH = ROOT / "data" / "silkit_test.db"

SCHEMA = """
PRAGMA foreign_keys = ON;

CREATE TABLE users (
    id         INTEGER PRIMARY KEY AUTOINCREMENT,
    name       TEXT    NOT NULL,
    email      TEXT,
    status     TEXT    NOT NULL DEFAULT 'active',
    score      REAL             DEFAULT 0.0,
    created_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    avatar     BLOB,
    note       TEXT
);

CREATE UNIQUE INDEX idx_users_email ON users(email);
CREATE INDEX idx_users_status ON users(status);

CREATE TABLE orders (
    id         INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id    INTEGER NOT NULL,
    total      DECIMAL(10,2) NOT NULL DEFAULT 0,
    status     TEXT    NOT NULL DEFAULT 'pending',
    created_at DATE    NOT NULL,
    CONSTRAINT fk_orders_user FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE
);
CREATE INDEX idx_orders_user ON orders(user_id);

CREATE TABLE products (
    id    INTEGER PRIMARY KEY AUTOINCREMENT,
    name  TEXT NOT NULL,
    price DECIMAL(10,2) NOT NULL,
    stock INTEGER NOT NULL DEFAULT 0,
    state TEXT
);
CREATE INDEX idx_products_name ON products(name);

CREATE VIEW active_users AS
SELECT id, name, email, status FROM users WHERE status = 'active';

CREATE TRIGGER trg_users_touch AFTER UPDATE ON users
BEGIN
    SELECT 1;
END;

PRAGMA user_version = 1;
"""

USERS = [
    ("林知夏", "lin@example.com", "active", 88.5, "2026-01-05 09:12:00", None, "种子数据"),
    ("Alice Chen", "alice@example.com", "active", 92.0, "2026-01-06 10:20:00", None, None),
    ("Mika", None, "pending", 75.5, "2026-01-07 11:30:00", None, None),
    ("苏晚", "su@example.com", "active", 81.0, "2026-01-08 12:40:00", None, None),
    ("Nora", "nora@example.com", "review", 66.5, "2026-01-09 13:50:00", None, None),
    ("陈见月", "chen@example.com", "active", 95.0, "2026-01-10 14:00:00", None, None),
    ("周以宁", None, "active", 70.0, "2026-01-11 15:10:00", None, None),
    ("李澈", "li@example.com", "pending", 58.5, "2026-01-12 16:20:00", None, None),
    ("王砚", "wang@example.com", "active", 89.0, "2026-01-13 17:30:00", None, None),
    ("赵星野", "zhao@example.com", "review", 77.5, "2026-01-14 18:40:00", None, None),
    ("沈岚", "shen@example.com", "active", 84.0, "2026-01-15 19:50:00", None, None),
    ("顾清和", "gu@example.com", "active", 91.5, "2026-01-16 20:00:00", b"\x89PNG\r\n\x1a\n", None),
]

ORDERS = [
    (1, "129.00", "paid", "2026-10-01"),
    (1, "89.50", "paid", "2026-10-03"),
    (2, "299.00", "pending", "2026-10-05"),
    (3, "45.00", "paid", "2026-10-07"),
    (4, "199.00", "refunded", "2026-10-09"),
    (6, "520.00", "paid", "2026-10-11"),
    (9, "76.80", "pending", "2026-10-13"),
    (12, "999.00", "paid", "2026-10-15"),
]

PRODUCTS = [
    ("机械键盘", "399.00", 42, "在售"),
    ("显示器", "1299.00", 18, "在售"),
    ("人体工学椅", "899.00", 7, "在售"),
    ("Type-C 扩展坞", "159.00", 65, "在售"),
    ("降噪耳机", "699.00", 0, "缺货"),
    ("桌面灯", "129.00", 30, "在售"),
]


def main() -> None:
    DB_PATH.parent.mkdir(parents=True, exist_ok=True)
    if DB_PATH.exists():
        DB_PATH.unlink()

    connection = sqlite3.connect(DB_PATH)
    try:
        connection.executescript(SCHEMA)
        connection.executemany(
            "INSERT INTO users (name, email, status, score, created_at, avatar, note)"
            " VALUES (?, ?, ?, ?, ?, ?, ?)",
            USERS,
        )
        connection.executemany(
            "INSERT INTO orders (user_id, total, status, created_at) VALUES (?, ?, ?, ?)",
            ORDERS,
        )
        connection.executemany(
            "INSERT INTO products (name, price, stock, state) VALUES (?, ?, ?, ?)",
            PRODUCTS,
        )
        connection.commit()
    finally:
        connection.close()

    print(f"created {DB_PATH} ({DB_PATH.stat().st_size} bytes)")


if __name__ == "__main__":
    main()
