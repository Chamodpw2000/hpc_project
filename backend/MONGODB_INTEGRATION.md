# MongoDB Integration Guide - Score Analyzer Backend

## ✅ What's Been Set Up

Your backend now has complete MongoDB Atlas integration with:

1. **MongoDB C Driver** - Installed and configured
2. **Database Helper** (`db.c` / `db.h`) - Ready-to-use database operations
3. **Configuration System** (`config.c` / `config.h`) - Environment-based config
4. **Build System** - Makefile updated with MongoDB support

## 📋 Quick Start (5 Steps)

### Step 1: Get MongoDB Atlas Connection String

1. Go to **https://cloud.mongodb.com**
2. Sign in (or create free account)
3. **Create Cluster** (if needed):
   - Click "Build a Database"
   - Select **FREE** (M0 tier)
   - Choose region → Create
4. **Create Database User**:
   - Sidebar: "Database Access"
   - "Add New Database User"
   - Username: `your_username`
   - Password: `your_password` (save this!)
   - Privileges: "Read and write to any database"
5. **Whitelist Your IP**:
   - Sidebar: "Network Access"
   - "Add IP Address"
   - "Allow Access from Anywhere" (0.0.0.0/0)
   - Confirm
6. **Get Connection String**:
   - Sidebar: "Database" (Clusters)
   - Click "Connect" button
   - Choose "Connect your application"
   - Driver: **C** / Version: **1.17 or later**
   - **Copy the connection string**

Example string:
```
mongodb+srv://myuser:mypass@cluster0.abc123.mongodb.net/?retryWrites=true&w=majority
```

### Step 2: Create Your Config File

```bash
cd /home/chamod/hpc_project/backend
cp config.env.example config.env
nano config.env  # or use your preferred editor
```

Update `config.env` with your actual values:
```env
MONGODB_URI=mongodb+srv://your_username:your_password@cluster0.xxxxx.mongodb.net/score_analyzer?retryWrites=true&w=majority
DB_NAME=score_analyzer
PORT=8090
```

⚠️ **Important**: Replace:
- `your_username` → your MongoDB username
- `your_password` → your MongoDB password  
- `cluster0.xxxxx` → your cluster address
- Special characters in password must be URL-encoded (@ → %40, # → %23, etc.)

### Step 3: Build the Project

```bash
cd /home/chamod/hpc_project/backend
make clean
make all
```

### Step 4: Run the Server

```bash
# Normal run
make run

# Or with auto-reload (development)
make watch
```

### Step 5: Test It!

```bash
# Test server is running
curl http://localhost:8090/

# Test MongoDB connection (you'll add this endpoint)
curl http://localhost:8090/api/db/status
```

## 📚 Available Database Functions

Your `db.h` provides these functions:

### Connection Management
```c
db_connection_t* db_init(const char *connection_string, const char *db_name);
void db_cleanup(db_connection_t *db);
int db_test_connection(db_connection_t *db);
```

### Student Operations
```c
// Create new student
int db_create_student(db_connection_t *db, const char *name, 
                      const char *email, const char *student_id);

// Get all students (returns JSON string)
char* db_get_all_students(db_connection_t *db);

// Get specific student
char* db_get_student_by_id(db_connection_t *db, const char *student_id);

// Update student
int db_update_student(db_connection_t *db, const char *student_id, 
                      const char *name, const char *email);

// Delete student
int db_delete_student(db_connection_t *db, const char *student_id);
```

### Score Operations
```c
// Add score for student
int db_add_score(db_connection_t *db, const char *student_id, 
                 const char *subject, double score);

// Get all scores for a student
char* db_get_student_scores(db_connection_t *db, const char *student_id);

// Get all scores
char* db_get_all_scores(db_connection_t *db);
```

## 🎯 Next Steps: Integrate into Your Server

To use MongoDB in your server, you need to:

1. **Add includes** to `score_analyzer_backend.c`:
```c
#include "db.h"
#include "config.h"
```

2. **Initialize in main()**:
```c
// Load config
config_t *config = config_load("config.env");

// Connect to MongoDB
db_connection_t *db = db_init(config->mongodb_uri, config->db_name);

if (db && db_test_connection(db)) {
    printf("✓ Connected to MongoDB!\n");
} else {
    fprintf(stderr, "✗ MongoDB connection failed!\n");
}
```

3. **Add API endpoints** (example):
```c
// GET /api/students - List all students
char *students_json = db_get_all_students(db);
// Send students_json in response
free(students_json);

// POST /api/students - Create student
db_create_student(db, "John Doe", "john@example.com", "S001");

// POST /api/scores - Add score
db_add_score(db, "S001", "Math", 95.5);
```

4. **Cleanup on exit**:
```c
db_cleanup(db);
config_free(config);
```

## 🔧 Troubleshooting

### "Failed to create MongoDB client"
- Check your connection string format
- Verify username/password are correct
- URL-encode special characters in password

### "MongoDB ping failed"
- Check IP whitelist in MongoDB Atlas Network Access
- Verify internet connection
- Try: `ping cluster0.xxxxx.mongodb.net`

### "Config file not found"
- Make sure `config.env` exists in `/home/chamod/hpc_project/backend/`
- Check file permissions: `ls -la config.env`
- Server will use defaults if config missing

### Compile errors
```bash
# Verify MongoDB driver is installed
pkg-config --modversion libmongoc-1.0

# Should show version like: 1.26.0
```

## 📖 Example: Full Integration

See `MONGODB_SETUP.md` for detailed MongoDB Atlas setup.
See `db.h` for all available database functions.

## 🔒 Security Notes

- ✅ `config.env` is in `.gitignore` (won't be committed)
- ✅ Never share your MongoDB password
- ✅ For production: Use environment variables instead of config file
- ✅ For production: Restrict IP whitelist to your server's IP only

## 📝 What's Next?

Would you like me to:
1. Add MongoDB endpoints to your server?
2. Create example student/score management APIs?
3. Add authentication/authorization?
4. Set up database indexes for better performance?

Just let me know!
