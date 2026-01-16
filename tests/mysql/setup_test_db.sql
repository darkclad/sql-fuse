-- MySQL FUSE Test Database Setup
-- Creates a test database with tables, views, procedures, functions, triggers, and events
-- Run as: mysql -u root -p < setup_test_db.sql

-- ============================================================================
-- Database Setup
-- ============================================================================

DROP DATABASE IF EXISTS fuse_test;
CREATE DATABASE fuse_test CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;
USE fuse_test;

-- ============================================================================
-- Tables
-- ============================================================================

-- Users table
CREATE TABLE users (
    id INT AUTO_INCREMENT PRIMARY KEY,
    username VARCHAR(50) NOT NULL UNIQUE,
    email VARCHAR(100) NOT NULL UNIQUE,
    password_hash VARCHAR(255) NOT NULL,
    first_name VARCHAR(50),
    last_name VARCHAR(50),
    role ENUM('admin', 'user', 'guest') DEFAULT 'user',
    is_active BOOLEAN DEFAULT TRUE,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    INDEX idx_email (email),
    INDEX idx_role (role)
) ENGINE=InnoDB;

-- Categories table
CREATE TABLE categories (
    id INT AUTO_INCREMENT PRIMARY KEY,
    name VARCHAR(100) NOT NULL,
    slug VARCHAR(100) NOT NULL UNIQUE,
    description TEXT,
    parent_id INT,
    sort_order INT DEFAULT 0,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY (parent_id) REFERENCES categories(id) ON DELETE SET NULL
) ENGINE=InnoDB;

-- Products table
CREATE TABLE products (
    id INT AUTO_INCREMENT PRIMARY KEY,
    sku VARCHAR(50) NOT NULL UNIQUE,
    name VARCHAR(200) NOT NULL,
    description TEXT,
    price DECIMAL(10, 2) NOT NULL,
    cost DECIMAL(10, 2),
    quantity INT DEFAULT 0,
    category_id INT,
    is_available BOOLEAN DEFAULT TRUE,
    weight DECIMAL(8, 2),
    dimensions JSON,
    tags JSON,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    FOREIGN KEY (category_id) REFERENCES categories(id) ON DELETE SET NULL,
    INDEX idx_sku (sku),
    INDEX idx_category (category_id),
    INDEX idx_price (price),
    FULLTEXT INDEX ft_name_desc (name, description)
) ENGINE=InnoDB;

-- Orders table
CREATE TABLE orders (
    id INT AUTO_INCREMENT PRIMARY KEY,
    order_number VARCHAR(20) NOT NULL UNIQUE,
    user_id INT NOT NULL,
    status ENUM('pending', 'processing', 'shipped', 'delivered', 'cancelled') DEFAULT 'pending',
    subtotal DECIMAL(10, 2) NOT NULL,
    tax DECIMAL(10, 2) DEFAULT 0,
    shipping DECIMAL(10, 2) DEFAULT 0,
    total DECIMAL(10, 2) NOT NULL,
    shipping_address JSON,
    billing_address JSON,
    notes TEXT,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE,
    INDEX idx_user (user_id),
    INDEX idx_status (status),
    INDEX idx_created (created_at)
) ENGINE=InnoDB;

-- Order items table
CREATE TABLE order_items (
    id INT AUTO_INCREMENT PRIMARY KEY,
    order_id INT NOT NULL,
    product_id INT NOT NULL,
    quantity INT NOT NULL,
    unit_price DECIMAL(10, 2) NOT NULL,
    total_price DECIMAL(10, 2) NOT NULL,
    FOREIGN KEY (order_id) REFERENCES orders(id) ON DELETE CASCADE,
    FOREIGN KEY (product_id) REFERENCES products(id) ON DELETE CASCADE,
    INDEX idx_order (order_id),
    INDEX idx_product (product_id)
) ENGINE=InnoDB;

-- Audit log table
CREATE TABLE audit_log (
    id BIGINT AUTO_INCREMENT PRIMARY KEY,
    table_name VARCHAR(64) NOT NULL,
    record_id INT NOT NULL,
    action ENUM('INSERT', 'UPDATE', 'DELETE') NOT NULL,
    old_values JSON,
    new_values JSON,
    user_id INT,
    ip_address VARCHAR(45),
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    INDEX idx_table_record (table_name, record_id),
    INDEX idx_created (created_at)
) ENGINE=InnoDB;

-- Settings table (key-value store)
CREATE TABLE settings (
    id INT AUTO_INCREMENT PRIMARY KEY,
    setting_key VARCHAR(100) NOT NULL UNIQUE,
    setting_value TEXT,
    setting_type ENUM('string', 'integer', 'boolean', 'json') DEFAULT 'string',
    description VARCHAR(255),
    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP
) ENGINE=InnoDB;

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
-- Stored Procedures
-- ============================================================================

DELIMITER //

-- Get user orders procedure
CREATE PROCEDURE sp_get_user_orders(IN p_user_id INT)
BEGIN
    SELECT
        o.id,
        o.order_number,
        o.status,
        o.total,
        o.created_at,
        COUNT(oi.id) AS item_count
    FROM orders o
    LEFT JOIN order_items oi ON o.id = oi.order_id
    WHERE o.user_id = p_user_id
    GROUP BY o.id
    ORDER BY o.created_at DESC;
END //

-- Create order procedure
CREATE PROCEDURE sp_create_order(
    IN p_user_id INT,
    IN p_shipping_address JSON,
    IN p_billing_address JSON,
    OUT p_order_id INT,
    OUT p_order_number VARCHAR(20)
)
BEGIN
    DECLARE v_order_number VARCHAR(20);

    -- Generate order number
    SET v_order_number = CONCAT('ORD-', DATE_FORMAT(NOW(), '%Y%m%d'), '-', LPAD(FLOOR(RAND() * 10000), 4, '0'));

    INSERT INTO orders (order_number, user_id, subtotal, total, shipping_address, billing_address)
    VALUES (v_order_number, p_user_id, 0, 0, p_shipping_address, p_billing_address);

    SET p_order_id = LAST_INSERT_ID();
    SET p_order_number = v_order_number;
END //

-- Update order total procedure
CREATE PROCEDURE sp_update_order_total(IN p_order_id INT)
BEGIN
    DECLARE v_subtotal DECIMAL(10, 2);
    DECLARE v_tax DECIMAL(10, 2);
    DECLARE v_shipping DECIMAL(10, 2);

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
END //

-- Search products procedure
CREATE PROCEDURE sp_search_products(
    IN p_search_term VARCHAR(100),
    IN p_category_id INT,
    IN p_min_price DECIMAL(10, 2),
    IN p_max_price DECIMAL(10, 2),
    IN p_limit INT,
    IN p_offset INT
)
BEGIN
    SELECT
        p.id,
        p.sku,
        p.name,
        p.description,
        p.price,
        p.quantity,
        c.name AS category_name
    FROM products p
    LEFT JOIN categories c ON p.category_id = c.id
    WHERE p.is_available = TRUE
        AND (p_search_term IS NULL OR MATCH(p.name, p.description) AGAINST(p_search_term))
        AND (p_category_id IS NULL OR p.category_id = p_category_id)
        AND (p_min_price IS NULL OR p.price >= p_min_price)
        AND (p_max_price IS NULL OR p.price <= p_max_price)
    ORDER BY p.name
    LIMIT p_limit OFFSET p_offset;
END //

-- Get category tree procedure (recursive)
CREATE PROCEDURE sp_get_category_tree(IN p_parent_id INT)
BEGIN
    WITH RECURSIVE category_tree AS (
        SELECT id, name, slug, parent_id, 0 AS level
        FROM categories
        WHERE (p_parent_id IS NULL AND parent_id IS NULL) OR parent_id = p_parent_id

        UNION ALL

        SELECT c.id, c.name, c.slug, c.parent_id, ct.level + 1
        FROM categories c
        JOIN category_tree ct ON c.parent_id = ct.id
    )
    SELECT * FROM category_tree ORDER BY level, name;
END //

DELIMITER ;

-- ============================================================================
-- Stored Functions
-- ============================================================================

DELIMITER //

-- Calculate order total function
CREATE FUNCTION fn_calculate_order_total(p_order_id INT)
RETURNS DECIMAL(10, 2)
DETERMINISTIC
READS SQL DATA
BEGIN
    DECLARE v_total DECIMAL(10, 2);

    SELECT COALESCE(SUM(total_price), 0) INTO v_total
    FROM order_items
    WHERE order_id = p_order_id;

    RETURN v_total;
END //

-- Get user full name function
CREATE FUNCTION fn_get_user_fullname(p_user_id INT)
RETURNS VARCHAR(101)
DETERMINISTIC
READS SQL DATA
BEGIN
    DECLARE v_fullname VARCHAR(101);

    SELECT CONCAT_WS(' ', first_name, last_name) INTO v_fullname
    FROM users
    WHERE id = p_user_id;

    RETURN COALESCE(v_fullname, 'Unknown');
END //

-- Check product availability function
CREATE FUNCTION fn_check_availability(p_product_id INT, p_quantity INT)
RETURNS BOOLEAN
DETERMINISTIC
READS SQL DATA
BEGIN
    DECLARE v_available INT;

    SELECT quantity INTO v_available
    FROM products
    WHERE id = p_product_id AND is_available = TRUE;

    RETURN v_available IS NOT NULL AND v_available >= p_quantity;
END //

-- Generate SKU function
CREATE FUNCTION fn_generate_sku(p_category_id INT, p_product_name VARCHAR(200))
RETURNS VARCHAR(50)
DETERMINISTIC
BEGIN
    DECLARE v_prefix VARCHAR(10);
    DECLARE v_suffix VARCHAR(10);

    SELECT UPPER(LEFT(slug, 3)) INTO v_prefix
    FROM categories
    WHERE id = p_category_id;

    SET v_prefix = COALESCE(v_prefix, 'GEN');
    SET v_suffix = UPPER(LEFT(REPLACE(p_product_name, ' ', ''), 5));

    RETURN CONCAT(v_prefix, '-', v_suffix, '-', LPAD(FLOOR(RAND() * 10000), 4, '0'));
END //

DELIMITER ;

-- ============================================================================
-- Triggers
-- ============================================================================

DELIMITER //

-- Audit trigger for users table
CREATE TRIGGER trg_users_after_insert
AFTER INSERT ON users
FOR EACH ROW
BEGIN
    INSERT INTO audit_log (table_name, record_id, action, new_values)
    VALUES ('users', NEW.id, 'INSERT', JSON_OBJECT(
        'username', NEW.username,
        'email', NEW.email,
        'role', NEW.role
    ));
END //

CREATE TRIGGER trg_users_after_update
AFTER UPDATE ON users
FOR EACH ROW
BEGIN
    INSERT INTO audit_log (table_name, record_id, action, old_values, new_values)
    VALUES ('users', NEW.id, 'UPDATE',
        JSON_OBJECT('username', OLD.username, 'email', OLD.email, 'role', OLD.role),
        JSON_OBJECT('username', NEW.username, 'email', NEW.email, 'role', NEW.role)
    );
END //

CREATE TRIGGER trg_users_after_delete
AFTER DELETE ON users
FOR EACH ROW
BEGIN
    INSERT INTO audit_log (table_name, record_id, action, old_values)
    VALUES ('users', OLD.id, 'DELETE', JSON_OBJECT(
        'username', OLD.username,
        'email', OLD.email,
        'role', OLD.role
    ));
END //

-- Update order total after order item changes
CREATE TRIGGER trg_order_items_after_insert
AFTER INSERT ON order_items
FOR EACH ROW
BEGIN
    CALL sp_update_order_total(NEW.order_id);
END //

CREATE TRIGGER trg_order_items_after_update
AFTER UPDATE ON order_items
FOR EACH ROW
BEGIN
    CALL sp_update_order_total(NEW.order_id);
END //

CREATE TRIGGER trg_order_items_after_delete
AFTER DELETE ON order_items
FOR EACH ROW
BEGIN
    CALL sp_update_order_total(OLD.order_id);
END //

-- Update product quantity on order
CREATE TRIGGER trg_order_items_reduce_stock
AFTER INSERT ON order_items
FOR EACH ROW
BEGIN
    UPDATE products
    SET quantity = quantity - NEW.quantity
    WHERE id = NEW.product_id;
END //

DELIMITER ;

-- ============================================================================
-- Events (requires event_scheduler = ON)
-- ============================================================================

DELIMITER //

-- Cleanup old audit logs (runs daily)
CREATE EVENT IF NOT EXISTS evt_cleanup_audit_log
ON SCHEDULE EVERY 1 DAY
STARTS CURRENT_TIMESTAMP
DO
BEGIN
    DELETE FROM audit_log
    WHERE created_at < DATE_SUB(NOW(), INTERVAL 90 DAY);
END //

-- Update product availability (runs hourly)
CREATE EVENT IF NOT EXISTS evt_update_availability
ON SCHEDULE EVERY 1 HOUR
STARTS CURRENT_TIMESTAMP
DO
BEGIN
    UPDATE products
    SET is_available = FALSE
    WHERE quantity <= 0 AND is_available = TRUE;
END //

DELIMITER ;

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
('Men', 'men', 'Men\'s clothing', 4, 1),
('Women', 'women', 'Women\'s clothing', 4, 2),
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

-- Insert order items (without triggering stock reduction for sample data)
-- Temporarily disable triggers
SET @DISABLE_TRIGGERS = TRUE;

INSERT INTO order_items (order_id, product_id, quantity, unit_price, total_price) VALUES
(1, 1, 1, 1299.99, 1299.99),
(1, 5, 1, 24.99, 24.99),
(2, 3, 1, 899.99, 899.99),
(3, 5, 2, 24.99, 49.98),
(3, 8, 1, 39.99, 39.99),
(4, 6, 1, 59.99, 59.99),
(4, 7, 1, 79.99, 79.99);

SET @DISABLE_TRIGGERS = FALSE;

-- Insert settings
INSERT INTO settings (setting_key, setting_value, setting_type, description) VALUES
('site_name', 'Test Store', 'string', 'Website name'),
('items_per_page', '20', 'integer', 'Default pagination size'),
('enable_reviews', 'true', 'boolean', 'Allow product reviews'),
('shipping_options', '{"standard": 5.99, "express": 15.99, "overnight": 29.99}', 'json', 'Available shipping options'),
('tax_rate', '0.10', 'string', 'Default tax rate (10%)'),
('maintenance_mode', 'false', 'boolean', 'Enable maintenance mode');

-- ============================================================================
-- Test User for mysql-fuse
-- ============================================================================

-- Create a dedicated user for mysql-fuse testing
-- (Run these as root/admin user)
-- CREATE USER IF NOT EXISTS 'fuse_test'@'localhost' IDENTIFIED BY 'fuse_test_password';
-- GRANT ALL PRIVILEGES ON fuse_test.* TO 'fuse_test'@'localhost';
-- GRANT SELECT ON mysql.proc TO 'fuse_test'@'localhost';
-- FLUSH PRIVILEGES;

-- ============================================================================
-- Verification Queries
-- ============================================================================

SELECT '=== Database Objects Summary ===' AS '';
SELECT 'Tables:' AS 'Object Type', COUNT(*) AS 'Count' FROM information_schema.tables WHERE table_schema = 'fuse_test' AND table_type = 'BASE TABLE'
UNION ALL
SELECT 'Views:', COUNT(*) FROM information_schema.views WHERE table_schema = 'fuse_test'
UNION ALL
SELECT 'Procedures:', COUNT(*) FROM information_schema.routines WHERE routine_schema = 'fuse_test' AND routine_type = 'PROCEDURE'
UNION ALL
SELECT 'Functions:', COUNT(*) FROM information_schema.routines WHERE routine_schema = 'fuse_test' AND routine_type = 'FUNCTION'
UNION ALL
SELECT 'Triggers:', COUNT(*) FROM information_schema.triggers WHERE trigger_schema = 'fuse_test'
UNION ALL
SELECT 'Events:', COUNT(*) FROM information_schema.events WHERE event_schema = 'fuse_test';

SELECT '=== Setup Complete ===' AS '';
