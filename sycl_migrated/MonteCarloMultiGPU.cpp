/* Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *  * Neither the name of NVIDIA CORPORATION nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * This sample evaluates fair call price for a
 * given set of European options using Monte Carlo approach.
 * See supplied whitepaper for more explanations.
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <CL/sycl.hpp>
#include <multithreading.h>
using namespace sycl;
#include <chrono>

#include "MonteCarlo_common.h"

int *pArgc = NULL;
char **pArgv = NULL;

#ifdef WIN32
#define strcasecmp _strcmpi
#endif

////////////////////////////////////////////////////////////////////////////////
// Common functions
////////////////////////////////////////////////////////////////////////////////
float randFloat(float low, float high) {
  float t = (float)rand() / (float)RAND_MAX;
  return (1.0f - t) * low + t * high;
}

/// Utility function to tweak problem size for small GPUs
int adjustProblemSize(int GPU_N, int default_nOptions) {
  int nOptions = default_nOptions;

  // select problem size
  for (int i = 0; i < GPU_N; i++) {
    sycl::queue q_ct1 = sycl::queue(gpu_selector{});
    auto device = q_ct1.get_device();

    int cudaCores =
        device.get_info<cl::sycl::info::device::max_compute_units>();

    if (cudaCores <= 32) {
      nOptions = (nOptions < cudaCores / 2 ? nOptions : cudaCores / 2);
    }
  }

  return nOptions;
}

int adjustGridSize(int GPUIndex, int defaultGridSize) {
  sycl::queue q_ct1 = sycl::queue(gpu_selector{});
  auto device = q_ct1.get_device();

  int maxGridSize =
      device.get_info<cl::sycl::info::device::max_compute_units>() * 40;

  return ((defaultGridSize > maxGridSize) ? maxGridSize : defaultGridSize);
}

///////////////////////////////////////////////////////////////////////////////
// CPU reference functions
///////////////////////////////////////////////////////////////////////////////
extern "C" void MonteCarloCPU(TOptionValue &callValue, TOptionData optionData,
                              float *h_Random, int pathN);

// Black-Scholes formula for call options
extern "C" void BlackScholesCall(float &CallResult, TOptionData optionData);

////////////////////////////////////////////////////////////////////////////////
// GPU-driving host thread
////////////////////////////////////////////////////////////////////////////////
// Timer
StopWatchInterface **hTimer = NULL;

static CUT_THREADPROC solverThread(TOptionPlan *plan) {
  // Start the timer
  sdkStartTimer(&hTimer[plan->device]);
  sycl::queue stream = sycl::queue(
      (sycl::platform(sycl::gpu_selector())
           .get_devices(sycl::info::device_type::gpu)[plan->device]));

  initMonteCarloGPU(plan, &stream);

  // Main computation
  MonteCarloGPU(plan, &stream);

  stream.wait_and_throw();
  // Stop the timer
  sdkStopTimer(&hTimer[plan->device]);

  // Shut down this GPU
  closeMonteCarloGPU(plan, &stream);

  stream.wait();

  CUT_THREADEND;
}

static void multiSolver(TOptionPlan *plan, int nPlans) {
  // allocate and initialize an array of stream handles
  sycl::queue *streams = (sycl::queue *)malloc(nPlans * sizeof(sycl::queue));
  sycl::event *events = new sycl::event[nPlans];
  std::chrono::time_point<std::chrono::steady_clock> events_ct1_i;

  auto gpu_devices = sycl::platform(sycl::gpu_selector())
                         .get_devices(sycl::info::device_type::gpu);

  for (int i = 0; i < nPlans; i++) {
    streams[i] =
        sycl::queue(gpu_devices[plan[i].device], property::queue::in_order());
  }

  for (int i = 0; i < nPlans; i++) {
    initMonteCarloGPU(&plan[i], &streams[i]);
  }

  for (int i = 0; i < nPlans; i++) {
    streams[i].wait_and_throw();
  }

  for (int i = 0; i < nPlans; i++) {
    // Main computations

    MonteCarloGPU(&plan[i], &streams[i]);

    events_ct1_i = std::chrono::steady_clock::now();
    events[i] = streams[i].ext_oneapi_submit_barrier();
  }

  for (int i = 0; i < nPlans; i++) {
    events[i].wait_and_throw();
  }

  // Stop the timer
  sdkStopTimer(&hTimer[0]);
  for (int i = 0; i < nPlans; i++) {
    closeMonteCarloGPU(&plan[i], &streams[i]);
  }
}

///////////////////////////////////////////////////////////////////////////////
// Main program
///////////////////////////////////////////////////////////////////////////////
#define DO_CPU
#undef DO_CPU

#define PRINT_RESULTS
#undef PRINT_RESULTS

void usage() {
  printf("--method=[threaded,streamed] --scaling=[strong,weak] [--help]\n");
  printf("Method=threaded: 1 CPU thread for each GPU     [default]\n");
  printf(
      "       streamed: 1 CPU thread handles all GPUs (requires CUDA 4.0 or "
      "newer)\n");
  printf("Scaling=strong : constant problem size\n");
  printf(
      "        weak   : problem size scales with number of available GPUs "
      "[default]\n");
}

int main(int argc, char **argv) {
  char *multiMethodChoice = NULL;
  char *scalingChoice = NULL;
  bool use_threads = true;
  bool bqatest = false;
  bool strongScaling = false;

  pArgc = &argc;
  pArgv = argv;

  printf("%s Starting...\n\n", argv[0]);

  if (checkCmdLineFlag(argc, (const char **)argv, "qatest")) {
    bqatest = true;
  }

  getCmdLineArgumentString(argc, (const char **)argv, "method",
                           &multiMethodChoice);
  getCmdLineArgumentString(argc, (const char **)argv, "scaling",
                           &scalingChoice);

  if (checkCmdLineFlag(argc, (const char **)argv, "h") ||
      checkCmdLineFlag(argc, (const char **)argv, "help")) {
    usage();
    exit(EXIT_SUCCESS);
  }

  if (multiMethodChoice == NULL) {
    use_threads = false;
  } else {
    if (!strcasecmp(multiMethodChoice, "threaded")) {
      use_threads = true;
    } else {
      use_threads = false;
    }
  }

  if (use_threads == false) {
    printf("Using single CPU thread for multiple GPUs\n");
  }

  if (scalingChoice == NULL) {
    strongScaling = false;
  } else {
    if (!strcasecmp(scalingChoice, "strong")) {
      strongScaling = true;
    } else {
      strongScaling = false;
    }
  }

  // GPU number present in the system
  int GPU_N;

  GPU_N =
      (cl::sycl::device::get_devices(cl::sycl::info::device_type::all)).size();
  int nOptions = 8 * 1024;
  nOptions = adjustProblemSize(GPU_N, nOptions);

  // select problem size
  int scale = (strongScaling) ? 1 : GPU_N;
  int OPT_N = nOptions * scale;
  int PATH_N = 262144;

  // initialize the timers
  hTimer = new StopWatchInterface *[GPU_N];

  for (int i = 0; i < GPU_N; i++) {
    sdkCreateTimer(&hTimer[i]);
    sdkResetTimer(&hTimer[i]);
  }

  // Input data array
  TOptionData *optionData = new TOptionData[OPT_N];
  // Final GPU MC results
  TOptionValue *callValueGPU = new TOptionValue[OPT_N];
  //"Theoretical" call values by Black-Scholes formula
  float *callValueBS = new float[OPT_N];
  // Solver config
  TOptionPlan *optionSolver = new TOptionPlan[GPU_N];
  // OS thread ID
  CUTThread *threadID = new CUTThread[GPU_N];

  int gpuBase, gpuIndex;
  int i;

  float time;

  double delta, ref, sumDelta, sumRef, sumReserve;

  printf("MonteCarloMultiGPU\n");
  printf("==================\n");
  printf("Parallelization method  = %s\n",
         use_threads ? "threaded" : "streamed");
  printf("Problem scaling         = %s\n", strongScaling ? "strong" : "weak");
  printf("Number of GPUs          = %d\n", GPU_N);
  printf("Total number of options = %d\n", OPT_N);
  printf("Number of paths         = %d\n", PATH_N);

  printf("main(): generating input data...\n");
  srand(123);

  for (i = 0; i < OPT_N; i++) {
    optionData[i].S = randFloat(5.0f, 50.0f);
    optionData[i].X = randFloat(10.0f, 25.0f);
    optionData[i].T = randFloat(1.0f, 5.0f);
    optionData[i].R = 0.06f;
    optionData[i].V = 0.10f;
    callValueGPU[i].Expected = -1.0f;
    callValueGPU[i].Confidence = -1.0f;
  }

  printf("main(): starting %i host threads...\n", GPU_N);

  // Get option count for each GPU
  for (i = 0; i < GPU_N; i++) {
    optionSolver[i].optionCount = OPT_N / GPU_N;
  }

  // Take into account cases with "odd" option counts
  for (i = 0; i < (OPT_N % GPU_N); i++) {
    optionSolver[i].optionCount++;
  }

  // Assign GPU option ranges
  gpuBase = 0;

  for (i = 0; i < GPU_N; i++) {
    optionSolver[i].device = i;
    optionSolver[i].optionData = optionData + gpuBase;
    optionSolver[i].callValue = callValueGPU + gpuBase;
    optionSolver[i].pathN = PATH_N;
    optionSolver[i].gridSize =
        adjustGridSize(optionSolver[i].device, optionSolver[i].optionCount);
    gpuBase += optionSolver[i].optionCount;
  }

  if (use_threads || bqatest) {
    // Start CPU thread for each GPU
    for (gpuIndex = 0; gpuIndex < GPU_N; gpuIndex++) {
      threadID[gpuIndex] = cutStartThread((CUT_THREADROUTINE)solverThread,
                                          &optionSolver[gpuIndex]);
    }

    printf("main(): waiting for GPU results...\n");
    cutWaitForThreads(threadID, GPU_N);

    printf("main(): GPU statistics, threaded\n");

    for (i = 0; i < GPU_N; i++) {
      sycl::queue q_ct1 = sycl::queue();
      printf("GPU Device #%i: ", optionSolver[i].device);
      std::cout << "\nRunning on "
                << q_ct1.get_device().get_info<sycl::info::device::name>()
                << "\n";
      printf("Options         : %i\n", optionSolver[i].optionCount);
      printf("Simulation paths: %i\n", optionSolver[i].pathN);
      time = sdkGetTimerValue(&hTimer[i]);
      printf("Total time (ms.): %f\n", time);
      printf("Options per sec.: %f\n", OPT_N / (time * 0.001));
    }

    printf("main(): comparing Monte Carlo and Black-Scholes results...\n");
    sumDelta = 0;
    sumRef = 0;
    sumReserve = 0;

    for (i = 0; i < OPT_N; i++) {
      BlackScholesCall(callValueBS[i], optionData[i]);
      delta = fabs(callValueBS[i] - callValueGPU[i].Expected);
      ref = callValueBS[i];
      sumDelta += delta;
      sumRef += fabs(ref);

      if (delta > 1e-6) {
        sumReserve += callValueGPU[i].Confidence / delta;
      }

#ifdef PRINT_RESULTS
      printf("BS: %f; delta: %E\n", callValueBS[i], delta);
#endif
    }

    sumReserve /= OPT_N;
  }

  if (!use_threads || bqatest) {
    multiSolver(optionSolver, GPU_N);

    printf("main(): GPU statistics, streamed\n");

    for (i = 0; i < GPU_N; i++) {
      sycl::queue q_ct1 = sycl::queue();
      printf("GPU Device #%i: ", optionSolver[i].device);
      std::cout << q_ct1.get_device().get_info<sycl::info::device::name>()
                << "\n";
      printf("Options         : %i\n", optionSolver[i].optionCount);
      printf("Simulation paths: %i\n", optionSolver[i].pathN);
    }

    time = sdkGetTimerValue(&hTimer[0]);
    printf("\nTotal time (ms.): %f\n", time);
    printf("\tNote: This is elapsed time for all to compute.\n");
    printf("Options per sec.: %f\n", OPT_N / (time * 0.001));

    printf("main(): comparing Monte Carlo and Black-Scholes results...\n");
    sumDelta = 0;
    sumRef = 0;
    sumReserve = 0;

    for (i = 0; i < OPT_N; i++) {
      BlackScholesCall(callValueBS[i], optionData[i]);
      delta = fabs(callValueBS[i] - callValueGPU[i].Expected);
      ref = callValueBS[i];
      sumDelta += delta;
      sumRef += fabs(ref);

      if (delta > 1e-6) {
        sumReserve += callValueGPU[i].Confidence / delta;
      }

#ifdef PRINT_RESULTS
      printf("BS: %f; delta: %E\n", callValueBS[i], delta);
#endif
    }

    sumReserve /= OPT_N;
  }

#ifdef DO_CPU
  printf("main(): running CPU MonteCarlo...\n");
  TOptionValue callValueCPU;
  sumDelta = 0;
  sumRef = 0;

  for (i = 0; i < OPT_N; i++) {
    MonteCarloCPU(callValueCPU, optionData[i], NULL, PATH_N);
    delta = fabs(callValueCPU.Expected - callValueGPU[i].Expected);
    ref = callValueCPU.Expected;
    sumDelta += delta;
    sumRef += fabs(ref);
    printf("Exp : %f | %f\t", callValueCPU.Expected, callValueGPU[i].Expected);
    printf("Conf: %f | %f\n", callValueCPU.Confidence,
           callValueGPU[i].Confidence);
  }

  printf("L1 norm: %E\n", sumDelta / sumRef);
#endif

  printf("Shutting down...\n");

  for (int i = 0; i < GPU_N; i++) {
    sdkStartTimer(&hTimer[i]);
  }

  delete[] optionSolver;
  delete[] callValueBS;
  delete[] callValueGPU;
  delete[] optionData;
  delete[] threadID;
  delete[] hTimer;

  printf("Test Summary...\n");
  printf("L1 norm        : %E\n", sumDelta / sumRef);
  printf("Average reserve: %f\n", sumReserve);
  printf(sumReserve > 1.0f ? "Test passed\n" : "Test failed!\n");
  exit(sumReserve > 1.0f ? EXIT_SUCCESS : EXIT_FAILURE);
}
