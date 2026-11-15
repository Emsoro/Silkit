-- Silkit E2E 可选夹具：PostgreSQL 上的 employees 精简镜像库。
-- 由 `python tools/e2e_runner.py --seed-pg-employees` 先 CREATE DATABASE，再用 psql -f 执行。
-- 结构与 MySQL 8.4 的 datacharmer/test_db 对齐（enum 用 CHECK 约束模拟），
-- 数据量刻意控制在几十行，便于对照 MySQL 的 30 万员工 / 284 万薪资大表做跨引擎用例。
-- 可重复执行：先按依赖顺序 DROP 再重建。

DROP VIEW IF EXISTS dept_emp_latest_date;
DROP VIEW IF EXISTS current_dept_emp;
DROP TABLE IF EXISTS dept_manager;
DROP TABLE IF EXISTS dept_emp;
DROP TABLE IF EXISTS salaries;
DROP TABLE IF EXISTS titles;
DROP TABLE IF EXISTS employees;
DROP TABLE IF EXISTS departments;

CREATE TABLE departments (
    dept_no   char(4) NOT NULL PRIMARY KEY,
    dept_name varchar(40) NOT NULL UNIQUE
);

CREATE TABLE employees (
    emp_no     integer NOT NULL PRIMARY KEY,
    birth_date date NOT NULL,
    first_name varchar(14) NOT NULL,
    last_name  varchar(16) NOT NULL,
    gender     varchar(1) NOT NULL CHECK (gender IN ('M', 'F')),
    hire_date  date NOT NULL
);

CREATE TABLE dept_emp (
    emp_no    integer NOT NULL REFERENCES employees(emp_no) ON DELETE CASCADE,
    dept_no   char(4) NOT NULL REFERENCES departments(dept_no) ON DELETE CASCADE,
    from_date date NOT NULL,
    to_date   date NOT NULL,
    PRIMARY KEY (emp_no, dept_no)
);

CREATE TABLE dept_manager (
    emp_no    integer NOT NULL REFERENCES employees(emp_no) ON DELETE CASCADE,
    dept_no   char(4) NOT NULL REFERENCES departments(dept_no) ON DELETE CASCADE,
    from_date date NOT NULL,
    to_date   date NOT NULL,
    PRIMARY KEY (emp_no, dept_no)
);

CREATE TABLE titles (
    emp_no    integer NOT NULL REFERENCES employees(emp_no) ON DELETE CASCADE,
    title     varchar(50) NOT NULL,
    from_date date NOT NULL,
    to_date   date,
    PRIMARY KEY (emp_no, title, from_date)
);

CREATE TABLE salaries (
    emp_no    integer NOT NULL REFERENCES employees(emp_no) ON DELETE CASCADE,
    salary    integer NOT NULL,
    from_date date NOT NULL,
    to_date   date NOT NULL,
    PRIMARY KEY (emp_no, from_date)
);

CREATE VIEW current_dept_emp AS
SELECT emp_no, dept_no, from_date, to_date FROM dept_emp WHERE to_date = '9999-01-01';

CREATE VIEW dept_emp_latest_date AS
SELECT emp_no, max(from_date) AS from_date, max(to_date) AS to_date
FROM dept_emp GROUP BY emp_no;

INSERT INTO departments (dept_no, dept_name) VALUES
('d001', 'Marketing'), ('d002', 'Finance'), ('d003', 'Human Resources'),
('d004', 'Production'), ('d005', 'Development'), ('d006', 'Quality Management'),
('d007', 'Sales'), ('d008', 'Research'), ('d009', 'Customer Service');

INSERT INTO employees (emp_no, birth_date, first_name, last_name, gender, hire_date) VALUES
(10001, '1953-09-02', 'Georgi', 'Facello', 'M', '1986-06-26'),
(10002, '1964-06-02', 'Bezalel', 'Simmel', 'F', '1985-11-21'),
(10003, '1959-12-03', 'Parto', 'Bamford', 'M', '1986-08-28'),
(10004, '1954-05-01', 'Chirstian', 'Koblick', 'M', '1986-12-01'),
(10005, '1955-01-21', 'Kyoichi', 'Maliniak', 'M', '1989-09-12'),
(10006, '1953-04-20', 'Anneke', 'Preusig', 'F', '1989-06-02'),
(10007, '1957-05-23', 'Tzvetan', 'Zielinski', 'F', '1989-02-10'),
(10008, '1958-02-19', 'Saniya', 'Kalloufi', 'M', '1994-09-15'),
(10009, '1952-04-19', 'Sumant', 'Peac', 'F', '1985-02-18'),
(10010, '1963-06-01', 'Duangkaew', 'Piveteau', 'F', '1989-08-24'),
(10011, '1953-11-07', 'Mary', 'Sluis', 'F', '1990-01-22'),
(10012, '1960-10-04', 'Patricio', 'Bridgland', 'M', '1992-12-18');

INSERT INTO dept_emp (emp_no, dept_no, from_date, to_date) VALUES
(10001, 'd005', '1986-06-26', '9999-01-01'),
(10002, 'd007', '1996-08-03', '9999-01-01'),
(10003, 'd004', '1995-12-03', '9999-01-01'),
(10004, 'd004', '1986-12-01', '9999-01-01'),
(10005, 'd003', '1989-09-12', '9999-01-01'),
(10006, 'd005', '1989-06-02', '9999-01-01'),
(10007, 'd005', '1989-02-10', '9999-01-01'),
(10008, 'd005', '1994-09-15', '9999-01-01'),
(10009, 'd006', '1985-02-18', '9999-01-01'),
(10010, 'd004', '1989-08-24', '1996-11-25'),
(10010, 'd006', '1996-11-25', '9999-01-01'),
(10011, 'd001', '1990-01-22', '9999-01-01'),
(10012, 'd002', '1992-12-18', '9999-01-01');

INSERT INTO dept_manager (emp_no, dept_no, from_date, to_date) VALUES
(10011, 'd001', '1990-01-22', '9999-01-01'),
(10012, 'd002', '1992-12-18', '9999-01-01'),
(10005, 'd003', '1989-09-12', '9999-01-01'),
(10003, 'd004', '1995-12-03', '9999-01-01');

INSERT INTO titles (emp_no, title, from_date, to_date) VALUES
(10001, 'Senior Engineer', '1986-06-26', '9999-01-01'),
(10002, 'Staff', '1996-08-03', '9999-01-01'),
(10003, 'Senior Engineer', '1995-12-03', '9999-01-01'),
(10004, 'Engineer', '1986-12-01', '1995-12-01'),
(10004, 'Senior Engineer', '1995-12-01', '9999-01-01'),
(10005, 'Senior Staff', '1989-09-12', '9999-01-01'),
(10006, 'Senior Engineer', '1989-06-02', '9999-01-01'),
(10007, 'Senior Staff', '1989-02-10', '9999-01-01'),
(10008, 'Assistant Engineer', '1994-09-15', '9999-01-01'),
(10009, 'Assistant Engineer', '1985-02-18', '9999-01-01'),
(10010, 'Engineer', '1989-08-24', '1996-11-25'),
(10011, 'Staff', '1990-01-22', '1996-11-09'),
(10012, 'Engineer', '1992-12-18', '9999-01-01');

INSERT INTO salaries (emp_no, salary, from_date, to_date) VALUES
(10001, 60117, '1986-06-26', '1987-06-26'),
(10001, 62102, '1987-06-26', '1988-06-25'),
(10001, 66074, '1988-06-25', '1989-06-25'),
(10001, 66596, '1989-06-25', '1990-06-25'),
(10001, 66961, '1990-06-25', '1991-06-25'),
(10002, 65828, '1996-08-03', '1997-08-03'),
(10002, 65909, '1997-08-03', '1998-08-03'),
(10003, 40006, '1995-12-03', '1996-12-02'),
(10003, 43616, '1996-12-02', '1997-12-02'),
(10004, 40054, '1986-12-01', '1987-12-01'),
(10005, 78228, '1989-09-12', '1990-09-12'),
(10006, 40000, '1989-06-02', '1990-06-02'),
(10007, 56724, '1989-02-10', '1990-02-10'),
(10008, 46671, '1994-09-15', '1995-09-15'),
(10009, 60929, '1985-02-18', '1986-02-18'),
(10010, 72488, '1989-08-24', '1990-08-24'),
(10011, 42365, '1990-01-22', '1991-01-22'),
(10012, 40000, '1992-12-18', '1993-12-18');
