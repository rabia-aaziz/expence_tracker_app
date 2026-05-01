
===============================================================
   SMART EXPENSE TRACKER — C Language Backend + HTML Frontend
   University Project | Pakistan & International
===============================================================

FILES:
  server.c      →  C Language HTTP Backend Server
  index.html    →  Frontend (Browser UI)
  expenses.json →  Auto-created: expense data
  budgets.json  →  Auto-created: budget data

---------------------------------------------------------------
HOW TO COMPILE & RUN
---------------------------------------------------------------

STEP 1 — Compile the C server:

  Linux / Mac:
    gcc server.c -o server -Wall

  Windows (with MinGW/GCC):
    gcc server.c -o server.exe -lws2_32

STEP 2 — Put both files in the SAME folder:
    server.c    (or compiled binary)
    index.html

STEP 3 — Run the server:

  Linux / Mac:
    ./server

  Windows:
    server.exe

STEP 4 — Open your browser:
    http://localhost:8080

That's it! The C server serves the HTML file and handles
all API calls. Data is saved to expenses.json automatically.

---------------------------------------------------------------
API REFERENCE  (C Backend Endpoints)
---------------------------------------------------------------

  GET    /                    Serve index.html
  GET    /api/expenses        Get all expenses (JSON)
  POST   /api/expenses        Add new expense
  DELETE /api/expenses/:id    Delete expense by ID
  GET    /api/budgets         Get all budgets
  POST   /api/budgets         Set/update budget
  GET    /api/summary         Stats summary

Example — Add expense via curl:
  curl -X POST http://localhost:8080/api/expenses \
    -H "Content-Type: application/json" \
    -d '{"name":"Biryani","amount":850,"category":"Food","date":"2025-01-01","note":"Burns Road"}'

Example — Get all expenses:
  curl http://localhost:8080/api/expenses

---------------------------------------------------------------
C LANGUAGE CONCEPTS USED (For University Report)
---------------------------------------------------------------

  struct          Expense and Budget data structures
  Arrays          expenses[], budgets[] global arrays
  File I/O        fopen/fwrite/fread — JSON persistence
  Sockets         socket(), bind(), listen(), accept()
  String ops      strncpy, strcmp, strstr, strlen
  Memory          malloc/free for dynamic buffers
  Pointers        *buf, *req, *Expense pointer ops
  HTTP Protocol   Manual HTTP/1.1 request parsing
  JSON            Custom parser and builder (no library)
  #define         Constants and config values
  Static vars     Module-level state management
  Preprocessor    #ifdef _WIN32 for cross-platform

===============================================================
