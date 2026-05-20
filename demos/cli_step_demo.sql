-- DBMS-5 CLI step demo.
-- This script is designed for slide-by-slide explanation. Each block contains
-- one capability point and immediately follows it with a query that proves the
-- result.

CONNECT 'root' IDENTIFIED BY 'root';

DROP DATABASE IF EXISTS cli_demo;
CREATE DATABASE cli_demo;
USE cli_demo;

-- 1. DDL + constraints.
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

SHOW TABLES;
DESCRIBE users;

-- 2. DML insert + basic query.
INSERT INTO users (name, region, age, email) VALUES ('Alice', 'East', 30, 'alice@example.com');
INSERT INTO users (name, region, age, email) VALUES ('Bob', 'West', 25, 'bob@example.com');
INSERT INTO users (name, region, age, email) VALUES ('Cindy', 'East', 28, 'cindy@example.com');

INSERT INTO orders (user_id, amount, status) VALUES (1, 199.99, 'paid');
INSERT INTO orders (user_id, amount, status) VALUES (1, 88.00, 'new');
INSERT INTO orders (user_id, amount, status) VALUES (2, 320.50, 'paid');

SELECT uid, name, region, age FROM users ORDER BY uid ASC;
SELECT name, region, age FROM users WHERE age >= 28 ORDER BY age DESC LIMIT 2;

-- 3. Join and aggregate query.
SELECT u.name, o.amount, o.status
FROM users u INNER JOIN orders o ON u.uid = o.user_id
ORDER BY o.amount DESC;

SELECT u.region, COUNT(o.oid) AS order_count, SUM(o.amount) AS total_amount
FROM users u INNER JOIN orders o ON u.uid = o.user_id
GROUP BY u.region
HAVING COUNT(o.oid) >= 2
ORDER BY total_amount DESC;

-- 4. Update, delete, alter and index.
UPDATE users SET age = 31, region = 'North' WHERE name = 'Alice';
DELETE FROM orders WHERE amount < 100;
ALTER TABLE users ADD COLUMN level VARCHAR(20) DEFAULT 'normal';
CREATE INDEX idx_users_region ON users(region);
CREATE INDEX idx_orders_user_status ON orders(user_id, status);

DESCRIBE users;
SELECT uid, name, region, age, level FROM users ORDER BY uid ASC;
SELECT oid, user_id, amount, status FROM orders ORDER BY oid ASC;

-- 5. Transaction rollback and commit.
BEGIN;
UPDATE users SET age = 99 WHERE name = 'Bob';
ROLLBACK;
SELECT name, age FROM users ORDER BY uid ASC;

BEGIN;
UPDATE orders SET status = 'archived' WHERE amount > 300;
COMMIT;
SELECT oid, amount, status FROM orders ORDER BY oid ASC;

-- 6. Security.
CREATE USER 'cli_reader' IDENTIFIED BY 'reader123';
GRANT SELECT ON cli_demo.users TO 'cli_reader';

CONNECT 'cli_reader' IDENTIFIED BY 'reader123';
USE cli_demo;
SELECT name, region FROM users ORDER BY uid ASC;
INSERT INTO users (name, region, age, email) VALUES ('Blocked', 'South', 18, 'blocked@example.com');

CONNECT 'root' IDENTIFIED BY 'root';
DROP USER 'cli_reader';
DROP DATABASE cli_demo;
