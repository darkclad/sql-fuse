-- MySQL FUSE Test Database Verification
-- Run as: mysql -u fuse_test -p fuse_test < verify_test_db.sql

USE fuse_test;

SELECT '=== Tables ===' AS '';
SHOW TABLES;

SELECT '' AS '';
SELECT '=== Table: users ===' AS '';
SELECT * FROM users;

SELECT '' AS '';
SELECT '=== Table: categories ===' AS '';
SELECT * FROM categories;

SELECT '' AS '';
SELECT '=== Table: products ===' AS '';
SELECT id, sku, name, price, quantity, category_id FROM products;

SELECT '' AS '';
SELECT '=== Table: orders ===' AS '';
SELECT id, order_number, user_id, status, total FROM orders;

SELECT '' AS '';
SELECT '=== Table: order_items ===' AS '';
SELECT * FROM order_items;

SELECT '' AS '';
SELECT '=== View: v_active_users ===' AS '';
SELECT * FROM v_active_users;

SELECT '' AS '';
SELECT '=== View: v_product_catalog ===' AS '';
SELECT id, sku, name, price, category_name FROM v_product_catalog;

SELECT '' AS '';
SELECT '=== View: v_order_summary ===' AS '';
SELECT * FROM v_order_summary;

SELECT '' AS '';
SELECT '=== View: v_revenue_by_category ===' AS '';
SELECT * FROM v_revenue_by_category;

SELECT '' AS '';
SELECT '=== View: v_low_stock ===' AS '';
SELECT * FROM v_low_stock;

SELECT '' AS '';
SELECT '=== Stored Procedures ===' AS '';
SELECT routine_name, routine_type
FROM information_schema.routines
WHERE routine_schema = 'fuse_test' AND routine_type = 'PROCEDURE';

SELECT '' AS '';
SELECT '=== Stored Functions ===' AS '';
SELECT routine_name, routine_type
FROM information_schema.routines
WHERE routine_schema = 'fuse_test' AND routine_type = 'FUNCTION';

SELECT '' AS '';
SELECT '=== Triggers ===' AS '';
SELECT trigger_name, event_manipulation, event_object_table, action_timing
FROM information_schema.triggers
WHERE trigger_schema = 'fuse_test';

SELECT '' AS '';
SELECT '=== Events ===' AS '';
SELECT event_name, status, interval_value, interval_field
FROM information_schema.events
WHERE event_schema = 'fuse_test';

SELECT '' AS '';
SELECT '=== Test Procedure Call: sp_get_user_orders(2) ===' AS '';
CALL sp_get_user_orders(2);

SELECT '' AS '';
SELECT '=== Test Function Call: fn_get_user_fullname(2) ===' AS '';
SELECT fn_get_user_fullname(2) AS user_fullname;

SELECT '' AS '';
SELECT '=== Test Function Call: fn_calculate_order_total(1) ===' AS '';
SELECT fn_calculate_order_total(1) AS order_total;

SELECT '' AS '';
SELECT '=== Verification Complete ===' AS '';
