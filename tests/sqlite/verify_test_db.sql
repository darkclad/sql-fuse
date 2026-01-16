-- SQLite Test Database Verification
-- Run this to verify the test database was created correctly

.headers on
.mode column

SELECT '=== Database Schema ===' AS '';

SELECT type, name FROM sqlite_master
WHERE type IN ('table', 'view', 'trigger', 'index')
ORDER BY type, name;

SELECT '' AS '';
SELECT '=== Table Row Counts ===' AS '';

SELECT 'users' AS table_name, COUNT(*) AS row_count FROM users
UNION ALL SELECT 'categories', COUNT(*) FROM categories
UNION ALL SELECT 'products', COUNT(*) FROM products
UNION ALL SELECT 'orders', COUNT(*) FROM orders
UNION ALL SELECT 'order_items', COUNT(*) FROM order_items
UNION ALL SELECT 'audit_log', COUNT(*) FROM audit_log
UNION ALL SELECT 'settings', COUNT(*) FROM settings;

SELECT '' AS '';
SELECT '=== Sample Users ===' AS '';
SELECT * FROM users LIMIT 5;

SELECT '' AS '';
SELECT '=== Sample Products ===' AS '';
SELECT id, name, price, stock_quantity FROM products LIMIT 5;

SELECT '' AS '';
SELECT '=== Order Summary View ===' AS '';
SELECT * FROM v_order_summary;

SELECT '' AS '';
SELECT '=== Low Stock Products ===' AS '';
SELECT * FROM v_low_stock;
