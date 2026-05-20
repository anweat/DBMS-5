-- DBMS-5 full acceptance demo script.
-- Run from CLI after starting dbms.exe:
--   source demos/full_acceptance_demo.sql

CONNECT 'root' IDENTIFIED BY 'root';

DROP DATABASE IF EXISTS dbms5_demo;
CREATE DATABASE dbms5_demo;
USE dbms5_demo;

-- 1. DDL: database, tables, constraints, foreign key.
CREATE TABLE users (
    uid INTEGER PRIMARY KEY AUTO_INCREMENT,
    name VARCHAR(40) NOT NULL,
    region VARCHAR(20) DEFAULT 'unknown',
    age INTEGER DEFAULT 0,
    email VARCHAR(80) UNIQUE
);

CREATE TABLE orders (
    oid INTEGER PRIMARY KEY AUTO_INCREMENT,
    user_id INTEGER,
    amount DOUBLE NOT NULL,
    status VARCHAR(20) DEFAULT 'new',
    CONSTRAINT fk_orders_user FOREIGN KEY (user_id) REFERENCES users(uid)
);

CREATE TABLE audit_log (
    id INTEGER PRIMARY KEY AUTO_INCREMENT,
    action VARCHAR(40) NOT NULL,
    actor VARCHAR(40) DEFAULT 'system'
);

SHOW TABLES;
DESCRIBE users;

-- 2. DML: insert, select, filter, order, limit, distinct.
INSERT INTO users (name, region, age, email) VALUES ('Alice', 'East', 30, 'alice@example.com');
INSERT INTO users (name, region, age, email) VALUES ('Bob', 'West', 25, 'bob@example.com');
INSERT INTO users (name, region, age, email) VALUES ('Cindy', 'East', 28, 'cindy@example.com');

INSERT INTO orders (user_id, amount, status) VALUES (1, 199.99, 'paid');
INSERT INTO orders (user_id, amount, status) VALUES (1, 88.00, 'new');
INSERT INTO orders (user_id, amount, status) VALUES (2, 320.50, 'paid');
INSERT INTO orders (user_id, amount, status) VALUES (3, 45.00, 'new');

SELECT * FROM users ORDER BY uid ASC;
SELECT name, region, age FROM users WHERE age >= 28 ORDER BY age DESC LIMIT 2;
SELECT DISTINCT region FROM users ORDER BY region ASC;

-- 3. JOIN: implicit join, explicit INNER JOIN, aggregate on joined tables.
SELECT u.name, o.amount, o.status
FROM users u, orders o
WHERE u.uid = o.user_id AND o.amount > 100
ORDER BY o.amount DESC;

SELECT u.region, COUNT(o.oid) AS order_count, SUM(o.amount) AS total_amount
FROM users u INNER JOIN orders o ON u.uid = o.user_id
GROUP BY u.region
HAVING COUNT(o.oid) >= 2
ORDER BY total_amount DESC;

-- 4. UPDATE and DELETE.
UPDATE users SET age = 31, region = 'North' WHERE name = 'Alice';
SELECT uid, name, region, age FROM users ORDER BY uid ASC;

DELETE FROM orders WHERE amount < 50;
SELECT oid, user_id, amount, status FROM orders ORDER BY oid ASC;

-- 5. ALTER TABLE and index.
ALTER TABLE users ADD COLUMN level VARCHAR(20) DEFAULT 'normal';
ALTER TABLE users MODIFY COLUMN name VARCHAR(80) NOT NULL;
DESCRIBE users;

CREATE INDEX idx_users_region ON users(region);
CREATE INDEX idx_orders_user_status ON orders(user_id, status);

SELECT name, region FROM users WHERE region = 'North';

DROP INDEX idx_users_region ON users;
CREATE INDEX idx_users_region ON users(region);

-- 6. Transaction: rollback and commit.
BEGIN;
INSERT INTO audit_log (action, actor) VALUES ('temporary-change', 'demo');
UPDATE users SET age = 99 WHERE name = 'Bob';
ROLLBACK;
SELECT name, age FROM users ORDER BY uid ASC;
SELECT * FROM audit_log;

BEGIN;
INSERT INTO audit_log (action, actor) VALUES ('commit-change', 'demo');
UPDATE orders SET status = 'archived' WHERE amount > 300;
COMMIT;
SELECT * FROM audit_log;
SELECT oid, amount, status FROM orders ORDER BY oid ASC;

-- 7. Security: users, grant, revoke, unauthorized write.
CREATE USER 'accept_reader' IDENTIFIED BY 'reader123';
GRANT SELECT ON dbms5_demo.users TO 'accept_reader';
GRANT SELECT ON dbms5_demo.orders TO 'accept_reader';

CONNECT 'accept_reader' IDENTIFIED BY 'reader123';
USE dbms5_demo;
SELECT name, region FROM users ORDER BY uid ASC;
INSERT INTO users (name, region, age, email) VALUES ('Blocked', 'South', 18, 'blocked@example.com');

CONNECT 'root' IDENTIFIED BY 'root';
USE dbms5_demo;
REVOKE SELECT ON dbms5_demo.orders FROM 'accept_reader';

CONNECT 'accept_reader' IDENTIFIED BY 'reader123';
USE dbms5_demo;
SELECT * FROM orders;

CONNECT 'root' IDENTIFIED BY 'root';
USE dbms5_demo;
DROP USER 'accept_reader';

-- 8. Cleanup statements for repeated rehearsals.
DROP INDEX idx_orders_user_status ON orders;
DROP INDEX idx_users_region ON users;
DROP TABLE audit_log;
DROP TABLE orders;
DROP TABLE users;
DROP DATABASE dbms5_demo;
