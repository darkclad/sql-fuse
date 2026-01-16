-- PostgreSQL FUSE Test Database Setup
-- Creates a test database with tables, views, procedures, functions, and triggers
-- Run as: psql -U postgres -f setup_test_db.sql

-- ============================================================================
-- Database Setup
-- ============================================================================

-- Drop and create database (run as superuser)
DROP DATABASE IF EXISTS fuse_test;
CREATE DATABASE fuse_test WITH ENCODING 'UTF8';

-- Connect to the new database
\c fuse_test

-- ============================================================================
-- Custom Types
-- ============================================================================

CREATE TYPE user_role AS ENUM ('admin', 'user', 'guest');
CREATE TYPE order_status AS ENUM ('pending', 'processing', 'shipped', 'delivered', 'cancelled');
CREATE TYPE audit_action AS ENUM ('INSERT', 'UPDATE', 'DELETE');
CREATE TYPE setting_type AS ENUM ('string', 'integer', 'boolean', 'json');

-- ============================================================================
-- Tables
-- ============================================================================

-- Users table
CREATE TABLE users (
    id SERIAL PRIMARY KEY,
    username VARCHAR(50) NOT NULL UNIQUE,
    email VARCHAR(100) NOT NULL UNIQUE,
    password_hash VARCHAR(255) NOT NULL,
    first_name VARCHAR(50),
    last_name VARCHAR(50),
    role user_role DEFAULT 'user',
    is_active BOOLEAN DEFAULT TRUE,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

CREATE INDEX idx_users_email ON users(email);
CREATE INDEX idx_users_role ON users(role);

-- Categories table
CREATE TABLE categories (
    id SERIAL PRIMARY KEY,
    name VARCHAR(100) NOT NULL,
    slug VARCHAR(100) NOT NULL UNIQUE,
    description TEXT,
    parent_id INT REFERENCES categories(id) ON DELETE SET NULL,
    sort_order INT DEFAULT 0,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

-- Products table
CREATE TABLE products (
    id SERIAL PRIMARY KEY,
    sku VARCHAR(50) NOT NULL UNIQUE,
    name VARCHAR(200) NOT NULL,
    description TEXT,
    price DECIMAL(10, 2) NOT NULL,
    cost DECIMAL(10, 2),
    quantity INT DEFAULT 0,
    category_id INT REFERENCES categories(id) ON DELETE SET NULL,
    is_available BOOLEAN DEFAULT TRUE,
    weight DECIMAL(8, 2),
    dimensions JSONB,
    tags JSONB,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

CREATE INDEX idx_products_sku ON products(sku);
CREATE INDEX idx_products_category ON products(category_id);
CREATE INDEX idx_products_price ON products(price);
CREATE INDEX idx_products_name_desc ON products USING GIN (to_tsvector('english', name || ' ' || COALESCE(description, '')));

-- Orders table
CREATE TABLE orders (
    id SERIAL PRIMARY KEY,
    order_number VARCHAR(20) NOT NULL UNIQUE,
    user_id INT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    status order_status DEFAULT 'pending',
    subtotal DECIMAL(10, 2) NOT NULL,
    tax DECIMAL(10, 2) DEFAULT 0,
    shipping DECIMAL(10, 2) DEFAULT 0,
    total DECIMAL(10, 2) NOT NULL,
    shipping_address JSONB,
    billing_address JSONB,
    notes TEXT,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

CREATE INDEX idx_orders_user ON orders(user_id);
CREATE INDEX idx_orders_status ON orders(status);
CREATE INDEX idx_orders_created ON orders(created_at);

-- Order items table
CREATE TABLE order_items (
    id SERIAL PRIMARY KEY,
    order_id INT NOT NULL REFERENCES orders(id) ON DELETE CASCADE,
    product_id INT NOT NULL REFERENCES products(id) ON DELETE CASCADE,
    quantity INT NOT NULL,
    unit_price DECIMAL(10, 2) NOT NULL,
    total_price DECIMAL(10, 2) NOT NULL
);

CREATE INDEX idx_order_items_order ON order_items(order_id);
CREATE INDEX idx_order_items_product ON order_items(product_id);

-- Audit log table
CREATE TABLE audit_log (
    id BIGSERIAL PRIMARY KEY,
    table_name VARCHAR(64) NOT NULL,
    record_id INT NOT NULL,
    action audit_action NOT NULL,
    old_values JSONB,
    new_values JSONB,
    user_id INT,
    ip_address VARCHAR(45),
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

CREATE INDEX idx_audit_table_record ON audit_log(table_name, record_id);
CREATE INDEX idx_audit_created ON audit_log(created_at);

-- Settings table (key-value store)
CREATE TABLE settings (
    id SERIAL PRIMARY KEY,
    setting_key VARCHAR(100) NOT NULL UNIQUE,
    setting_value TEXT,
    setting_type setting_type DEFAULT 'string',
    description VARCHAR(255),
    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

-- ============================================================================
-- Auto-update timestamp trigger function
-- ============================================================================

CREATE OR REPLACE FUNCTION update_updated_at_column()
RETURNS TRIGGER AS $$
BEGIN
    NEW.updated_at = CURRENT_TIMESTAMP;
    RETURN NEW;
END;
$$ LANGUAGE plpgsql;

-- Apply to tables with updated_at
CREATE TRIGGER trg_users_updated_at
    BEFORE UPDATE ON users
    FOR EACH ROW EXECUTE FUNCTION update_updated_at_column();

CREATE TRIGGER trg_products_updated_at
    BEFORE UPDATE ON products
    FOR EACH ROW EXECUTE FUNCTION update_updated_at_column();

CREATE TRIGGER trg_orders_updated_at
    BEFORE UPDATE ON orders
    FOR EACH ROW EXECUTE FUNCTION update_updated_at_column();

CREATE TRIGGER trg_settings_updated_at
    BEFORE UPDATE ON settings
    FOR EACH ROW EXECUTE FUNCTION update_updated_at_column();

-- ============================================================================
-- Views
-- ============================================================================

-- Active users view
CREATE VIEW v_active_users AS
SELECT id, username, email, first_name, last_name, role, created_at
FROM users
WHERE is_active = TRUE;

-- Product catalog view
CREATE VIEW v_product_catalog AS
SELECT
    p.id,
    p.sku,
    p.name,
    p.description,
    p.price,
    p.quantity,
    p.is_available,
    c.name AS category_name,
    c.slug AS category_slug
FROM products p
LEFT JOIN categories c ON p.category_id = c.id
WHERE p.is_available = TRUE;

-- Order summary view
CREATE VIEW v_order_summary AS
SELECT
    o.id,
    o.order_number,
    o.status,
    o.total,
    o.created_at,
    u.username,
    u.email,
    COUNT(oi.id) AS item_count,
    SUM(oi.quantity) AS total_items
FROM orders o
JOIN users u ON o.user_id = u.id
LEFT JOIN order_items oi ON o.id = oi.order_id
GROUP BY o.id, o.order_number, o.status, o.total, o.created_at, u.username, u.email;

-- Revenue by category view
CREATE VIEW v_revenue_by_category AS
SELECT
    c.id AS category_id,
    c.name AS category_name,
    COUNT(DISTINCT p.id) AS product_count,
    COUNT(DISTINCT oi.order_id) AS order_count,
    COALESCE(SUM(oi.total_price), 0) AS total_revenue
FROM categories c
LEFT JOIN products p ON c.id = p.category_id
LEFT JOIN order_items oi ON p.id = oi.product_id
GROUP BY c.id, c.name;

-- Low stock products view
CREATE VIEW v_low_stock AS
SELECT id, sku, name, quantity, category_id
FROM products
WHERE quantity < 10 AND is_available = TRUE;

-- ============================================================================
-- Stored Functions
-- ============================================================================

-- Calculate order total function
CREATE OR REPLACE FUNCTION fn_calculate_order_total(p_order_id INT)
RETURNS DECIMAL(10, 2) AS $$
DECLARE
    v_total DECIMAL(10, 2);
BEGIN
    SELECT COALESCE(SUM(total_price), 0) INTO v_total
    FROM order_items
    WHERE order_id = p_order_id;

    RETURN v_total;
END;
$$ LANGUAGE plpgsql;

-- Get user full name function
CREATE OR REPLACE FUNCTION fn_get_user_fullname(p_user_id INT)
RETURNS VARCHAR(101) AS $$
DECLARE
    v_fullname VARCHAR(101);
BEGIN
    SELECT CONCAT_WS(' ', first_name, last_name) INTO v_fullname
    FROM users
    WHERE id = p_user_id;

    RETURN COALESCE(v_fullname, 'Unknown');
END;
$$ LANGUAGE plpgsql;

-- Check product availability function
CREATE OR REPLACE FUNCTION fn_check_availability(p_product_id INT, p_quantity INT)
RETURNS BOOLEAN AS $$
DECLARE
    v_available INT;
BEGIN
    SELECT quantity INTO v_available
    FROM products
    WHERE id = p_product_id AND is_available = TRUE;

    RETURN v_available IS NOT NULL AND v_available >= p_quantity;
END;
$$ LANGUAGE plpgsql;

-- Generate SKU function
CREATE OR REPLACE FUNCTION fn_generate_sku(p_category_id INT, p_product_name VARCHAR(200))
RETURNS VARCHAR(50) AS $$
DECLARE
    v_prefix VARCHAR(10);
    v_suffix VARCHAR(10);
BEGIN
    SELECT UPPER(LEFT(slug, 3)) INTO v_prefix
    FROM categories
    WHERE id = p_category_id;

    v_prefix := COALESCE(v_prefix, 'GEN');
    v_suffix := UPPER(LEFT(REPLACE(p_product_name, ' ', ''), 5));

    RETURN CONCAT(v_prefix, '-', v_suffix, '-', LPAD(FLOOR(RANDOM() * 10000)::TEXT, 4, '0'));
END;
$$ LANGUAGE plpgsql;

-- ============================================================================
-- Stored Procedures (PostgreSQL 11+)
-- ============================================================================

-- Update order total procedure
CREATE OR REPLACE PROCEDURE sp_update_order_total(p_order_id INT)
LANGUAGE plpgsql AS $$
DECLARE
    v_subtotal DECIMAL(10, 2);
    v_tax DECIMAL(10, 2);
    v_shipping DECIMAL(10, 2);
BEGIN
    SELECT COALESCE(SUM(total_price), 0) INTO v_subtotal
    FROM order_items
    WHERE order_id = p_order_id;

    SELECT tax, shipping INTO v_tax, v_shipping
    FROM orders
    WHERE id = p_order_id;

    UPDATE orders
    SET subtotal = v_subtotal,
        total = v_subtotal + COALESCE(v_tax, 0) + COALESCE(v_shipping, 0)
    WHERE id = p_order_id;
END;
$$;

-- Get category tree procedure
CREATE OR REPLACE FUNCTION sp_get_category_tree(p_parent_id INT DEFAULT NULL)
RETURNS TABLE(id INT, name VARCHAR, slug VARCHAR, parent_id INT, level INT) AS $$
BEGIN
    RETURN QUERY
    WITH RECURSIVE category_tree AS (
        SELECT c.id, c.name, c.slug, c.parent_id, 0 AS level
        FROM categories c
        WHERE (p_parent_id IS NULL AND c.parent_id IS NULL) OR c.parent_id = p_parent_id

        UNION ALL

        SELECT c.id, c.name, c.slug, c.parent_id, ct.level + 1
        FROM categories c
        JOIN category_tree ct ON c.parent_id = ct.id
    )
    SELECT ct.id, ct.name, ct.slug, ct.parent_id, ct.level
    FROM category_tree ct
    ORDER BY ct.level, ct.name;
END;
$$ LANGUAGE plpgsql;

-- ============================================================================
-- Triggers
-- ============================================================================

-- Audit trigger function for users table
CREATE OR REPLACE FUNCTION fn_audit_users()
RETURNS TRIGGER AS $$
BEGIN
    IF TG_OP = 'INSERT' THEN
        INSERT INTO audit_log (table_name, record_id, action, new_values)
        VALUES ('users', NEW.id, 'INSERT', jsonb_build_object(
            'username', NEW.username,
            'email', NEW.email,
            'role', NEW.role::text
        ));
        RETURN NEW;
    ELSIF TG_OP = 'UPDATE' THEN
        INSERT INTO audit_log (table_name, record_id, action, old_values, new_values)
        VALUES ('users', NEW.id, 'UPDATE',
            jsonb_build_object('username', OLD.username, 'email', OLD.email, 'role', OLD.role::text),
            jsonb_build_object('username', NEW.username, 'email', NEW.email, 'role', NEW.role::text)
        );
        RETURN NEW;
    ELSIF TG_OP = 'DELETE' THEN
        INSERT INTO audit_log (table_name, record_id, action, old_values)
        VALUES ('users', OLD.id, 'DELETE', jsonb_build_object(
            'username', OLD.username,
            'email', OLD.email,
            'role', OLD.role::text
        ));
        RETURN OLD;
    END IF;
    RETURN NULL;
END;
$$ LANGUAGE plpgsql;

CREATE TRIGGER trg_users_audit
    AFTER INSERT OR UPDATE OR DELETE ON users
    FOR EACH ROW EXECUTE FUNCTION fn_audit_users();

-- Order items trigger to update order total
CREATE OR REPLACE FUNCTION fn_order_items_update_total()
RETURNS TRIGGER AS $$
BEGIN
    IF TG_OP = 'DELETE' THEN
        CALL sp_update_order_total(OLD.order_id);
        RETURN OLD;
    ELSE
        CALL sp_update_order_total(NEW.order_id);
        RETURN NEW;
    END IF;
END;
$$ LANGUAGE plpgsql;

CREATE TRIGGER trg_order_items_update_total
    AFTER INSERT OR UPDATE OR DELETE ON order_items
    FOR EACH ROW EXECUTE FUNCTION fn_order_items_update_total();

-- ============================================================================
-- Sample Data
-- ============================================================================

-- Insert users
INSERT INTO users (username, email, password_hash, first_name, last_name, role) VALUES
('admin', 'admin@example.com', '$2b$12$LQv3c1yqBWVHxkd0LHAkCOYz6TtxMQJqhN8/X4.S9f0t6VJaXuNMi', 'Admin', 'User', 'admin'),
('john_doe', 'john@example.com', '$2b$12$LQv3c1yqBWVHxkd0LHAkCOYz6TtxMQJqhN8/X4.S9f0t6VJaXuNMi', 'John', 'Doe', 'user'),
('jane_smith', 'jane@example.com', '$2b$12$LQv3c1yqBWVHxkd0LHAkCOYz6TtxMQJqhN8/X4.S9f0t6VJaXuNMi', 'Jane', 'Smith', 'user'),
('bob_wilson', 'bob@example.com', '$2b$12$LQv3c1yqBWVHxkd0LHAkCOYz6TtxMQJqhN8/X4.S9f0t6VJaXuNMi', 'Bob', 'Wilson', 'user'),
('guest', 'guest@example.com', '$2b$12$LQv3c1yqBWVHxkd0LHAkCOYz6TtxMQJqhN8/X4.S9f0t6VJaXuNMi', 'Guest', 'User', 'guest');

-- Insert categories
INSERT INTO categories (name, slug, description, parent_id, sort_order) VALUES
('Electronics', 'electronics', 'Electronic devices and accessories', NULL, 1),
('Computers', 'computers', 'Desktop and laptop computers', 1, 1),
('Smartphones', 'smartphones', 'Mobile phones and accessories', 1, 2),
('Clothing', 'clothing', 'Apparel and fashion items', NULL, 2),
('Men', 'men', 'Men''s clothing', 4, 1),
('Women', 'women', 'Women''s clothing', 4, 2),
('Home & Garden', 'home-garden', 'Home improvement and garden supplies', NULL, 3),
('Books', 'books', 'Books and publications', NULL, 4);

-- Insert products
INSERT INTO products (sku, name, description, price, cost, quantity, category_id, weight, dimensions, tags) VALUES
('COMP-LAPTOP-001', 'Pro Laptop 15"', 'High-performance laptop with 15" display, 16GB RAM, 512GB SSD', 1299.99, 950.00, 25, 2, 2.1, '{"length": 35, "width": 24, "height": 2}', '["laptop", "computer", "portable"]'),
('COMP-LAPTOP-002', 'Budget Laptop 14"', 'Affordable laptop for everyday use, 8GB RAM, 256GB SSD', 599.99, 420.00, 50, 2, 1.8, '{"length": 32, "width": 22, "height": 2}', '["laptop", "budget", "student"]'),
('PHONE-SMART-001', 'SmartPhone Pro', 'Latest smartphone with 6.5" OLED display, 128GB storage', 899.99, 650.00, 100, 3, 0.2, '{"length": 15, "width": 7, "height": 0.8}', '["phone", "smartphone", "5g"]'),
('PHONE-SMART-002', 'SmartPhone Lite', 'Mid-range smartphone with great camera, 64GB storage', 499.99, 350.00, 75, 3, 0.18, '{"length": 14, "width": 7, "height": 0.8}', '["phone", "smartphone", "camera"]'),
('CLOTH-MENS-001', 'Classic T-Shirt', 'Comfortable cotton t-shirt, available in multiple colors', 24.99, 8.00, 200, 5, 0.2, NULL, '["tshirt", "cotton", "casual"]'),
('CLOTH-MENS-002', 'Denim Jeans', 'Classic fit denim jeans, durable and stylish', 59.99, 25.00, 150, 5, 0.5, NULL, '["jeans", "denim", "casual"]'),
('CLOTH-WMNS-001', 'Summer Dress', 'Light and elegant summer dress, floral pattern', 79.99, 30.00, 80, 6, 0.3, NULL, '["dress", "summer", "floral"]'),
('HOME-FURN-001', 'Desk Lamp', 'Adjustable LED desk lamp with multiple brightness levels', 39.99, 15.00, 60, 7, 1.2, '{"length": 40, "width": 15, "height": 45}', '["lamp", "led", "office"]'),
('BOOK-TECH-001', 'Learn Programming', 'Comprehensive guide to modern programming languages', 49.99, 20.00, 30, 8, 0.8, '{"length": 24, "width": 17, "height": 3}', '["programming", "education", "tech"]'),
('BOOK-TECH-002', 'Database Design', 'Master database design and SQL queries', 44.99, 18.00, 25, 8, 0.7, '{"length": 24, "width": 17, "height": 2.5}', '["database", "sql", "tech"]');

-- Disable triggers temporarily for sample data
ALTER TABLE order_items DISABLE TRIGGER trg_order_items_update_total;

-- Insert orders
INSERT INTO orders (order_number, user_id, status, subtotal, tax, shipping, total, shipping_address, billing_address) VALUES
('ORD-20250101-0001', 2, 'delivered', 1324.98, 132.50, 15.00, 1472.48,
    '{"street": "123 Main St", "city": "New York", "state": "NY", "zip": "10001", "country": "USA"}',
    '{"street": "123 Main St", "city": "New York", "state": "NY", "zip": "10001", "country": "USA"}'),
('ORD-20250102-0002', 3, 'shipped', 899.99, 90.00, 0.00, 989.99,
    '{"street": "456 Oak Ave", "city": "Los Angeles", "state": "CA", "zip": "90001", "country": "USA"}',
    '{"street": "456 Oak Ave", "city": "Los Angeles", "state": "CA", "zip": "90001", "country": "USA"}'),
('ORD-20250103-0003', 2, 'processing', 84.98, 8.50, 5.00, 98.48,
    '{"street": "123 Main St", "city": "New York", "state": "NY", "zip": "10001", "country": "USA"}',
    '{"street": "123 Main St", "city": "New York", "state": "NY", "zip": "10001", "country": "USA"}'),
('ORD-20250104-0004', 4, 'pending', 139.98, 14.00, 10.00, 163.98,
    '{"street": "789 Pine Rd", "city": "Chicago", "state": "IL", "zip": "60601", "country": "USA"}',
    '{"street": "789 Pine Rd", "city": "Chicago", "state": "IL", "zip": "60601", "country": "USA"}');

-- Insert order items
INSERT INTO order_items (order_id, product_id, quantity, unit_price, total_price) VALUES
(1, 1, 1, 1299.99, 1299.99),
(1, 5, 1, 24.99, 24.99),
(2, 3, 1, 899.99, 899.99),
(3, 5, 2, 24.99, 49.98),
(3, 8, 1, 39.99, 39.99),
(4, 6, 1, 59.99, 59.99),
(4, 7, 1, 79.99, 79.99);

-- Re-enable triggers
ALTER TABLE order_items ENABLE TRIGGER trg_order_items_update_total;

-- Insert settings
INSERT INTO settings (setting_key, setting_value, setting_type, description) VALUES
('site_name', 'Test Store', 'string', 'Website name'),
('items_per_page', '20', 'integer', 'Default pagination size'),
('enable_reviews', 'true', 'boolean', 'Allow product reviews'),
('shipping_options', '{"standard": 5.99, "express": 15.99, "overnight": 29.99}', 'json', 'Available shipping options'),
('tax_rate', '0.10', 'string', 'Default tax rate (10%)'),
('maintenance_mode', 'false', 'boolean', 'Enable maintenance mode');

-- ============================================================================
-- Test User for sql-fuse
-- ============================================================================

-- Create a dedicated user for sql-fuse testing (run as superuser)
-- CREATE USER fuse_test WITH PASSWORD 'fuse_test_password';
-- GRANT ALL PRIVILEGES ON DATABASE fuse_test TO fuse_test;
-- GRANT ALL PRIVILEGES ON ALL TABLES IN SCHEMA public TO fuse_test;
-- GRANT ALL PRIVILEGES ON ALL SEQUENCES IN SCHEMA public TO fuse_test;
-- GRANT EXECUTE ON ALL FUNCTIONS IN SCHEMA public TO fuse_test;

-- ============================================================================
-- Verification Queries
-- ============================================================================

SELECT '=== Database Objects Summary ===' AS info;

SELECT 'Tables' AS object_type, COUNT(*) AS count
FROM information_schema.tables
WHERE table_schema = 'public' AND table_type = 'BASE TABLE'
UNION ALL
SELECT 'Views', COUNT(*)
FROM information_schema.views
WHERE table_schema = 'public'
UNION ALL
SELECT 'Functions', COUNT(*)
FROM information_schema.routines
WHERE routine_schema = 'public' AND routine_type = 'FUNCTION'
UNION ALL
SELECT 'Procedures', COUNT(*)
FROM information_schema.routines
WHERE routine_schema = 'public' AND routine_type = 'PROCEDURE'
UNION ALL
SELECT 'Triggers', COUNT(*)
FROM information_schema.triggers
WHERE trigger_schema = 'public';

SELECT '=== Setup Complete ===' AS info;
