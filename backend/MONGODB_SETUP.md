# MongoDB Atlas Setup Guide for Score Analyzer Backend

## Step 1: Get Your MongoDB Atlas Connection String

1. **Go to MongoDB Atlas**: https://www.mongodb.com/cloud/atlas
2. **Sign in** or **Create a free account**
3. **Create a Cluster** (if you don't have one):
   - Click "Build a Database"
   - Choose "FREE" tier (M0)
   - Select a cloud provider and region
   - Click "Create Cluster"

4. **Create Database User**:
   - Go to "Database Access" in left sidebar
   - Click "Add New Database User"
   - Create username and password
   - Set privileges to "Read and write to any database"
   - Click "Add User"

5. **Whitelist IP Address**:
   - Go to "Network Access" in left sidebar
   - Click "Add IP Address"
   - Click "Allow Access from Anywhere" (0.0.0.0/0) for development
   - Click "Confirm"

6. **Get Connection String**:
   - Go to "Database" (Clusters)
   - Click "Connect" on your cluster
   - Choose "Connect your application"
   - Driver: C / Version: 1.17 or later
   - Copy the connection string

## Step 2: Configure Your Application

1. Create `config.env` from the example:
   ```bash
   cd /home/chamod/hpc_project/backend
   cp config.env.example config.env
   ```

2. Edit `config.env` and replace with your actual connection string:
   ```
   MONGODB_URI=mongodb+srv://your_username:your_password@cluster0.xxxxx.mongodb.net/score_analyzer?retryWrites=true&w=majority
   DB_NAME=score_analyzer
   PORT=8090
   ```

3. Replace:
   - `your_username` with your MongoDB username
   - `your_password` with your MongoDB password
   - `cluster0.xxxxx` with your actual cluster address
   - `score_analyzer` with your database name

## Step 3: Build and Run

```bash
# Build with MongoDB support
make clean
make all

# Run the server
make run
```

## Step 4: Test the Connection

```bash
# Test health endpoint
curl http://localhost:8090/health

# Test database connection
curl http://localhost:8090/api/db/test
```

## Example Connection Strings

### MongoDB Atlas (Cloud):
```
mongodb+srv://myuser:mypass@cluster0.abc123.mongodb.net/score_analyzer?retryWrites=true&w=majority
```

### Local MongoDB:
```
mongodb://localhost:27017/score_analyzer
```

## Security Notes

- ⚠️ **Never commit `config.env` to version control**
- The `config.env` file is already in `.gitignore`
- For production, use environment variables instead of config files
- Restrict IP whitelist to your actual server IPs in production

## Troubleshooting

### Connection timeout:
- Check your IP is whitelisted in MongoDB Atlas Network Access
- Verify internet connection
- Check firewall settings

### Authentication failed:
- Verify username and password are correct
- URL encode special characters in password (e.g., @ become %40)
- Check user has proper database permissions

### SSL/TLS errors:
- Make sure you're using `mongodb+srv://` (not `mongodb://`)
- Update system CA certificates: `sudo apt update && sudo apt upgrade ca-certificates`
