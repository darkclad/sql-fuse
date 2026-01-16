-- Oracle test database setup script for sql-fuse
-- Run this as SYSDBA or a user with CREATE USER privileges
--
-- Usage:
--   sqlplus / as sysdba @setup_test_db.sql
--   or
--   sqlplus system/password @setup_test_db.sql

-- Create test user/schema (in Oracle, users and schemas are equivalent)
-- Drop if exists first
DECLARE
    user_exists NUMBER;
BEGIN
    SELECT COUNT(*) INTO user_exists FROM all_users WHERE username = 'SQLFUSE_TEST';
    IF user_exists > 0 THEN
        EXECUTE IMMEDIATE 'DROP USER sqlfuse_test CASCADE';
    END IF;
END;
/

CREATE USER sqlfuse_test IDENTIFIED BY testpass123
    DEFAULT TABLESPACE users
    TEMPORARY TABLESPACE temp
    QUOTA UNLIMITED ON users;

GRANT CONNECT, RESOURCE TO sqlfuse_test;
GRANT CREATE SESSION TO sqlfuse_test;
GRANT CREATE TABLE TO sqlfuse_test;
GRANT CREATE VIEW TO sqlfuse_test;
GRANT CREATE PROCEDURE TO sqlfuse_test;
GRANT CREATE TRIGGER TO sqlfuse_test;
GRANT CREATE SEQUENCE TO sqlfuse_test;

-- Connect as the test user
CONNECT sqlfuse_test/testpass123

-- Create tables
CREATE TABLE employees (
    id NUMBER PRIMARY KEY,
    name VARCHAR2(100) NOT NULL,
    email VARCHAR2(100),
    department VARCHAR2(50),
    salary NUMBER(10,2),
    hire_date DATE DEFAULT SYSDATE
);

CREATE TABLE departments (
    id NUMBER PRIMARY KEY,
    name VARCHAR2(100) NOT NULL,
    location VARCHAR2(100),
    budget NUMBER(15,2)
);

CREATE TABLE projects (
    id NUMBER PRIMARY KEY,
    name VARCHAR2(100) NOT NULL,
    description CLOB,
    start_date DATE,
    end_date DATE,
    status VARCHAR2(20) DEFAULT 'ACTIVE',
    CONSTRAINT chk_status CHECK (status IN ('ACTIVE', 'COMPLETED', 'CANCELLED'))
);

CREATE TABLE employee_projects (
    employee_id NUMBER,
    project_id NUMBER,
    role VARCHAR2(50),
    assigned_date DATE DEFAULT SYSDATE,
    PRIMARY KEY (employee_id, project_id),
    FOREIGN KEY (employee_id) REFERENCES employees(id),
    FOREIGN KEY (project_id) REFERENCES projects(id)
);

-- Create indexes
CREATE INDEX idx_employees_department ON employees(department);
CREATE INDEX idx_employees_email ON employees(email);
CREATE INDEX idx_projects_status ON projects(status);

-- Insert sample data
INSERT INTO departments (id, name, location, budget) VALUES (1, 'Engineering', 'Building A', 1000000);
INSERT INTO departments (id, name, location, budget) VALUES (2, 'Marketing', 'Building B', 500000);
INSERT INTO departments (id, name, location, budget) VALUES (3, 'Sales', 'Building C', 750000);

INSERT INTO employees (id, name, email, department, salary, hire_date)
VALUES (1, 'John Doe', 'john.doe@example.com', 'Engineering', 85000, TO_DATE('2020-01-15', 'YYYY-MM-DD'));
INSERT INTO employees (id, name, email, department, salary, hire_date)
VALUES (2, 'Jane Smith', 'jane.smith@example.com', 'Marketing', 75000, TO_DATE('2019-06-01', 'YYYY-MM-DD'));
INSERT INTO employees (id, name, email, department, salary, hire_date)
VALUES (3, 'Bob Wilson', 'bob.wilson@example.com', 'Engineering', 95000, TO_DATE('2018-03-20', 'YYYY-MM-DD'));
INSERT INTO employees (id, name, email, department, salary, hire_date)
VALUES (4, 'Alice Johnson', 'alice.j@example.com', 'Sales', 80000, TO_DATE('2021-02-10', 'YYYY-MM-DD'));
INSERT INTO employees (id, name, email, department, salary, hire_date)
VALUES (5, 'Charlie Brown', 'charlie.b@example.com', 'Engineering', 90000, TO_DATE('2020-08-05', 'YYYY-MM-DD'));

INSERT INTO projects (id, name, description, start_date, end_date, status)
VALUES (1, 'Website Redesign', 'Modernize company website', TO_DATE('2023-01-01', 'YYYY-MM-DD'), TO_DATE('2023-06-30', 'YYYY-MM-DD'), 'COMPLETED');
INSERT INTO projects (id, name, description, start_date, end_date, status)
VALUES (2, 'Mobile App', 'Build mobile application', TO_DATE('2023-03-01', 'YYYY-MM-DD'), NULL, 'ACTIVE');
INSERT INTO projects (id, name, description, start_date, end_date, status)
VALUES (3, 'Data Migration', 'Migrate to new database', TO_DATE('2023-06-01', 'YYYY-MM-DD'), NULL, 'ACTIVE');

INSERT INTO employee_projects (employee_id, project_id, role, assigned_date)
VALUES (1, 1, 'Lead Developer', TO_DATE('2023-01-01', 'YYYY-MM-DD'));
INSERT INTO employee_projects (employee_id, project_id, role, assigned_date)
VALUES (3, 1, 'Senior Developer', TO_DATE('2023-01-15', 'YYYY-MM-DD'));
INSERT INTO employee_projects (employee_id, project_id, role, assigned_date)
VALUES (1, 2, 'Backend Developer', TO_DATE('2023-03-01', 'YYYY-MM-DD'));
INSERT INTO employee_projects (employee_id, project_id, role, assigned_date)
VALUES (5, 2, 'Frontend Developer', TO_DATE('2023-03-15', 'YYYY-MM-DD'));
INSERT INTO employee_projects (employee_id, project_id, role, assigned_date)
VALUES (3, 3, 'Lead Developer', TO_DATE('2023-06-01', 'YYYY-MM-DD'));

-- Create a view
CREATE OR REPLACE VIEW v_employee_details AS
SELECT
    e.id,
    e.name,
    e.email,
    e.department,
    e.salary,
    e.hire_date,
    d.name AS dept_name,
    d.location AS dept_location
FROM employees e
LEFT JOIN departments d ON e.department = d.name;

-- Create a view for project assignments
CREATE OR REPLACE VIEW v_project_assignments AS
SELECT
    p.name AS project_name,
    p.status,
    e.name AS employee_name,
    ep.role,
    ep.assigned_date
FROM projects p
JOIN employee_projects ep ON p.id = ep.project_id
JOIN employees e ON e.id = ep.employee_id;

-- Create a simple stored procedure
CREATE OR REPLACE PROCEDURE get_employee_count (
    p_department IN VARCHAR2,
    p_count OUT NUMBER
) AS
BEGIN
    SELECT COUNT(*) INTO p_count
    FROM employees
    WHERE department = p_department;
END;
/

-- Create a function
CREATE OR REPLACE FUNCTION get_department_budget (
    p_dept_name IN VARCHAR2
) RETURN NUMBER AS
    v_budget NUMBER;
BEGIN
    SELECT budget INTO v_budget
    FROM departments
    WHERE name = p_dept_name;
    RETURN v_budget;
EXCEPTION
    WHEN NO_DATA_FOUND THEN
        RETURN 0;
END;
/

-- Create a trigger
CREATE OR REPLACE TRIGGER trg_employees_audit
BEFORE INSERT OR UPDATE ON employees
FOR EACH ROW
BEGIN
    IF INSERTING THEN
        :NEW.hire_date := NVL(:NEW.hire_date, SYSDATE);
    END IF;
END;
/

-- Commit all changes
COMMIT;

-- Display summary
SELECT 'Tables created: ' || COUNT(*) FROM user_tables;
SELECT 'Views created: ' || COUNT(*) FROM user_views;
SELECT 'Procedures created: ' || COUNT(*) FROM user_procedures WHERE object_type = 'PROCEDURE';
SELECT 'Functions created: ' || COUNT(*) FROM user_procedures WHERE object_type = 'FUNCTION';
SELECT 'Triggers created: ' || COUNT(*) FROM user_triggers;

PROMPT
PROMPT Oracle test database setup complete!
PROMPT Schema: SQLFUSE_TEST
PROMPT Password: testpass123
PROMPT
