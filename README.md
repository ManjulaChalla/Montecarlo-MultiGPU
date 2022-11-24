﻿# `MonteCarloMultiGPU` Sample

Monte Carlo method is basically a way to compute expected values by generating random scenarios and then averaging them, it is actually very efficient to parallelize. With the GPU we can reduce this problem by parallelizing the paths. That is, we can assign each path to a single thread, simulating thousands of them in parallel, with massive savings in computational power and time.

> **Note**: This sample is migrated from NVIDIA CUDA sample. See the [MonteCarloMultiGPU](https://github.com/NVIDIA/cuda-samples/tree/master/Samples/5_Domain_Specific/MonteCarloMultiGPU) sample in the NVIDIA/cuda-samples GitHub.

| Property                       | Description
|:---                               |:---
| What you will learn               | How to begin migrating CUDA to SYCL*
| Time to complete                  | 15 minutes


## Purpose

This sample contains four versions:

| Folder Name                          | Description
|:---                                  |:---
| 01_sycl_dpct_output                  | Contains output of Intel® DPC++ Compatibility Tool used to migrate SYCL-compliant code from CUDA code, this SYCL code has some unmigrated code which has to be manually fixed to get full functionality, the code does not functionally work.
| 02_sycl_dpct_migrated                | Contains Intel® DPC++ Compatibility Tool migrated SYCL code from CUDA code with manual changes done to fix the unmigrated code to work functionally.
| 03_sycl_migrated                     | Contains manually migrated SYCL code from CUDA code.
| 04_sycl_migrated_optimized           | Contains manually migrated SYCL code from CUDA code with performance optimizations applied.

## Prerequisites
| Property                       | Description
|:---                               |:---
| OS                                | Ubuntu* 20.04
| Hardware                          | Skylake with GEN9 or newer
| Software                          | Intel&reg; oneAPI DPC++/C++ Compiler

## Key Implementation Details

This sample application demonstrates the CUDA MonteCarloMultiGPU using key concepts such as Random Number Generator and Computational Finance.


## Build the `MonteCarloMultiGPU` Sample for CPU and GPU

When working with the command-line interface (CLI), you should configure the oneAPI toolkits using environment variables. Set up your CLI environment by sourcing the `setvars` script every time you open a new terminal window. This practice ensures that your compiler, libraries, and tools are ready for development.

> **Note**: If you have not already done so, set up your CLI
> environment by sourcing  the `setvars` script located in
> the root of your oneAPI installation.
>
> Linux*:
> - For system wide installations: `. /opt/intel/oneapi/setvars.sh`
> - For private installations: `. ~/intel/oneapi/setvars.sh`
> - For non-POSIX shells, like csh, use the following command: `$ bash -c 'source <install-dir>/setvars.sh ; exec csh'`
>
>For more information on environment variables, see [Use the setvars Script with Linux* or macOS*](https://www.intel.com/content/www/us/en/develop/documentation/oneapi-programming-guide/top/oneapi-development-environment-setup/use-the-setvars-script-with-linux-or-macos.html), or [Windows](https://www.intel.com/content/www/us/en/develop/documentation/oneapi-programming-guide/top/oneapi-development-environment-setup/use-the-setvars-script-with-windows.html).

### On Linux*
Perform the following steps:
1. Change to the `MonteCarloMultiGPU` directory.
2. Build the program.
   ```
   $ mkdir build
   $ cd build
   $ cmake ..
   $ make
   ```

   By default, these commands build the `02_sycl_dpct_migrated`, `03_sycl_migrated` and `04_sycl_migrated_optimized` versions of the program.

If an error occurs, you can get more details by running `make` with the `VERBOSE=1` argument:
```
make VERBOSE=1
```

#### Troubleshooting
If you receive an error message, troubleshoot the problem using the Diagnostics Utility for Intel&reg; oneAPI Toolkits. The diagnostic utility provides configuration and system checks to help find missing dependencies, permissions errors and other issues. See [Diagnostics Utility for Intel&reg; oneAPI Toolkits User Guide](https://www.intel.com/content/www/us/en/develop/documentation/diagnostic-utility-user-guide/top.html).

## Run the `MonteCarloMultiGPU` Sample
In all cases, you can run the programs for CPU and GPU. The run commands indicate the device target.
1. Run `02_sycl_dpct_migrated` for CPU and GPU.
    ```
    make run_sdm_cpu
    make run_sdm_gpu
    ```
    
2. Run `03_sycl_migrated` for CPU and GPU.
    ```
    make run_cpu
    make run_gpu
    ```
    
3. Run `04_sycl_migrated_optimized` for CPU and GPU.
    ```
    make run_smo_cpu
    make run_smo_gpu
    ```

### Run the `MonteCarloMultiGPU` Sample in Intel&reg; DevCloud

When running a sample in the Intel&reg; DevCloud, you must specify the compute node (CPU, GPU, FPGA) and whether to run in batch or interactive mode. For more information, see the Intel&reg; oneAPI Base Toolkit [Get Started Guide](https://devcloud.intel.com/oneapi/get_started/).

#### Build and Run Samples in Batch Mode (Optional)

You can submit build and run jobs through a Portable Bash Script (PBS). A job is a script that submitted to PBS through the `qsub` utility. By default, the `qsub` utility does not inherit the current environment variables or your current working directory, so you might need to submit jobs to configure the environment variables. To indicate the correct working directory, you can use either absolute paths or pass the `-d \<dir\>` option to `qsub`.

1. Open a terminal on a Linux* system.
2. Log in to Intel&reg; DevCloud.
    ```
    ssh devcloud
    ```
3. Download the samples.
    ```
    git clone https://github.com/oneapi-src/oneAPI-samples.git
    ```
4. Change to the `MontecarloMultiGPU` directory.
    ```
    cd ~/oneAPI-samples/DirectProgramming/DPC++/MapReduce/MonteCarloMultiGPU
    ```
5. Configure the sample for a GPU node using `qsub`.
    ```
    qsub  -I  -l nodes=1:gpu:ppn=2 -d .
    ```
    - `-I` (upper case I) requests an interactive session.
    - `-l nodes=1:gpu:ppn=2` (lower case L) assigns one full GPU node.
    - `-d .` makes the current folder as the working directory for the task.
6. Perform build steps as you would on Linux.
7. Run the sample.
8. Clean up the project files.
    ```
    make clean
    ```
9. Disconnect from the Intel&reg; DevCloud.
    ```
    exit
    ```

### Example Output
The following example is for `03_sycl_migrated` for GPU on Intel(R) UHD Graphics P630 [0x3e96] with Level Zero Backend.
```
./a.out Starting...

Using single CPU thread for multiple GPUs
MonteCarloMultiGPU
==================
Parallelization method  = streamed
Problem scaling         = weak
Number of GPUs          = 1
Total number of options = 16
Number of paths         = 262144
main(): generating input data...
main(): starting 1 host threads...
main(): GPU statistics, streamed
GPU Device #0: Intel(R) UHD Graphics [0x9a60]
Options         : 16
Simulation paths: 262144

Total time (ms.): 6.054000
        Note: This is elapsed time for all to compute.
Options per sec.: 2642.880783
main(): comparing Monte Carlo and Black-Scholes results...
Shutting down...
Test Summary...
L1 norm        : 4.815522E-04
Average reserve: 5.051839
Test passed

```

## License
Code samples are licensed under the MIT license. See
[License.txt](https://github.com/oneapi-src/oneAPI-samples/blob/master/License.txt) for details.

Third party program licenses are at [third-party-programs.txt](https://github.com/oneapi-src/oneAPI-samples/blob/master/third-party-programs.txt).
