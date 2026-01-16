-- SQLite Test Database Setup
-- Creates tables, views, triggers, and sample data for testing sql-fuse

-- Enable foreign keys
PRAGMA foreign_keys = ON;

-- Users table
CREATE TABLE users (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    username TEXT NOT NULL UNIQUE,
    email TEXT NOT NULL UNIQUE,
    full_name TEXT,
    created_at TEXT DEFAULT (datetime('now')),
    is_active INTEGER DEFAULT 1
);

-- Categories table
CREATE TABLE categories (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    name TEXT NOT NULL UNIQUE,
    description TEXT,
    parent_id INTEGER REFERENCES categories(id)
);

-- Products table
CREATE TABLE products (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    name TEXT NOT NULL,
    description TEXT,
    price REAL NOT NULL,
    category_id INTEGER REFERENCES categories(id),
    stock_quantity INTEGER DEFAULT 0,
    created_at TEXT DEFAULT (datetime('now')),
    updated_at TEXT
);

-- Orders table
CREATE TABLE orders (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id INTEGER NOT NULL REFERENCES users(id),
    order_date TEXT DEFAULT (datetime('now')),
    status TEXT DEFAULT 'pending',
    total_amount REAL DEFAULT 0
);

-- Order items table
CREATE TABLE order_items (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    order_id INTEGER NOT NULL REFERENCES orders(id),
    product_id INTEGER NOT NULL REFERENCES products(id),
    quantity INTEGER NOT NULL,
    unit_price REAL NOT NULL
);

-- Audit log table
CREATE TABLE audit_log (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    table_name TEXT NOT NULL,
    record_id INTEGER,
    action TEXT NOT NULL,
    old_values TEXT,
    new_values TEXT,
    changed_at TEXT DEFAULT (datetime('now'))
);

-- Settings table
CREATE TABLE settings (
    key TEXT PRIMARY KEY,
    value TEXT,
    description TEXT
);

-- Views
CREATE VIEW v_active_users AS
SELECT id, username, email, full_name, created_at
FROM users
WHERE is_active = 1;

CREATE VIEW v_product_catalog AS
SELECT
    p.id,
    p.name,
    p.description,
    p.price,
    c.name AS category_name,
    p.stock_quantity
FROM products p
LEFT JOIN categories c ON p.category_id = c.id;

CREATE VIEW v_order_summary AS
SELECT
    o.id AS order_id,
    u.username,
    o.order_date,
    o.status,
    o.total_amount,
    COUNT(oi.id) AS item_count
FROM orders o
JOIN users u ON o.user_id = u.id
LEFT JOIN order_items oi ON o.id = oi.order_id
GROUP BY o.id;

CREATE VIEW v_low_stock AS
SELECT id, name, stock_quantity
FROM products
WHERE stock_quantity < 10;

-- Triggers
CREATE TRIGGER tr_products_updated
AFTER UPDATE ON products
BEGIN
    UPDATE products SET updated_at = datetime('now') WHERE id = NEW.id;
END;

CREATE TRIGGER tr_audit_users_insert
AFTER INSERT ON users
BEGIN
    INSERT INTO audit_log (table_name, record_id, action, new_values)
    VALUES ('users', NEW.id, 'INSERT',
            json_object('username', NEW.username, 'email', NEW.email));
END;

CREATE TRIGGER tr_audit_users_update
AFTER UPDATE ON users
BEGIN
    INSERT INTO audit_log (table_name, record_id, action, old_values, new_values)
    VALUES ('users', NEW.id, 'UPDATE',
            json_object('username', OLD.username, 'email', OLD.email),
            json_object('username', NEW.username, 'email', NEW.email));
END;

CREATE TRIGGER tr_audit_users_delete
AFTER DELETE ON users
BEGIN
    INSERT INTO audit_log (table_name, record_id, action, old_values)
    VALUES ('users', OLD.id, 'DELETE',
            json_object('username', OLD.username, 'email', OLD.email));
END;

CREATE TRIGGER tr_order_total
AFTER INSERT ON order_items
BEGIN
    UPDATE orders
    SET total_amount = (
        SELECT COALESCE(SUM(quantity * unit_price), 0)
        FROM order_items
        WHERE order_id = NEW.order_id
    )
    WHERE id = NEW.order_id;
END;

-- Indexes
CREATE INDEX idx_products_category ON products(category_id);
CREATE INDEX idx_orders_user ON orders(user_id);
CREATE INDEX idx_orders_status ON orders(status);
CREATE INDEX idx_order_items_order ON order_items(order_id);
CREATE INDEX idx_order_items_product ON order_items(product_id);

-- Sample Data

-- Users
INSERT INTO users (username, email, full_name) VALUES
    ('alice', 'alice@example.com', 'Alice Johnson'),
    ('bob', 'bob@example.com', 'Bob Smith'),
    ('charlie', 'charlie@example.com', 'Charlie Brown'),
    ('diana', 'diana@example.com', 'Diana Prince'),
    ('eve', 'eve@example.com', 'Eve Wilson');

-- Categories
INSERT INTO categories (name, description) VALUES
    ('Electronics', 'Electronic devices and accessories'),
    ('Books', 'Physical and digital books'),
    ('Clothing', 'Apparel and accessories'),
    ('Home & Garden', 'Home improvement and garden supplies');

INSERT INTO categories (name, description, parent_id) VALUES
    ('Smartphones', 'Mobile phones', 1),
    ('Laptops', 'Portable computers', 1),
    ('Fiction', 'Fiction books', 2),
    ('Non-Fiction', 'Non-fiction books', 2);

-- Products
INSERT INTO products (name, description, price, category_id, stock_quantity) VALUES
    ('iPhone 15', 'Latest Apple smartphone', 999.99, 5, 50),
    ('Samsung Galaxy S24', 'Samsung flagship phone', 899.99, 5, 45),
    ('MacBook Pro 14"', 'Apple laptop with M3 chip', 1999.99, 6, 25),
    ('Dell XPS 15', 'Premium Windows laptop', 1499.99, 6, 30),
    ('The Great Gatsby', 'Classic American novel', 12.99, 7, 100),
    ('1984', 'Dystopian novel by George Orwell', 14.99, 7, 85),
    ('Sapiens', 'A Brief History of Humankind', 18.99, 8, 60),
    ('T-Shirt Basic', 'Cotton t-shirt', 19.99, 3, 200),
    ('Garden Hose 50ft', 'Durable garden hose', 34.99, 4, 5);

-- Orders
INSERT INTO orders (user_id, status) VALUES
    (1, 'completed'),
    (1, 'pending'),
    (2, 'completed'),
    (3, 'shipped'),
    (4, 'pending');

-- Order Items
INSERT INTO order_items (order_id, product_id, quantity, unit_price) VALUES
    (1, 1, 1, 999.99),
    (1, 5, 2, 12.99),
    (2, 3, 1, 1999.99),
    (3, 2, 1, 899.99),
    (3, 6, 1, 14.99),
    (3, 7, 1, 18.99),
    (4, 8, 3, 19.99),
    (5, 9, 2, 34.99);

-- Settings
INSERT INTO settings (key, value, description) VALUES
    ('site_name', 'Test Store', 'Name of the online store'),
    ('currency', 'USD', 'Default currency'),
    ('tax_rate', '0.08', 'Sales tax rate'),
    ('shipping_fee', '5.99', 'Default shipping fee');

-- Verify data
SELECT 'Tables created: ' || COUNT(*) FROM sqlite_master WHERE type='table';
SELECT 'Views created: ' || COUNT(*) FROM sqlite_master WHERE type='view';
SELECT 'Triggers created: ' || COUNT(*) FROM sqlite_master WHERE type='trigger';
SELECT 'Indexes created: ' || COUNT(*) FROM sqlite_master WHERE type='index';
