/* Copyright 2024 The OpenXLA Authors.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
==============================================================================*/
// TODO(ROCm): Enable and include ROCm Triton passes when ROCm Triton is
// included in build.
#include "third_party/amd/include/TritonAMDGPUToLLVM/Passes.h"
#include "third_party/amd/include/TritonAMDGPUTransforms/Passes.h"
#include "mlir/Conversion/ArithToLLVM/ArithToLLVM.h"
#include "mlir/Conversion/ControlFlowToLLVM/ControlFlowToLLVM.h"
#include "mlir/Conversion/IndexToLLVM/IndexToLLVM.h"
#include "mlir/Conversion/SCFToControlFlow/SCFToControlFlow.h"
#include "mlir/Pass/PassManager.h"
#include "mlir/Transforms/Passes.h"
#include "xla/service/gpu/llvm_gpu_backend/gpu_backend_lib.h"
#include "xla/service/gpu/matmul_utils.h"
#include "xla/service/gpu/model/tiled_hlo_computation.h"
#include "xla/service/hlo_module_config.h"
#include "tsl/platform/rocm_rocdl_path.h"
#include "triton/Conversion/TritonGPUToLLVM/Passes.h"
#include "triton/Conversion/TritonToTritonGPU/TritonToTritonGPUPass.h"
#include "triton/Dialect/Triton/Transforms/Passes.h"
#include "triton/Dialect/TritonGPU/Transforms/Passes.h"
#include "triton/Dialect/TritonNvidiaGPU/Transforms/Passes.h"

namespace xla {
namespace gpu {

// Value 0 for num_stages is used to represent AMD specific register
// file double buffering.
constexpr int kAmdDoubleBuffering = 0;

namespace ma = ::mlir::arith;
namespace mm = ::mlir::math;
namespace ml = ::mlir::LLVM;
namespace mt = ::mlir::triton;

using ::llvm::SmallVector;
using mlir::ArrayRef;
using ::mlir::ShapedType;
using ::mlir::Type;
using ::mlir::Value;
using mlir::ValueRange;

absl::Status CreateTritonPipeline(
    mlir::OpPassManager& pm, const se::DeviceDescription& device_info,
    const BlockLevelParameters& block_level_parameters,
    mt::nvidia_gpu::ClusterInfo& out_cluster_info) {
  // TODO(ROCm): Check why some test fail when threadsPerWarp is set to 64.
  const int threadsPerWarp = device_info.threads_per_warp();
  auto ccRocm = device_info.rocm_compute_capability();

  // Based on make_ttir() in
  // @triton//:third_party/amd/backend/compiler.py
  pm.addPass(mlir::createInlinerPass());
  pm.addPass(mt::createRewriteTensorPointerPass());
  pm.addPass(mt::createCombineOpsPass());
  pm.addPass(mlir::createCanonicalizerPass());
  pm.addPass(mt::createReorderBroadcastPass());
  pm.addPass(mlir::createCSEPass());
  pm.addPass(mlir::createLoopInvariantCodeMotionPass());
  pm.addPass(mlir::createSymbolDCEPass());

  // Based on make_ttgir() in
  // @triton//:third_party/amd/backend/compiler.py
  pm.addPass(mt::createConvertTritonToTritonGPUPass(
      absl::StrCat("hip:", ccRocm.gfx_version()),
      block_level_parameters.num_warps, threadsPerWarp,
      block_level_parameters.num_ctas));
  pm.addPass(mt::gpu::createTritonGPUCoalesce());
  pm.addPass(mt::gpu::createTritonGPURemoveLayoutConversions());
  pm.addPass(mt::gpu::createTritonGPUOptimizeThreadLocality());
  pm.addPass(mlir::createTritonAMDGPUAccelerateMatmulPass());
  pm.addPass(mt::gpu::createTritonGPURemoveLayoutConversions());
  // TODO ROCm Check if we want to compare MI100 and greater
  pm.addPass(mlir::createTritonAMDGPUOptimizeEpiloguePass());
  pm.addPass(mt::gpu::createTritonGPUOptimizeDotOperands({true}));
  if (block_level_parameters.num_stages == kAmdDoubleBuffering &&
      ccRocm.has_amd_matrix_core()) {
    pm.addPass(mlir::createTritonAMDGPUStreamPipelinePass());
    pm.addPass(mlir::createCanonicalizerPass());
  }
  pm.addPass(mt::gpu::createTritonGPUOptimizeDotOperands({true}));
  pm.addPass(mt::gpu::createTritonGPURemoveLayoutConversions());
  pm.addPass(mt::gpu::createTritonGPUReduceDataDuplication());
  if (block_level_parameters.num_stages != kAmdDoubleBuffering) {
    pm.addPass(mt::gpu::createTritonGPUReorderInstructions());
  }
  pm.addPass(mlir::createTritonAMDGPUCanonicalizePointersPass());
  pm.addPass(mlir::createCanonicalizerPass());
  pm.addPass(mlir::createCSEPass());
  pm.addPass(mlir::createSymbolDCEPass());

  // Based on make_llir() in
  // @triton//:third_party/amd/backend/compiler.py
  pm.addPass(mlir::triton::AMD::createDecomposeUnsupportedConversionsPass(
      ccRocm.gfx_version()));
  const int custom_lds_size = 0;
  pm.addPass(mlir::triton::AMD::createOptimizeLDSUsagePass(ccRocm.gfx_version(),
                                                           custom_lds_size));
  pm.addPass(mlir::createConvertSCFToCFPass());
  pm.addPass(mlir::createConvertIndexToLLVMPass());
  pm.addPass(mt::gpu::createAllocateSharedMemoryPass());
  pm.addPass(
      mt::createConvertTritonAMDGPUToLLVMPass(ccRocm.gfx_version(), true));
  pm.addPass(mlir::createCanonicalizerPass());
  pm.addPass(mlir::createCSEPass());
  // Note: translateTritonGPUToLLVMIR adds line info with LLVMDIScopePass.
  pm.addPass(mlir::createConvertControlFlowToLLVMPass());
  pm.addPass(mlir::createArithToLLVMConversionPass());
  pm.addPass(mlir::createCanonicalizerPass());
  pm.addPass(mlir::createCSEPass());
  pm.addPass(mlir::createSymbolDCEPass());
  pm.addPass(mt::createConvertBuiltinFuncToLLVMPass());
  // There is no clusters in ROCm for now.
  out_cluster_info.clusterDimX = 1;
  out_cluster_info.clusterDimY = 1;
  out_cluster_info.clusterDimZ = 1;

  return absl::OkStatus();
}

std::string GetLibdevicePath(const HloModuleConfig& hlo_config,
                             const se::DeviceDescription& device_info) {
  std::string libdevice_dir = tsl::RocdlRoot();
  auto compute_capability = device_info.rocm_compute_capability();
  const std::string libdevice_path =
      amdgpu::LibDevicePath(compute_capability.gcn_arch_name(), libdevice_dir);
  return libdevice_path;
}

}  // namespace gpu
}  // namespace xla
