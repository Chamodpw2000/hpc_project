import os
import subprocess
import struct
try:
    import pymongo
except ImportError:
    print("Installing pymongo...")
    subprocess.run("pip install pymongo", shell=True, check=True)
    import pymongo

# ==============================================================================
# Google Colab CUDA Implementation wrapper for Score Analyzer Calc Engine
#
# INSTRUCTIONS FOR GOOGLE COLAB:
# 1. Open Google Colab (colab.research.google.com).
# 2. Change Runtime to GPU: Runtime -> Change runtime type -> Hardware accelerator -> T4 GPU.
# 3. Paste this entire Python script into a code cell and run it.
# ==============================================================================

print("Connecting to MongoDB Atlas...")
# Using the connection string from your backend config
MONGO_URI = "mongodb+srv://mailtochamodw_db_user:92nP7N2ACAEdiZHz@score-analyzer.iio05hg.mongodb.net/?appName=score-analyzer"
client = pymongo.MongoClient(MONGO_URI)
db = client["score_analyzer"]
scores_col = db["scores"]

print("Fetching scores from database...")
# Fetch all scores from the DB. 
# In a huge production database, you might limit this or stream it, 
# but for Colab VM memory, millions of float scores easily fit.
db_scores = list(scores_col.find({}, {"score": 1, "_id": 0}))

num_scores = len(db_scores)
print(f"Fetched {num_scores} scores from the database!")

# Write scores to a binary file so C++ can read it blazingly fast
bin_file_path = "scores.bin"
print(f"Writing scores to binary file '{bin_file_path}' for CUDA processing...")
with open(bin_file_path, "wb") as f:
    for doc in db_scores:
        # Some records might be missing the score field, safely default to 0.0
        val = float(doc.get("score", 0.0))
        # Pack as 8-byte double (little-endian)
        f.write(struct.pack('<d', val))

print("Binary file written successfully.\n")

cuda_source_code = r'''
#include <iostream>
#include <vector>
#include <cmath>
#include <chrono>
#include <fstream>
#include <cuda_runtime.h>
#include <thrust/sort.h>
#include <thrust/execution_policy.h>

using namespace std;

#define CHECK_CUDA(call) do { \
    cudaError_t err = (call); \
    if (err != cudaSuccess) { \
        cerr << "CUDA error: " << cudaGetErrorString(err) << " at line " << __LINE__ << endl; \
        return 1; \
    } \
} while (0)

// -----------------------------------------------------------------------------
// Atomic operations for double precision natively on modern GPUs (like Colab T4)
// -----------------------------------------------------------------------------

__device__ void atomicMinDouble(double* address, double val) {
    unsigned long long int* address_as_ull = (unsigned long long int*)address;
    unsigned long long int old = *address_as_ull, assumed;
    do {
        assumed = old;
        if (__longlong_as_double(assumed) <= val) break;
        old = atomicCAS(address_as_ull, assumed, __double_as_longlong(val));
    } while (assumed != old);
}

__device__ void atomicMaxDouble(double* address, double val) {
    unsigned long long int* address_as_ull = (unsigned long long int*)address;
    unsigned long long int old = *address_as_ull, assumed;
    do {
        assumed = old;
        if (__longlong_as_double(assumed) >= val) break;
        old = atomicCAS(address_as_ull, assumed, __double_as_longlong(val));
    } while (assumed != old);
}

// -----------------------------------------------------------------------------
// KERNEL 1: Calculate Sum, Min, and Max
// -----------------------------------------------------------------------------
__global__ void calc_stats_kernel(const double* scores, int n, double* total_sum, double* min_score, double* max_score) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    int stride = blockDim.x * gridDim.x;
    
    double local_sum = 0.0;
    double local_min = 1e9;
    double local_max = -1e9;

    for (int i = idx; i < n; i += stride) {
        double val = scores[i];
        local_sum += val;
        if (val < local_min) local_min = val;
        if (val > local_max) local_max = val;
    }

    if (local_sum > 0 || local_min != 1e9) {
        atomicAdd(total_sum, local_sum);
        atomicMinDouble(min_score, local_min);
        atomicMaxDouble(max_score, local_max);
    }
}

// -----------------------------------------------------------------------------
// KERNEL 2: Calculate Variance sum and Grade Distribution
// -----------------------------------------------------------------------------
__global__ void calc_variance_grades_kernel(const double* scores, int n, double mean, 
                                            double* var_sum, 
                                            int* grade_A, int* grade_B, int* grade_C, int* grade_D, int* grade_F) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    int stride = blockDim.x * gridDim.x;
    
    double local_var = 0.0;
    int loc_A = 0, loc_B = 0, loc_C = 0, loc_D = 0, loc_F = 0;

    for (int i = idx; i < n; i += stride) {
        double val = scores[i];
        double diff = val - mean;
        local_var += diff * diff;

        if (val >= 75.0) loc_A++;
        else if (val >= 65.0) loc_B++;
        else if (val >= 55.0) loc_C++;
        else if (val >= 45.0) loc_D++;
        else loc_F++;
    }

    if (loc_A > 0) atomicAdd(grade_A, loc_A);
    if (loc_B > 0) atomicAdd(grade_B, loc_B);
    if (loc_C > 0) atomicAdd(grade_C, loc_C);
    if (loc_D > 0) atomicAdd(grade_D, loc_D);
    if (loc_F > 0) atomicAdd(grade_F, loc_F);
    
    atomicAdd(var_sum, local_var);
}

// -----------------------------------------------------------------------------
// MAIN HOST FUNCTION
// -----------------------------------------------------------------------------
int main() {
    // 1. Read binary data from the file generated by Python
    ifstream file("scores.bin", ios::binary | ios::ate);
    if(!file.is_open()) {
        cerr << "Failed to open scores.bin!" << endl;
        return 1;
    }
    
    streamsize size = file.tellg();
    file.seekg(0, ios::beg);
    
    int n = size / sizeof(double);
    if (n == 0) {
        cout << "No scores found in the database. Exiting." << endl;
        return 0;
    }

    cout << "Allocating Unified Memory for " << n << " scores..." << endl;
    
    double *scores;
    CHECK_CUDA(cudaMallocManaged(&scores, n * sizeof(double)));

    // Read directly into unified memory
    if(!file.read(reinterpret_cast<char*>(scores), size)) {
        cerr << "Error reading binary data" << endl;
        return 1;
    }
    file.close();

    // Allocate memory for reduction results
    double *d_sum, *d_min, *d_max;
    CHECK_CUDA(cudaMallocManaged(&d_sum, sizeof(double)));
    CHECK_CUDA(cudaMallocManaged(&d_min, sizeof(double)));
    CHECK_CUDA(cudaMallocManaged(&d_max, sizeof(double)));
    
    *d_sum = 0.0;
    *d_min = 1e9;
    *d_max = -1e9;

    // Kernel launch parameters
    int blockSize = 256;
    int numBlocks = (n + blockSize - 1) / blockSize;
    if (numBlocks > 1024) numBlocks = 1024; // Cap blocks for grid-stride loops

    auto t_start = chrono::high_resolution_clock::now();

    // --------------------------------------------------
    // PHASE 1: Stats calculation
    // --------------------------------------------------
    calc_stats_kernel<<<numBlocks, blockSize>>>(scores, n, d_sum, d_min, d_max);
    CHECK_CUDA(cudaGetLastError());
    CHECK_CUDA(cudaDeviceSynchronize()); // Wait for GPU to finish

    double mean = (*d_sum) / n;

    // --------------------------------------------------
    // PHASE 2: Variance & Grades calculation
    // --------------------------------------------------
    double *d_var_sum;
    int *d_grade_A, *d_grade_B, *d_grade_C, *d_grade_D, *d_grade_F;
    CHECK_CUDA(cudaMallocManaged(&d_var_sum, sizeof(double)));
    CHECK_CUDA(cudaMallocManaged(&d_grade_A, sizeof(int)));
    CHECK_CUDA(cudaMallocManaged(&d_grade_B, sizeof(int)));
    CHECK_CUDA(cudaMallocManaged(&d_grade_C, sizeof(int)));
    CHECK_CUDA(cudaMallocManaged(&d_grade_D, sizeof(int)));
    CHECK_CUDA(cudaMallocManaged(&d_grade_F, sizeof(int)));

    *d_var_sum = 0.0;
    *d_grade_A = 0; *d_grade_B = 0; *d_grade_C = 0; *d_grade_D = 0; *d_grade_F = 0;

    calc_variance_grades_kernel<<<numBlocks, blockSize>>>(scores, n, mean, 
        d_var_sum, d_grade_A, d_grade_B, d_grade_C, d_grade_D, d_grade_F);
    CHECK_CUDA(cudaGetLastError());
    CHECK_CUDA(cudaDeviceSynchronize());

    double variance = (*d_var_sum) / n;
    double stddev = sqrt(variance);

    // --------------------------------------------------
    // PHASE 3: Sorting with Thrust for Median
    // --------------------------------------------------
    auto t_sort_start = chrono::high_resolution_clock::now();
    thrust::sort(thrust::device, scores, scores + n);
    CHECK_CUDA(cudaDeviceSynchronize());
    auto t_sort_end = chrono::high_resolution_clock::now();
    
    double median = (n % 2 == 0) 
        ? (scores[n / 2 - 1] + scores[n / 2]) / 2.0 
        : scores[n / 2];

    auto t_end = chrono::high_resolution_clock::now();
    
    // --------------------------------------------------
    // TIMING AND OUTPUT
    // --------------------------------------------------
    double total_ms = chrono::duration<double, milli>(t_end - t_start).count();
    double sort_ms = chrono::duration<double, milli>(t_sort_end - t_sort_start).count();

    cout << "\n==============================================" << endl;
    cout << "          CUDA SCORE ANALYSIS RESULTS         " << endl;
    cout << "==============================================" << endl;
    cout << "Database Scores  : " << n << endl;
    cout << "Sum              : " << *d_sum << endl;
    if (n > 0) {
        cout << "Min              : " << *d_min << endl;
        cout << "Max              : " << *d_max << endl;
    }
    cout << "Mean             : " << mean << endl;
    cout << "Median           : " << median << endl;
    cout << "Variance         : " << variance << endl;
    cout << "StdDev           : " << stddev << endl;
    
    cout << "\n--- Grades Distribution ---" << endl;
    cout << "Grade A (>= 75)  : " << *d_grade_A << endl;
    cout << "Grade B (>= 65)  : " << *d_grade_B << endl;
    cout << "Grade C (>= 55)  : " << *d_grade_C << endl;
    cout << "Grade D (>= 45)  : " << *d_grade_D << endl;
    cout << "Grade F (<  45)  : " << *d_grade_F << endl;

    cout << "\n--- Performance ---" << endl;
    cout << "Sort Time        : " << sort_ms << " ms" << endl;
    cout << "Total GPU Time   : " << total_ms << " ms" << endl;
    cout << "==============================================" << endl;

    // Free Unified Memory
    cudaFree(scores);
    cudaFree(d_sum); cudaFree(d_min); cudaFree(d_max);
    cudaFree(d_var_sum); 
    cudaFree(d_grade_A); cudaFree(d_grade_B); cudaFree(d_grade_C); cudaFree(d_grade_D); cudaFree(d_grade_F);

    return 0;
}
'''

# 1. Write the string to a C++ file
cuda_file_path = "score_analysis.cu"
with open(cuda_file_path, "w") as f:
    f.write(cuda_source_code)
print(f"[Python] Successfully wrote CUDA source code to '{cuda_file_path}'")

# 2. Compile the code using nvcc
executable_path = "./score_analysis"
print(f"[Python] Compiling '{cuda_file_path}' with nvcc...")
# Use arch=sm_75 for Colab T4 GPU to ensure double-precision atomicAdd is supported
compile_command = f"nvcc -O3 -arch=sm_75 {cuda_file_path} -o {executable_path}"

try:
    result = subprocess.run(compile_command, shell=True, check=True, capture_output=True, text=True)
    if result.stdout:
        print(result.stdout)
    if result.stderr:
        print(result.stderr)
    print(f"[Python] Compilation successful! Executable created: '{executable_path}'\n")
    
    # 3. Execute the compiled program
    print(f"[Python] Running '{executable_path}'...\n")
    run_process = subprocess.Popen(
        executable_path,
        shell=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        bufsize=1,
    )

    while True:
        line = run_process.stdout.readline()
        if line:
            print(line, end="")
        elif run_process.poll() is not None:
            break

    return_code = run_process.wait()
    if return_code != 0:
        print(f"\n[Error] CUDA executable failed with return code {return_code}")
except subprocess.CalledProcessError as e:
    print(f"\n[Python Error] Command failed with exit code {e.returncode}.")
    print("--- COMPILER ERROR OUTPUT ---")
    print(e.stderr)
    print(e.stdout)
    print("-----------------------------")
    print("Ensure you are running this in an environment with the CUDA toolkit installed (e.g., Google Colab with GPU).")
