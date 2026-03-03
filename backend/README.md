# Score Analyzer Backend

A comprehensive **Students Score Management Engine** API server built with CivetWeb for managing and analyzing student scores.

**⚡ This project is fully self-contained and does not require external CivetWeb source code.**

## Features

- **Health Check Endpoint**: `/` - Welcome page and server status
- **RESTful User API**: CRUD operations for user management
- **Data API**: Sample data endpoint for testing
- **Test Endpoints**: Various testing scenarios (performance, errors, delays, large responses)
- **JSON Responses**: All endpoints return structured JSON responses
- **Request Logging**: Comprehensive request logging with timestamps
- **Graceful Shutdown**: Clean shutdown via `/exit` endpoint
- **Auto-reload**: `make watch` for development with automatic restarts

## Building

```bash
make all
```

## Running

```bash
# Run directly
make run

# Run with auto-reload (like nodemon)
make watch

# Or run in background
make test

# Stop background server
make stop
```

## API Endpoints

### Welcome Page
- **GET** `/` - Welcome message and server info

### Health Check
- **GET** `/health` - Server health status

### Users API
- **GET** `/api/users` - List all users
- **POST** `/api/users` - Create new user
- **GET** `/api/users/{id}` - Get specific user
- **PUT** `/api/users/{id}` - Update specific user
- **DELETE** `/api/users/{id}` - Delete specific user

### Data API
- **GET** `/api/data` - Get sample data
- **POST** `/api/data` - Submit data for processing

### Test Endpoints
- **GET** `/api/test` - List available test types
- **GET** `/api/test/performance` - Performance test with timing
- **GET** `/api/test/error` - Returns a 500 error for testing
- **GET** `/api/test/delay` - 2-second delayed response
- **GET** `/api/test/large-response` - Large payload response

### Control
- **GET** `/exit` - Gracefully shutdown the server

## Example Usage with curl

```bash
# Health check
curl http://localhost:8090/health

# Get users
curl http://localhost:8090/api/users

# Create user
curl -X POST http://localhost:8090/api/users \
  -H "Content-Type: application/json" \
  -d '{"name":"John Doe","email":"john@example.com"}'

# Update user
curl -X PUT http://localhost:8090/api/users/1 \
  -H "Content-Type: application/json" \
  -d '{"name":"John Updated","email":"john.updated@example.com"}'

# Delete user
curl -X DELETE http://localhost:8090/api/users/1

# Test endpoints
curl http://localhost:8090/api/test/performance
curl http://localhost:8090/api/test/delay
```

## Example Usage with Postman

1. **Health Check**:
   - Method: GET
   - URL: `http://localhost:8090/health`

2. **Create User**:
   - Method: POST
   - URL: `http://localhost:8090/api/users`
   - Headers: `Content-Type: application/json`
   - Body (raw JSON):
     ```json
     {
       "name": "Test User",
       "email": "test@example.com"
     }
     ```

3. **Performance Test**:
   - Method: GET
   - URL: `http://localhost:8090/api/test/performance`

4. **Error Test**:
   - Method: GET
   - URL: `http://localhost:8090/api/test/error`

## Response Format

All endpoints return JSON responses in the following format:

```json
{
  "status": "success|error|healthy",
  "message": "Description of the result",
  "timestamp": 1234567890,
  "request_count": 42,
  "data": { /* endpoint-specific data */ }
}
```

## Configuration

- **Port**: 8090 (configurable in source code)
- **Timeout**: 10 seconds
- **Logging**: Access and error logs saved to `score_analyzer_*.log`

## Cleaning Up

```bash
# Remove build files and logs
make clean

# Stop any running server
make stop
```

## Requirements

- CivetWeb library
- GCC or compatible C compiler
- pthreads support
- Linux/Unix environment (Windows support available)