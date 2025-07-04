/* Copyright 2023 The OpenXLA Authors.

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

#include "xla/service/gpu/fusions/triton/triton_fusion_emitter.h"

#include <algorithm>
#include <array>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <queue>
#include <string>
#include <system_error>  // NOLINT(build/c++11): required to interface with LLVM
#include <utility>
#include <variant>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/cord.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/Linker/Linker.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/Triple.h"
#include "mlir/Conversion/AffineToStandard/AffineToStandard.h"
#include "mlir/Conversion/ArithToLLVM/ArithToLLVM.h"
#include "mlir/Conversion/ControlFlowToLLVM/ControlFlowToLLVM.h"
#include "mlir/Conversion/IndexToLLVM/IndexToLLVM.h"
#include "mlir/Conversion/SCFToControlFlow/SCFToControlFlow.h"
#include "mlir/Dialect/Affine/IR/AffineOps.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/Func/Extensions/InlinerExtension.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/Dialect/LLVMIR/LLVMTypes.h"
#include "mlir/Dialect/LLVMIR/NVVMDialect.h"
#include "mlir/Dialect/LLVMIR/Transforms/InlinerInterfaceImpl.h"
#include "mlir/Dialect/Math/IR/Math.h"
#include "mlir/Dialect/SCF/IR/SCF.h"
#include "mlir/ExecutionEngine/OptUtils.h"
#include "mlir/IR/AffineExpr.h"
#include "mlir/IR/Attributes.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinAttributes.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/BuiltinTypeInterfaces.h"
#include "mlir/IR/BuiltinTypes.h"
#include "mlir/IR/DialectRegistry.h"
#include "mlir/IR/ImplicitLocOpBuilder.h"
#include "mlir/IR/Location.h"
#include "mlir/IR/MLIRContext.h"
#include "mlir/IR/OwningOpRef.h"
#include "mlir/IR/TypeUtilities.h"
#include "mlir/IR/Types.h"
#include "mlir/IR/Value.h"
#include "mlir/IR/ValueRange.h"
#include "mlir/IR/Verifier.h"
#include "mlir/Pass/Pass.h"
#include "mlir/Pass/PassManager.h"
#include "mlir/Support/LLVM.h"
#include "mlir/Support/LogicalResult.h"
#include "mlir/Target/LLVMIR/Dialect/Builtin/BuiltinToLLVMIRTranslation.h"
#include "mlir/Target/LLVMIR/Dialect/LLVMIR/LLVMToLLVMIRTranslation.h"
#include "mlir/Target/LLVMIR/Dialect/NVVM/NVVMToLLVMIRTranslation.h"
#include "mlir/Target/LLVMIR/Dialect/ROCDL/ROCDLToLLVMIRTranslation.h"
#include "mlir/Target/LLVMIR/Export.h"
#include "mlir/Transforms/Passes.h"
#include "xla/autotuning.pb.h"
#include "xla/comparison_util.h"
#include "xla/hlo/ir/hlo_casting_utils.h"
#include "xla/hlo/ir/hlo_computation.h"
#include "xla/hlo/ir/hlo_instruction.h"
#include "xla/hlo/ir/hlo_instructions.h"
#include "xla/hlo/ir/hlo_opcode.h"
#include "xla/hlo/translate/hlo_to_mhlo/hlo_function_importer.h"
#include "xla/hlo/utils/hlo_query.h"
#include "xla/layout_util.h"
#include "xla/literal.h"
#include "xla/mlir_hlo/mhlo/IR/hlo_ops.h"
#include "xla/mlir_hlo/mhlo/transforms/map_mhlo_to_scalar_op.h"
#include "xla/permutation_util.h"
#include "xla/primitive_util.h"
#include "xla/service/algorithm_util.h"
#include "xla/service/dump.h"
#include "xla/service/gpu/backend_configs.pb.h"
#include "xla/service/gpu/fusions/ir/xla_gpu_ops.h"
#include "xla/service/gpu/fusions/mlir/elemental_hlo_to_mlir.h"
#include "xla/service/gpu/fusions/transforms/passes.h"
#include "xla/service/gpu/fusions/triton/passes.h"
#include "xla/service/gpu/hlo_traversal.h"
#include "xla/service/gpu/ir_emission_utils.h"
#include "xla/service/gpu/launch_dimensions.h"
#include "xla/service/gpu/llvm_gpu_backend/gpu_backend_lib.h"
#include "xla/service/gpu/matmul_utils.h"
#include "xla/service/gpu/model/indexing_analysis.h"
#include "xla/service/gpu/model/indexing_map.h"
#include "xla/service/gpu/model/symbolic_tile_analysis.h"
#include "xla/service/gpu/model/tiled_hlo_computation.h"
#include "xla/service/gpu/model/tiled_hlo_instruction.h"
#include "xla/service/gpu/model/triton_emitter_constraints.h"
#include "xla/service/gpu/target_util.h"
#include "xla/service/gpu/triton_fusion_analysis.h"
#include "xla/service/gpu/triton_tiling_propagation.h"
#include "xla/service/hlo_module_config.h"
#include "xla/service/instruction_fusion.h"
#include "xla/service/llvm_ir/llvm_util.h"
#include "xla/shape_util.h"
#include "xla/status_macros.h"
#include "xla/stream_executor/device_description.h"
#include "xla/stream_executor/launch_dim.h"
#include "xla/tools/hlo_decomposer.h"
#include "xla/util.h"
#include "xla/xla.pb.h"
#include "xla/xla_data.pb.h"
#include "tsl/platform/errors.h"
#include "tsl/platform/path.h"
#include "tsl/platform/status.h"
#include "tsl/platform/statusor.h"
#include "tsl/platform/tensor_float_32_utils.h"
#include "triton/Conversion/TritonGPUToLLVM/Passes.h"
#include "triton/Conversion/TritonToTritonGPU/TritonToTritonGPUPass.h"
#include "triton/Dialect/Triton/IR/Dialect.h"
#include "triton/Dialect/Triton/IR/Types.h"
#include "triton/Dialect/TritonGPU/IR/Dialect.h"
#include "triton/Dialect/TritonNvidiaGPU/Transforms/Passes.h"

namespace xla {
namespace gpu {

namespace ma = ::mlir::arith;
namespace mm = ::mlir::math;
namespace mn = ::mlir::NVVM;
namespace mt = ::mlir::triton;

using ::llvm::SmallVector;
using mlir::ArrayRef;
using mlir::ImplicitLocOpBuilder;
using ::mlir::ShapedType;
using ::mlir::Type;
using ::mlir::Value;
using mlir::ValueRange;

namespace {

// Triton requires that all block dimensions are a power of 2.
// TODO(b/353484968): Delete this function once we have constraints to only
// propagate tile sizes that are a power of 2.
llvm::SmallVector<int64_t> GetPaddedTileSizes(ArrayRef<int64_t> tile_sizes) {
  llvm::SmallVector<int64_t> result;
  result.reserve(tile_sizes.size());
  for (int64_t value : tile_sizes) {
    result.push_back(llvm::PowerOf2Ceil(value));
  }
  return result;
}

// XLA -> Triton type conversions.
absl::StatusOr<Type> TritonType(mlir::OpBuilder b, PrimitiveType t) {
  switch (t) {
    case F64:
      return b.getF64Type();
    case F32:
      return b.getF32Type();
    case F16:
      return b.getF16Type();
    case BF16:
      return b.getBF16Type();
    case S64:
      return b.getI64Type();
    case S32:
      return b.getI32Type();
    case S16:
      return b.getI16Type();
    case PRED:
      return b.getI1Type();
    case S8:
      return b.getI8Type();
    case S4:  // The unpacking to i8 is supported by the emitter.
      // We pass the s4 tensor as i8 tensor with the minor dimension having 2x
      // less elements and unpack in the inner loop of the triton kernel.
      return b.getI8Type();
    case F8E5M2:
      return b.getFloat8E5M2Type();
    case F8E4M3FN:
      return b.getFloat8E4M3FNType();
    default:
      return absl::UnimplementedError(
          absl::StrCat("This type is not supported yet: ",
                       primitive_util::LowercasePrimitiveTypeName(t)));
  }
}

Type StorageType(mlir::OpBuilder b, Type t) {
  if (t.isInteger(1)) {
    return b.getI8Type();
  }
  return t;
}

// Get the value of the scalar constant's literal in a C++ type.
template <typename T>
T ScalarConstantValue(const HloInstruction& instr, PrimitiveType dst_type) {
  CHECK(hlo_query::IsScalarConstant(&instr));
  absl::StatusOr<Literal> converted = instr.literal().Convert(dst_type);
  TF_CHECK_OK(converted.status());
  return converted.value().GetFirstElement<T>();
}

// Create a scalar constant.
template <typename T>
ma::ConstantOp CreateConst(ImplicitLocOpBuilder b, Type type, T value) {
  if (mlir::isa<mlir::IntegerType>(type)) {
    return b.create<ma::ConstantOp>(b.getIntegerAttr(type, value));
  }
  if (mlir::isa<mlir::FloatType>(type)) {
    return b.create<ma::ConstantOp>(
        b.getFloatAttr(type, static_cast<double>(value)));
  }
  LOG(FATAL) << "Constant type not supported: " << llvm_ir::DumpToString(type);
}

// Create a tensor constant.
template <typename T>
ma::ConstantOp CreateConst(ImplicitLocOpBuilder& b, Type type, T value,
                           ArrayRef<int64_t> shape) {
  auto tensor_type = mlir::RankedTensorType::get(shape, type);
  if (auto int_type = mlir::dyn_cast<mlir::IntegerType>(type)) {
    return b.create<ma::ConstantOp>(mlir::DenseElementsAttr::get(
        tensor_type, mlir::APInt(int_type.getIntOrFloatBitWidth(), value)));
  }
  if (auto float_type = mlir::dyn_cast<mlir::FloatType>(type)) {
    return b.create<ma::ConstantOp>(mlir::DenseElementsAttr::get(
        tensor_type, b.getFloatAttr(type, static_cast<double>(value))));
  }
  LOG(FATAL) << "Constant type not supported: " << llvm_ir::DumpToString(type);
}

Value ZerosLike(ImplicitLocOpBuilder& b, Value x) {
  if (auto src_shaped_ty = mlir::dyn_cast<ShapedType>(x.getType())) {
    Type src_ty = src_shaped_ty.getElementType();
    return CreateConst(b, src_ty, 0, src_shaped_ty.getShape());
  }
  return CreateConst(b, x.getType(), 0);
}

Value OnesLike(ImplicitLocOpBuilder& b, Value x) {
  if (auto src_shaped_ty = mlir::dyn_cast<ShapedType>(x.getType())) {
    Type src_ty = src_shaped_ty.getElementType();
    return CreateConst(b, src_ty, 1, src_shaped_ty.getShape());
  }
  return CreateConst(b, x.getType(), 1);
}

bool IsFp8Type(Type t) {
  return t.isFloat8E5M2() || t.isFloat8E4M3FN() || t.isFloat8E5M2FNUZ() ||
         t.isFloat8E4M3FNUZ() || t.isFloat8E4M3B11FNUZ();
}

// Triton type conversions.
Value Cast(ImplicitLocOpBuilder& b, Value value, Type dst_element_ty) {
  Type src_ty = value.getType();
  Type src_element_ty = src_ty;
  Type fp32_ty = b.getF32Type();
  Type dst_ty = dst_element_ty;
  if (auto src_shaped_ty = mlir::dyn_cast<ShapedType>(src_ty)) {
    src_element_ty = src_shaped_ty.getElementType();
    dst_ty = src_shaped_ty.clone(src_shaped_ty.getShape(), dst_element_ty);
    fp32_ty = src_shaped_ty.clone(src_shaped_ty.getShape(), b.getF32Type());
  }
  if (src_ty == dst_ty) {
    return value;
  }

  // All operations on bf16 are done through f32.
  if (src_element_ty.isBF16()) {
    return Cast(b, b.create<ma::ExtFOp>(fp32_ty, value), dst_element_ty);
  }
  if (dst_element_ty.isBF16()) {
    // S8 -> BF16 is directly supported and doesn't need to go through f32.
    if (!src_element_ty.isInteger(8)) {
      return b.create<ma::TruncFOp>(dst_ty, Cast(b, value, b.getF32Type()));
    }
  }

  // float => float
  auto src_fp_element_ty = mlir::dyn_cast<mlir::FloatType>(src_element_ty);
  auto dst_fp_element_ty = mlir::dyn_cast<mlir::FloatType>(dst_element_ty);
  if (src_fp_element_ty && dst_fp_element_ty) {
    // F8 <-> FP16, BF16, FP32, FP64 need to be handled via Triton's tt.fp_to_fp
    // because LLVM doesn't support casts from/to FP8.
    // TODO(b/266862493): Add end-to-end test once FP8 support lands in XLA as
    // we can't test the code below without patching the feature.
    if (IsFp8Type(src_element_ty)) {
      return b.create<mt::FpToFpOp>(dst_ty, value);
    }
    if (IsFp8Type(dst_element_ty)) {
      return b.create<mt::FpToFpOp>(
          dst_ty, value,
          mt::RoundingModeAttr::get(b.getContext(), mt::RoundingMode::RTNE));
    }

    if (src_fp_element_ty.getFPMantissaWidth() >
        dst_fp_element_ty.getFPMantissaWidth()) {
      return b.create<ma::TruncFOp>(dst_ty, value);
    } else {
      return b.create<ma::ExtFOp>(dst_ty, value);
    }
  }
  // int => int
  if (mlir::isa<mlir::IntegerType>(src_element_ty) &&
      mlir::isa<mlir::IntegerType>(dst_element_ty)) {
    if (src_element_ty.getIntOrFloatBitWidth() <
        dst_element_ty.getIntOrFloatBitWidth()) {
      if (src_element_ty.isInteger(1)) {
        return b.create<ma::ExtUIOp>(dst_ty, value);
      }
      return b.create<ma::ExtSIOp>(dst_ty, value);
    }
    return b.create<ma::TruncIOp>(dst_ty, value);
  }
  // int => float
  if (mlir::isa<mlir::IntegerType>(src_element_ty) && dst_fp_element_ty) {
    // TODO(b/266862493): Support unsigned integer types.
    if (src_element_ty.isInteger(1)) {
      return b.create<ma::UIToFPOp>(dst_ty, value);
    }
    return b.create<ma::SIToFPOp>(dst_ty, value);
  }
  // float => int
  if (src_fp_element_ty && mlir::isa<mlir::IntegerType>(dst_element_ty)) {
    if (dst_element_ty.isInteger(1)) {
      return b.create<ma::CmpFOp>(ma::CmpFPredicate::UNE, value,
                                  ZerosLike(b, value));
    }
    // TODO(b/266862493): Support unsigned integer types.
    // The current logic handles signed integer types only. Additional handling
    // is needed for unsigned integer types.
    auto cst_int = [&](int64_t x) {
      if (auto src_shaped_ty = mlir::dyn_cast<ShapedType>(src_ty)) {
        return CreateConst(b, dst_element_ty, x, src_shaped_ty.getShape());
      } else {
        return CreateConst(b, dst_element_ty, x);
      }
    };
    auto cst_float = [&](int64_t x) {
      if (auto src_shaped_ty = mlir::dyn_cast<ShapedType>(src_ty)) {
        return CreateConst(b, src_fp_element_ty, x, src_shaped_ty.getShape());
      } else {
        return CreateConst(b, src_fp_element_ty, x);
      }
    };
    auto fptosi = b.create<ma::FPToSIOp>(dst_ty, value);
    int64_t min = llvm::minIntN(dst_element_ty.getIntOrFloatBitWidth());
    int64_t max = llvm::maxIntN(dst_element_ty.getIntOrFloatBitWidth());

    // value <= static_cast<float>(INT_MIN) ? INT_MIN : ...
    auto clamped = b.create<mlir::arith::SelectOp>(
        b.create<mlir::arith::CmpFOp>(mlir::arith::CmpFPredicate::OLE, value,
                                      cst_float(min)),
        cst_int(min), fptosi);
    // value >= static_cast<float>(INT_MAX) ? INT_MAX : ...
    clamped = b.create<mlir::arith::SelectOp>(
        b.create<mlir::arith::CmpFOp>(mlir::arith::CmpFPredicate::OGE, value,
                                      cst_float(max)),
        cst_int(max), clamped);
    // isnan(value) ? 0 : ...
    return b.create<mlir::arith::SelectOp>(
        b.create<mlir::arith::CmpFOp>(mlir::arith::CmpFPredicate::UNO, value,
                                      value),
        cst_int(0), clamped);
  }

  LOG(FATAL) << "Type conversion not supported: "
             << llvm_ir::DumpToString(src_element_ty) << " -> "
             << llvm_ir::DumpToString(dst_element_ty);
}

Value Subtract(ImplicitLocOpBuilder& b, ValueRange values) {
  if (mlir::isa<mlir::IntegerType>(mlir::getElementTypeOrSelf(values[0]))) {
    return b.create<ma::SubIOp>(values[0], values[1]);
  } else {
    return b.create<ma::SubFOp>(values[0], values[1]);
  }
}

Value Compare(ImplicitLocOpBuilder& b, ValueRange values,
              mlir::mhlo::ComparisonDirection direction) {
  const Type type = mlir::getElementTypeOrSelf(values[0]);
  if (mlir::isa<mlir::IntegerType>(type)) {
    return b.create<ma::CmpIOp>(
        mlir::mhlo::impl::getCmpPredicate<ma::CmpIPredicate>(
            direction,
            /*isSigned=*/!type.isInteger(1))
            .value(),
        values[0], values[1]);
  }
  return b.create<ma::CmpFOp>(
      mlir::mhlo::impl::getCmpPredicate<ma::CmpFPredicate>(direction,
                                                           /*isSigned=*/true)
          .value(),
      values[0], values[1]);
}

Value Maximum(ImplicitLocOpBuilder& b, const se::DeviceDescription& device_info,
              ValueRange values) {
  if (mlir::isa<mlir::FloatType>(mlir::getElementTypeOrSelf(values[0]))) {
    return b.create<ma::MaximumFOp>(values);
  }
  // logic: isNaN(lhs) || (!isNan(rhs) && lhs >= rhs) ? lhs : rhs
  // See also: IEEE Std 754-2008 5.11.
  //
  // This also works, but we wanted to make it similar to minimum.
  // logic: isNaN(lhs) || lhs >= rhs ? lhs : rhs
  Value lhs_is_nan =
      Compare(b, {values[0], values[0]}, mlir::mhlo::ComparisonDirection::NE);
  Value rhs_is_not_nan =
      Compare(b, {values[1], values[1]}, mlir::mhlo::ComparisonDirection::EQ);
  Value lhs_is_ge = Compare(b, values, mlir::mhlo::ComparisonDirection::GE);
  return b.create<ma::SelectOp>(
      b.create<ma::OrIOp>(lhs_is_nan,
                          b.create<ma::AndIOp>(rhs_is_not_nan, lhs_is_ge)),
      values[0], values[1]);
}

Value Minimum(ImplicitLocOpBuilder& b, const se::DeviceDescription& device_info,
              ValueRange values) {
  if (mlir::isa<mlir::FloatType>(mlir::getElementTypeOrSelf(values[0]))) {
    return b.create<ma::MinimumFOp>(values);
  }
  // logic: isNaN(lhs) || (!isNan(rhs) && lhs <= rhs) ? lhs : rhs
  // See also: IEEE Std 754-2008 5.11.
  //
  // This should also work, but the tests show that it doesn't work for
  // minimum(x, NaN):
  // logic: isNaN(lhs) || lhs <= rhs ? lhs : rhs
  Value lhs_is_nan =
      Compare(b, {values[0], values[0]}, mlir::mhlo::ComparisonDirection::NE);
  Value rhs_is_not_nan =
      Compare(b, {values[1], values[1]}, mlir::mhlo::ComparisonDirection::EQ);
  Value lhs_is_le = Compare(b, values, mlir::mhlo::ComparisonDirection::LE);
  return b.create<ma::SelectOp>(
      b.create<ma::OrIOp>(lhs_is_nan,
                          b.create<ma::AndIOp>(rhs_is_not_nan, lhs_is_le)),
      values[0], values[1]);
}

// TODO(b/269489810): Contribute nicer builders to Triton, so we don't need to
// define these utilities.
Value Splat(ImplicitLocOpBuilder& b, Value value, ArrayRef<int64_t> shape) {
  auto type = mlir::RankedTensorType::get(shape, value.getType());
  return b.create<mt::SplatOp>(type, value);
}

using TensorValue = mlir::TypedValue<mlir::RankedTensorType>;

Value Broadcast(ImplicitLocOpBuilder& b, TensorValue value,
                ArrayRef<int64_t> shape) {
  return b.create<mt::BroadcastOp>(value.getType().clone(shape), value);
}

Value Range(ImplicitLocOpBuilder& b, int32_t limit) {
  auto type = mlir::RankedTensorType::get(limit, b.getI32Type());
  return b.create<mt::MakeRangeOp>(type, 0, limit);
}

Value AddPtr(ImplicitLocOpBuilder& b, Value ptr, Value offset) {
  return b.create<mt::AddPtrOp>(ptr.getType(), ptr, offset);
}

absl::StatusOr<Value> EmitElementwise(ImplicitLocOpBuilder& b,
                                      absl::string_view libdevice_path,
                                      const se::DeviceDescription& device_info,
                                      const HloInstruction& hlo,
                                      ValueRange inputs) {
  if (mlir::getElementTypeOrSelf(inputs[0]).isF32() ||
      mlir::getElementTypeOrSelf(inputs[0]).isF64()) {
    auto dev_fn_id = GetTargetDeviceFunctionID(hlo.opcode());
    if (dev_fn_id.ok()) {
      llvm::Triple triple("nvptx64-unknown-unknown");
      if (std::holds_alternative<se::RocmComputeCapability>(
              device_info.gpu_compute_capability())) {
        triple.setTriple("amdgcn-unknown-unknown");
      }
      return b.create<mt::ExternElementwiseOp>(
          inputs[0].getType(), inputs, "libdevice", libdevice_path,
          ObtainDeviceFunctionName(dev_fn_id.value(),
                                   hlo.shape().element_type(), triple),
          /*pure=*/true);
    }
  }
  const bool is_integer =
      mlir::isa<mlir::IntegerType>(mlir::getElementTypeOrSelf(inputs[0]));

  switch (hlo.opcode()) {
    case HloOpcode::kCopy:
      // Dimension transformations are taken care of separately.
      return inputs[0];
    case HloOpcode::kAbs:
      if (is_integer) {
        return b.create<mm::AbsIOp>(inputs[0]);
      }
      return b.create<mm::AbsFOp>(inputs[0]);
    case HloOpcode::kCeil:
      return b.create<mm::CeilOp>(inputs[0]);
    case HloOpcode::kFloor:
      return b.create<mm::FloorOp>(inputs[0]);
    case HloOpcode::kNot:
      return b.create<ma::XOrIOp>(inputs[0], OnesLike(b, inputs[0]));
    case HloOpcode::kNegate:
      // NegFOp is not supported by Triton.
      return Subtract(b, {ZerosLike(b, inputs[0]), inputs[0]});
    case HloOpcode::kConvert: {
      TF_ASSIGN_OR_RETURN(Type dst_ty,
                          TritonType(b, hlo.shape().element_type()));
      return Cast(b, inputs[0], dst_ty);
    }
    case HloOpcode::kAdd:
      if (is_integer) {
        return b.create<ma::AddIOp>(inputs[0], inputs[1]);
      }
      return b.create<ma::AddFOp>(inputs[0], inputs[1]);
    case HloOpcode::kSubtract:
      return Subtract(b, inputs);
    case HloOpcode::kMultiply:
      if (is_integer) {
        return b.create<ma::MulIOp>(inputs[0], inputs[1]);
      }
      return b.create<ma::MulFOp>(inputs[0], inputs[1]);
    case HloOpcode::kMaximum:
      return Maximum(b, device_info, inputs);
    case HloOpcode::kMinimum:
      return Minimum(b, device_info, inputs);
    case HloOpcode::kClamp:
      return Maximum(
          b, device_info,
          {Minimum(b, device_info, {inputs[1], inputs[2]}), inputs[0]});
    case HloOpcode::kAnd:
      return b.create<ma::AndIOp>(inputs[0], inputs[1]);
    case HloOpcode::kOr:
      return b.create<ma::OrIOp>(inputs[0], inputs[1]);
    case HloOpcode::kXor:
      return b.create<ma::XOrIOp>(inputs[0], inputs[1]);
    case HloOpcode::kDivide:
      if (is_integer) {
        // Unsigned not supported yet.
        return b.create<ma::DivSIOp>(inputs[0], inputs[1]);
      }
      return b.create<ma::DivFOp>(inputs[0], inputs[1]);
    case HloOpcode::kCompare:
      return Compare(
          b, inputs,
          mlir::mhlo::symbolizeComparisonDirection(
              ComparisonDirectionToString(hlo.comparison_direction()))
              .value());
    case HloOpcode::kSelect:
      return b.create<ma::SelectOp>(
          Compare(b, {inputs[0], ZerosLike(b, inputs[0])},
                  mlir::mhlo::ComparisonDirection::NE),
          inputs[1], inputs[2]);
    default:
      return absl::InvalidArgumentError(
          absl::StrCat("Unsupported elementwise operation ", hlo.ToString()));
  }
}

Value EmitParameterLoad(ImplicitLocOpBuilder& b, Value pointer,
                        ArrayRef<int32_t> boundary_checks) {
  // 0-D MakeTensorPtrOp
  //
  // Triton tries to access the -1 element of a vector and segfaults when
  // lowering the code to load a 0-D tensor to LLVM. The workaround is to load a
  // regular pointer + a splat.
  if (auto make_tensor_ptr = pointer.getDefiningOp<mt::MakeTensorPtrOp>()) {
    if (make_tensor_ptr.getOffsets().empty()) {
      return Splat(b,
                   b.create<mt::LoadOp>(make_tensor_ptr.getBase(),
                                        mt::CacheModifier::NONE,
                                        mt::EvictionPolicy::NORMAL,
                                        /*isVolatile=*/false),
                   {});
    }
  }

  // Any other tensor pointer.
  if (mt::isTensorPointerType(pointer.getType())) {
    std::optional<mt::PaddingOption> padding;
    if (!boundary_checks.empty()) {
      padding = mt::PaddingOption::PAD_ZERO;
    }
    return b.create<mt::LoadOp>(pointer, boundary_checks, padding,
                                mt::CacheModifier::NONE,
                                mt::EvictionPolicy::NORMAL,
                                /*isVolatile=*/false);
  }

  // Non-tensor pointer.
  //
  // TODO(b/343013366): Remove this after we delete the legacy SoftMax code.
  // It's the only place where this code-path is used.
  return Splat(b,
               b.create<mt::LoadOp>(pointer, mt::CacheModifier::NONE,
                                    mt::EvictionPolicy::NORMAL,
                                    /*isVolatile=*/false),
               {});
}

absl::StatusOr<Value> EmitConstant(ImplicitLocOpBuilder& b,
                                   const HloInstruction& constant) {
  TF_ASSIGN_OR_RETURN(Type ty, TritonType(b, constant.shape().element_type()));
  if (constant.shape().IsInteger()) {
    if (constant.shape().element_type() == U64) {
      return CreateConst(b, ty, ScalarConstantValue<uint64_t>(constant, U64));
    } else {
      return CreateConst(b, ty, ScalarConstantValue<int64_t>(constant, S64));
    }
  }
  return CreateConst(b, ty, ScalarConstantValue<double>(constant, F64));
}

// Grouped properties of tiled dimensions used to generate block pointers.
struct DimProperties {
  DimProperties(int64_t index, Value pid, int block_size, int split_value)
      : index(index),
        pid(pid),
        block_size(block_size),
        split_value(split_value) {}

  // Logical index of the dimension at the tiling-defining operation.
  int64_t index;
  // Block program ID corresponding to this dimension.
  Value pid;
  // Elements of the dimension to process per block program.
  int block_size;
  // Size of the major part of the dimension if it's split into two parts.
  int split_value;
};

struct Side {
  explicit Side(TritonFusionAnalysis::Scope scope,
                std::vector<DimProperties> tiled_dims = {},
                std::optional<int64_t> batch_dim_idx = std::nullopt)
      : scope(scope), tiled_dims(tiled_dims), batch_dim_idx(batch_dim_idx) {}
  TritonFusionAnalysis::Scope scope;
  std::vector<DimProperties> tiled_dims;
  std::optional<int64_t> batch_dim_idx;
  int64_t unpack_dim_idx = 0;
};

absl::StatusOr<Value> EmitBroadcast(ImplicitLocOpBuilder& b,
                                    const TritonFusionAnalysis* analysis,
                                    const Side& side,
                                    const HloInstruction& broadcast,
                                    Value input) {
  TF_RET_CHECK(analysis != nullptr);
  std::vector<int64_t> out_shape;
  for (const DimProperties& dim : side.tiled_dims) {
    const TensorIterationSpec::DimIterationSpec* spec =
        analysis->IterSpec(side.scope, &broadcast, dim.index);
    if (spec != nullptr && spec->at(0).stride > 0) {
      out_shape.push_back(dim.block_size);
    }
  }
  auto tensor_input = mlir::dyn_cast<TensorValue>(input);
  if (!tensor_input) {
    // Input is scalar.
    return Splat(b, input, out_shape);
  }
  if (tensor_input.getType().getRank() == out_shape.size()) {
    // No dimensions to broadcast.
    return input;
  }
  // Add broadcasted dimensions one by one.
  Value expanded_input = tensor_input;
  int dim_idx = 0;
  for (const DimProperties& dim : side.tiled_dims) {
    if (auto* spec = analysis->IterSpec(side.scope, &broadcast, dim.index);
        spec != nullptr && spec->at(0).stride > 0) {
      if (analysis->IterSpec(side.scope, broadcast.operand(0), dim.index) ==
          nullptr) {
        // Broadcasted dimension.
        expanded_input = b.create<mt::ExpandDimsOp>(expanded_input, dim_idx);
      }
      ++dim_idx;
    }
  }
  return Broadcast(b, mlir::cast<TensorValue>(expanded_input), out_shape);
}

absl::StatusOr<Value> EmitScope(
    ImplicitLocOpBuilder& b, absl::string_view libdevice_path,
    const se::DeviceDescription& device_info,
    const TritonFusionAnalysis* analysis, const Side& side,
    absl::Span<const HloInstruction* const> instructions,
    absl::flat_hash_map<const HloInstruction*, Value>& values);

// Adds `n` leading `1` dimensions to the input tensor.
Value LeftExpandDimNTimes(ImplicitLocOpBuilder& b, Value input, int64_t n) {
  for (int i = 0; i < n; ++i) {
    input = b.create<mt::ExpandDimsOp>(input, /*axis=*/0);
  }
  return input;
}

absl::StatusOr<Value> EmitReduce(
    ImplicitLocOpBuilder& b, const TiledHloInstruction& tiled_hlo_reduce,
    absl::flat_hash_map<const TiledHloInstruction*, Value>& values,
    absl::string_view libdevice_path,
    const se::DeviceDescription& device_info) {
  // At the moment, we should only emit a full reduction over a single
  // dimension using a scalar as a neutral element.
  const HloReduceInstruction& hlo_reduce =
      *::xla::Cast<HloReduceInstruction>(tiled_hlo_reduce.hlo());
  TF_RET_CHECK(hlo_reduce.operand_count() == 2);
  TF_RET_CHECK(hlo_reduce.dimensions().size() == 1);

  Value input = values[tiled_hlo_reduce.operand(0)];
  llvm::ArrayRef<int64_t> input_shape =
      mlir::cast<ShapedType>(input.getType()).getShape();
  absl::Span<const int64_t> source_tensor_shape =
      hlo_reduce.operand(0)->shape().dimensions();

  int reduction_dimension = hlo_reduce.dimensions().front();

  // Since every shape is padded to a power of 2 in Triton, the input tile may
  // be padded with arbitrary values. These values could affect the result of
  // the reduction, so we need to mask them away. Luckily, we have a monoid
  // structure (element_type, hlo_reduce.to_apply(), hlo_reduce.operand(1))---
  // up to floating-point inaccuracies. Masking the input using
  // hlo_reduce.operand(1) is thus always the right choice to ensure that the
  // reduction is computed correctly, since it is the neutral value with regards
  // to the reducer.
  int64_t source_tensor_reduction_dimension_size =
      source_tensor_shape[reduction_dimension];
  int64_t input_reduction_dimension_size = input_shape[reduction_dimension];
  if (input_reduction_dimension_size !=
      source_tensor_reduction_dimension_size) {
    Value range = Range(b, input_reduction_dimension_size);
    // Triton's broadcast requires that the rank of the source and broadcasted
    // result are equal.
    for (int i = 0; i < input_shape.size() - 1; i++) {
      if (i < reduction_dimension) {
        range = b.create<mt::ExpandDimsOp>(range, /*axis=*/0);
      } else {
        range = b.create<mt::ExpandDimsOp>(range, /*axis=*/i + 1);
      }
    }
    Value mask = Broadcast(b, mlir::cast<TensorValue>(range), input_shape);
    Value constant =
        CreateConst(b, b.getI32Type(), source_tensor_reduction_dimension_size);
    Value constant_tensor = Splat(b, constant, input_shape);
    mask = b.create<ma::CmpIOp>(ma::CmpIPredicate::slt, mask, constant_tensor);

    Value neutral = values[tiled_hlo_reduce.operand(1)];
    // Triton's broadcast requires that the rank of the source and broadcasted
    // result are equal.
    for (int i = 0; i < input_shape.size(); i++) {
      neutral = b.create<mt::ExpandDimsOp>(neutral, /*axis=*/0);
    }
    neutral = Broadcast(b, mlir::cast<TensorValue>(neutral), input_shape);
    input = b.create<ma::SelectOp>(mask, input, neutral);
  }

  mt::ReduceOp reduction = b.create<mt::ReduceOp>(input, reduction_dimension);
  {
    TF_ASSIGN_OR_RETURN(Type result_ty,
                        TritonType(b, hlo_reduce.shape().element_type()));
    mlir::Location loc = b.getLoc();
    mlir::Block* reducer = b.createBlock(&reduction->getRegion(0), {},
                                         {result_ty, result_ty}, {loc, loc});

    HloComputation* reduction_computation = hlo_reduce.to_apply();

    std::vector<const HloInstruction*> to_emit;
    absl::flat_hash_map<const HloInstruction*, Value> region_values;
    for (const HloInstruction* instr :
         reduction_computation->MakeInstructionPostOrder()) {
      if (instr->opcode() == HloOpcode::kParameter) {
        int parameter_number = instr->parameter_number();
        TF_RET_CHECK(parameter_number < 2);
        TF_RET_CHECK(
            region_values
                .insert({instr, reducer->getArgument(parameter_number)})
                .second);
      } else {
        to_emit.push_back(instr);
      }
    }

    TF_RET_CHECK(!to_emit.empty());

    b.setInsertionPointToStart(reducer);
    TF_ASSIGN_OR_RETURN(
        Value result,
        EmitScope(b, libdevice_path, device_info, /*analysis=*/nullptr,
                  Side(TritonFusionAnalysis::Scope::OUTPUT), to_emit,
                  region_values));
    b.create<mt::ReduceReturnOp>(SmallVector<Value>({result}));
    b.setInsertionPointAfter(reduction);
  }

  Value result = reduction.getResult().front();

  // We want to return a tensor, but the ReturnReduceOp produces a raw scalar
  // when reducing a single dim. To convert to a tensor we splat the result.
  if (!mlir::dyn_cast<TensorValue>(reduction.getResult().front())) {
    result = Splat(b, result, {});
  }

  return result;
}

// Emit code corresponding to a fusion instruction somehow nested within the
// initial Triton fusion. This can happen when we carry around auxiliary
// computations, e.g. with reduces. Since we are emitting a single Triton
// fusion, we simply flatten the fusion inside the computation.
//
// TODO(b/331413981): get rid of this special handling once this is solved.
absl::StatusOr<Value> EmitNestedFusion(
    ImplicitLocOpBuilder& b, absl::string_view libdevice_path,
    const se::DeviceDescription& device_info,
    const HloFusionInstruction& fusion_instruction,
    absl::flat_hash_map<const HloInstruction*, Value>& values) {
  // TODO(b/331402498): revisit the order of scope once we completely deprecate
  // Triton fusion analysis.
  const HloComputation* fusion_computation =
      fusion_instruction.fused_instructions_computation();

  absl::flat_hash_map<const HloInstruction*, Value> region_values;

  std::vector<const HloInstruction*> to_emit;
  for (const HloInstruction* instr :
       fusion_computation->MakeInstructionPostOrder()) {
    if (instr->opcode() == HloOpcode::kParameter) {
      int64_t parameter_number = instr->parameter_number();
      auto it = values.find(fusion_instruction.operand(parameter_number));
      TF_RET_CHECK(it != values.end());
      TF_RET_CHECK(region_values.insert({instr, it->second}).second);
    } else {
      to_emit.push_back(instr);
    }
  }

  TF_RET_CHECK(to_emit.back() == fusion_computation->root_instruction());

  return EmitScope(b, libdevice_path, device_info, /*analysis=*/nullptr,
                   Side(TritonFusionAnalysis::Scope::OUTPUT), to_emit,
                   region_values);
}

Value EmitTiledBroadcast(
    ImplicitLocOpBuilder& b, const TiledHloInstruction& tiled_broadcast,
    absl::flat_hash_map<const TiledHloInstruction*, Value>& values) {
  const llvm::SmallVector<int64_t>& input_tile_shape =
      tiled_broadcast.operand(0)->tile_sizes();
  const llvm::SmallVector<int64_t>& output_tile_shape =
      tiled_broadcast.tile_sizes();
  SmallVector<int64_t> padded_output_tile_shape =
      GetPaddedTileSizes(output_tile_shape);

  Value expanded_input = values[tiled_broadcast.operand(0)];

  // Returns true if `dim_id` is broadcasted.
  auto is_broadcasted_dim = [&](int64_t dim_id) {
    return !llvm::is_contained(tiled_broadcast.hlo()->dimensions(), dim_id);
  };

  // Handle the 0d special case.
  if (input_tile_shape.empty()) {
    expanded_input =
        LeftExpandDimNTimes(b, expanded_input, output_tile_shape.size());
    return Broadcast(b, mlir::cast<TensorValue>(expanded_input),
                     padded_output_tile_shape);
  }

  // The loop below iterates over output dimensions and tracks matching dims in
  // input_tile_shape and expended_input value.
  // `input_dim_id != expanded_input_dim_id`, because size-1 dims are present in
  // the input tile shape, but not in the MLIR value. Triton doesn't like size-1
  // dims, so they are inserted only for dimensions that will be broadcasted.
  int64_t input_dim_id = 0;
  int64_t expanded_input_dim_id = 0;
  for (size_t output_dim_id = 0; output_dim_id < output_tile_shape.size();
       ++output_dim_id) {
    if (is_broadcasted_dim(output_dim_id)) {
      // Expand dim for broadcast.
      expanded_input =
          b.create<mt::ExpandDimsOp>(expanded_input, expanded_input_dim_id);
      ++expanded_input_dim_id;
    } else {
      // The dim is not broadcasted. Validate that it's equal in the input and
      // output tile.
      CHECK_EQ(input_tile_shape[input_dim_id],
               output_tile_shape[output_dim_id]);
      ++input_dim_id;
      ++expanded_input_dim_id;
    }
  }

  return Broadcast(b, mlir::cast<TensorValue>(expanded_input),
                   padded_output_tile_shape);
}

Value EmitTiledReshape(ImplicitLocOpBuilder& b, ArrayRef<int64_t> tile_sizes,
                       Value input) {
  SmallVector<int64_t> padded_tile_sizes = GetPaddedTileSizes(tile_sizes);

  Type input_element_type =
      mlir::cast<ShapedType>(input.getType()).getElementType();
  Type output_tensor_type =
      mlir::RankedTensorType::get(padded_tile_sizes, input_element_type);

  // Conservatively prevent Triton from reordering elements within the tile.
  // TODO(b/353637689): see if this restriction can be lifted.
  bool allow_reorder = false;
  return b.create<mt::ReshapeOp>(output_tensor_type, input, allow_reorder)
      .getResult();
}

Value EmitTiledTranspose(ImplicitLocOpBuilder& b, ArrayRef<int64_t> tile_sizes,
                         SmallVector<int64_t> dimensions, Value input) {
  SmallVector<int64_t> padded_tile_sizes = GetPaddedTileSizes(tile_sizes);

  Type input_element_type =
      mlir::cast<ShapedType>(input.getType()).getElementType();
  Type output_tensor_type =
      mlir::RankedTensorType::get(padded_tile_sizes, input_element_type);

  SmallVector<int32_t> order = llvm::to_vector_of<int32_t>(dimensions);

  return b.create<mt::TransOp>(output_tensor_type, input, order);
}

Value EmitTiledBitcast(ImplicitLocOpBuilder& b,
                       const TiledHloInstruction& tiled_bitcast, Value input) {
  // Any Bitcast is decomposable to a transpose+reshape+transpose.
  auto trt = ShapeUtil::DecomposeBitcastToTrt(
      tiled_bitcast.hlo()->operand(0)->shape(), tiled_bitcast.hlo()->shape());

  // When replacing the `bitcast` with `transpose` + `reshape` + `transpose` we
  // need to provide the tile sizes at output of each op. We already have the
  // tiling of the `input` (before the first transpose) and the tiling of the
  // final output (after the second transpose), so what's missing are the two
  // tilings in between - after the first transpose and after the reshape. In
  // the case of arbitrary ops, we would need to run the tiling analysis to
  // compute this, but in the case of bitcast we can trivially compute the
  // needed tile sizes from the input and output.

  // The tiles sizes we need to use for the output of the first transpose
  // are the permuted tiles sizes of the input. Note that these are
  // different, even in rank, compared to the tile sizes of the final shape of
  // the bitcast, so it's not possible to easily propagate them from the output.
  std::vector<int64_t> transpose1_tile_sizes =
      Permute(tiled_bitcast.operand(0)->tile_sizes(), trt.transpose1_dims);
  Value normalized_input =
      trt.IsTranspose1Identity()
          ? input
          : EmitTiledTranspose(b, transpose1_tile_sizes,
                               llvm::to_vector(trt.transpose1_dims), input);

  // Like the first transpose above, the tile sizes after the second transpose
  // are a permutation (according to transpose2_dims) of the tile sizes of
  // the reshape. Since we know the tile sizes of the final transpose and need
  // the tile sizes of the reshape, we compute the tile sizes backwards, taking
  // the inreverse permutation.
  std::vector<int64_t> reshape_tile_sizes =
      PermuteInverse(tiled_bitcast.tile_sizes(), trt.transpose2_dims);
  Value normalized_reshape =
      ShapeUtil::Equal(trt.transpose1_shape, trt.reshape_shape)
          ? normalized_input
          : EmitTiledReshape(b, reshape_tile_sizes, normalized_input);

  // The final transpose simply uses the tile sizes computed for the original
  // bitcast by the tiling analysis.
  return trt.IsTranspose2Identity()
             ? normalized_reshape
             : EmitTiledTranspose(b, tiled_bitcast.tile_sizes(),
                                  llvm::to_vector(trt.transpose2_dims),
                                  normalized_reshape);
}

absl::StatusOr<Value> EmitTiledHloInstruction(
    ImplicitLocOpBuilder& b, absl::string_view libdevice_path,
    const se::DeviceDescription& device_info,
    const HloFusionInstruction* fusion, const TiledHloInstruction& tiled_hlo,
    mlir::triton::FuncOp fn, ValueRange tile_multi_index,
    absl::flat_hash_map<const TiledHloInstruction*, Value>& values) {
  const HloInstruction* hlo = tiled_hlo.hlo();

  if (fusion->IsUserOf(hlo)) {
    TF_ASSIGN_OR_RETURN(auto make_tensor,
                        ir_emitter_triton_internal::CreateMakeTensorPtrOp(
                            b, tile_multi_index, tiled_hlo,
                            fn.getArgument(fusion->operand_index(hlo))));

    Value parameter =
        EmitParameterLoad(b, make_tensor.op, make_tensor.boundary_checks);

    // Some types are stored using different types, e.g. i1 is stored in memory
    // as i8. It's important to type checking that we perform a conversion
    // after loading if the type of the loaded parameter does not match what
    // is expected.
    Type loaded_element_type =
        mlir::cast<ShapedType>(parameter.getType()).getElementType();
    TF_ASSIGN_OR_RETURN(Type expected_element_type,
                        TritonType(b, hlo->shape().element_type()));

    if (expected_element_type != loaded_element_type) {
      // Ensure that we didn't mess up somewhere else by checking that we
      // indeed loaded the expected storage type for the expected element type.
      if (loaded_element_type != StorageType(b, expected_element_type)) {
        return absl::InternalError(absl::StrCat(
            "Parameters were loaded with an unexpected element type "
            "while lowering ",
            fusion->called_computation()->ToString()));
      }
      parameter = Cast(b, parameter, expected_element_type);
    }

    return parameter;
  }

  if (hlo->opcode() == HloOpcode::kConstant) {
    if (ShapeUtil::IsScalar(hlo->shape())) {
      TF_ASSIGN_OR_RETURN(Value constant, EmitConstant(b, *hlo));
      // Splat makes it a tensor to avoid type mismatches.
      return Splat(b, constant, {});
    }
    return absl::UnimplementedError(
        absl::StrCat("Unsupported non-scalar constant ", hlo->ToString()));
  }

  if (hlo->opcode() == HloOpcode::kBroadcast) {
    return EmitTiledBroadcast(b, tiled_hlo, values);
  }

  if (hlo->opcode() == HloOpcode::kReduce) {
    return EmitReduce(b, tiled_hlo, values, libdevice_path, device_info);
  }

  if (hlo->IsElementwise()) {
    std::vector<Value> operands;
    operands.reserve(hlo->operands().size());

    for (const TiledHloInstruction* operand : tiled_hlo.operands()) {
      operands.push_back(values[operand]);
    }
    return EmitElementwise(b, libdevice_path, device_info, *hlo, operands);
  }

  if (hlo->opcode() == HloOpcode::kReshape) {
    return EmitTiledReshape(b, tiled_hlo.tile_sizes(),
                            values[tiled_hlo.operand(0)]);
  }

  if (hlo->opcode() == HloOpcode::kBitcast) {
    return EmitTiledBitcast(b, tiled_hlo, values[tiled_hlo.operand(0)]);
  }

  if (hlo->opcode() == HloOpcode::kTranspose) {
    auto transpose =
        ::xla::Cast<const HloTransposeInstruction>(tiled_hlo.hlo());
    return EmitTiledTranspose(b, tiled_hlo.tile_sizes(),
                              llvm::to_vector(transpose->dimensions()),
                              values[tiled_hlo.operand(0)]);
  }

  // Slice is currently supported only as an operation on indices
  // which is pushed to loads and stores. We don't generate any further code.
  if (hlo->opcode() == HloOpcode::kSlice) {
    return values[tiled_hlo.operand(0)];
  }

  return absl::UnimplementedError(
      absl::StrCat("Unsupported operation ", hlo->ToString()));
}

// Emit sequence of instructions using compatible tiling ordered producers
// before consumers.
absl::StatusOr<Value> EmitTiledScope(
    ImplicitLocOpBuilder& b, absl::string_view libdevice_path,
    const se::DeviceDescription& device_info,
    const HloFusionInstruction* fusion,
    const TiledHloComputation& tiled_computation, mlir::triton::FuncOp fn,
    ValueRange tile_multi_index) {
  absl::flat_hash_map<const TiledHloInstruction*, Value> values;
  for (const TiledHloInstruction* tiled_hlo :
       tiled_computation.instructions()) {
    TF_ASSIGN_OR_RETURN(
        Value result,
        EmitTiledHloInstruction(b, libdevice_path, device_info, fusion,
                                *tiled_hlo, fn, tile_multi_index, values));
    TF_RET_CHECK(values.insert({tiled_hlo, result}).second)
        << tiled_hlo->hlo()->ToString();
    VLOG(8) << "Emitted "
            << tiled_hlo->hlo()->ToString(HloPrintOptions::ShortParsable());
  }
  return values[tiled_computation.GetRoot()];
}

// Emit sequence of operations for unpacking 2xi4 -> i8.
absl::StatusOr<Value> EmitUnpackInt4(ImplicitLocOpBuilder& b,
                                     const HloInstruction* hlo,
                                     const Side& side, Value& value) {
  VLOG(6) << "EmitUnpackInt4: " << hlo->ToString();
  auto input_type = mlir::cast<mlir::RankedTensorType>(value.getType());
  if (input_type.getShape().size() != 2) {
    return absl::InvalidArgumentError(
        absl::StrCat("UnpackInt4 works only for 2d inputs: ", hlo->ToString()));
  }
  // We use shifts instead the mask because we need to keep the sign bit.
  Value shift4 =
      Splat(b, CreateConst(b, b.getI8Type(), 4), input_type.getShape());
  Value lo = b.create<ma::ShRSIOp>(b.create<ma::ShLIOp>(value, shift4), shift4);
  Value hi = b.create<ma::ShRSIOp>(value, shift4);
  Value result = b.create<mt::JoinOp>(hi, lo);
  if (side.unpack_dim_idx == 0) {
    result = b.create<mt::TransOp>(result, b.getDenseI32ArrayAttr({0, 2, 1}));
  }
  SmallVector<int64_t> result_shape(input_type.getShape());
  result_shape[side.unpack_dim_idx] *= 2;
  auto type = mlir::RankedTensorType::get(result_shape, b.getI8Type());
  return b.create<mt::ReshapeOp>(type, result, /*allow_reorder=*/false);
}

// Emit sequence of instructions using compatible tiling ordered producers
// before consumers.
absl::StatusOr<Value> EmitScope(
    ImplicitLocOpBuilder& b, absl::string_view libdevice_path,
    const se::DeviceDescription& device_info,
    const TritonFusionAnalysis* analysis, const Side& side,
    absl::Span<const HloInstruction* const> instructions,
    absl::flat_hash_map<const HloInstruction*, Value>& values) {
  for (const HloInstruction* hlo : instructions) {
    Value result;
    if (hlo->opcode() == HloOpcode::kConvert &&
        hlo->operand(0)->shape().element_type() == S4) {
      if (!hlo->GetModule()
               ->config()
               .debug_options()
               .xla_gpu_enable_triton_gemm_int4()) {
        return absl::UnimplementedError(
            "Int4 support is not enabled in the debug options.");
      }

      TF_ASSIGN_OR_RETURN(
          auto unpacked, EmitUnpackInt4(b, hlo, side, values[hlo->operand(0)]));
      std::vector<Value> operands({unpacked});
      TF_ASSIGN_OR_RETURN(result, EmitElementwise(b, libdevice_path,
                                                  device_info, *hlo, operands));
    } else if (hlo->opcode() == HloOpcode::kConcatenate ||
               hlo->opcode() == HloOpcode::kDynamicSlice) {
      // Parameter loads and their concatenations are handled outside EmitScope.
      TF_RET_CHECK(values.contains(hlo)) << hlo->ToString();
      continue;
    } else if (hlo->opcode() == HloOpcode::kParameter) {
      if (hlo->users()[0]->opcode() == HloOpcode::kConcatenate ||
          hlo->users()[0]->opcode() == HloOpcode::kDynamicSlice) {
        continue;
      }
      TF_RET_CHECK(values.contains(hlo)) << hlo->ToString();
      continue;
    } else if (hlo->opcode() == HloOpcode::kConstant) {
      TF_ASSIGN_OR_RETURN(Value constant, EmitConstant(b, *hlo));
      // Splat makes it a tensor to avoid type mismatches.
      result = Splat(b, constant, {});
    } else if (hlo->opcode() == HloOpcode::kBroadcast) {
      TF_ASSIGN_OR_RETURN(result, EmitBroadcast(b, analysis, side, *hlo,
                                                values[hlo->operand(0)]));
    } else if (HloInstruction::IsOpElementwise(hlo->opcode())) {
      std::vector<Value> operands;
      operands.reserve(hlo->operands().size());
      for (const HloInstruction* operand : hlo->operands()) {
        operands.push_back(values[operand]);
      }
      TF_ASSIGN_OR_RETURN(result, EmitElementwise(b, libdevice_path,
                                                  device_info, *hlo, operands));
    } else if (hlo->opcode() == HloOpcode::kTuple) {
      TF_RET_CHECK(hlo->IsRoot()) << hlo->ToString();
    } else if (hlo->opcode() == HloOpcode::kBitcast ||
               hlo->opcode() == HloOpcode::kTranspose ||
               hlo->opcode() == HloOpcode::kSlice ||
               hlo->opcode() == HloOpcode::kReshape ||
               hlo->opcode() == HloOpcode::kPad) {
      // All these are currently supported only as operations on indices
      // which are pushed to loads and stores. No operations on tiles are
      // performed here.
      result = values[hlo->operand(0)];
    } else if (hlo->opcode() == HloOpcode::kFusion) {
      const auto* fusion_instruction = ::xla::Cast<HloFusionInstruction>(hlo);
      TF_ASSIGN_OR_RETURN(result,
                          EmitNestedFusion(b, libdevice_path, device_info,
                                           *fusion_instruction, values));
    } else {
      return absl::InvalidArgumentError(
          absl::StrCat("Unsupported operation ", hlo->ToString()));
    }
    TF_RET_CHECK(values.insert({hlo, result}).second) << hlo->ToString();
    VLOG(8) << "Emitted " << hlo->ToString(HloPrintOptions::ShortParsable());
  }
  return values[instructions.back()];
}

const TensorIterationSpec::DimIterationSpec* GetLhsNoncontractingSplitSpec(
    const TritonFusionAnalysis& analysis, int64_t lhs_noncontracting_dim_idx) {
  const TensorIterationSpec::DimIterationSpec* result = nullptr;
  for (const HloInstruction* lhs_param :
       analysis.ScopeParameters(TritonFusionAnalysis::Scope::LHS)) {
    const TensorIterationSpec::DimIterationSpec* spec =
        analysis.IterSpec(TritonFusionAnalysis::Scope::LHS, lhs_param,
                          lhs_noncontracting_dim_idx);
    if (spec != nullptr && spec->size() > 1) {
      CHECK_EQ(spec->size(), 2);
      if (result != nullptr) {
        CHECK_EQ(result->at(0).count, spec->at(0).count);
        CHECK_EQ(result->at(1).count, spec->at(1).count);
      }
      result = spec;
    }
  }
  return result;
}

// Structure for parameters relating to the MatMul shape and dimension indices.
//
// Variable naming: lhs [m, k] x rhs [k, n] -> out [m, n].
//
// The logical output dimensions are always ordered as:
//   split-K, batch, non-contracting LHS, non-contracting RHS,
// where split-K and batch are optional.
struct MatMulDims {
  static absl::StatusOr<MatMulDims> Create(
      const TritonGemmConfig& config, const HloDotInstruction& dot,
      const TritonFusionAnalysis& analysis);

  std::optional<int> out_split_k_dim_idx = std::nullopt;

  std::optional<int> lhs_batch_dim_idx = std::nullopt;
  std::optional<int> rhs_batch_dim_idx = std::nullopt;
  std::optional<int> out_batch_dim_idx = std::nullopt;

  // The LHS non-contracting can be split into two.
  std::optional<int64_t> lhs_noncontracting_split = std::nullopt;

  int lhs_contracting_dim_idx;
  int lhs_noncontracting_dim_idx;
  int rhs_contracting_dim_idx;
  int rhs_noncontracting_dim_idx;
  // The index of the LHS noncontracting dim in the output.
  int out_lhs_noncontracting_dim_idx;
  // The index of the RHS noncontracting dim in the output.
  int out_rhs_noncontracting_dim_idx;

  int64_t m;
  int64_t n;
  int64_t k;

 private:
  MatMulDims() = default;
};

// Structure for parameters relating to the MatMul launch grid.
struct MatMulLaunchConfig {
  explicit MatMulLaunchConfig(const TritonGemmConfig& config,
                              const HloDotInstruction& dot,
                              const MatMulDims& dims,
                              const se::DeviceDescription& device_info);

  int64_t grid_m;
  int64_t grid_n;
  LaunchDimensions launch_dims;
  mt::ProgramIDDim batch_program_id_dim;
  mt::ProgramIDDim noncontracting_program_id_dim;
};

/*static*/ absl::StatusOr<MatMulDims> MatMulDims::Create(
    const TritonGemmConfig& config, const HloDotInstruction& dot,
    const TritonFusionAnalysis& analysis) {
  MatMulDims matmul_dims;
  if (config.split_k > 1) {
    // split-k is always the first logical dimension.
    matmul_dims.out_split_k_dim_idx = 0;
  }

  int64_t num_split_k_dims = config.split_k > 1 ? 1 : 0;
  const auto& dims = dot.dot_dimension_numbers();
  matmul_dims.lhs_contracting_dim_idx = dims.lhs_contracting_dimensions(0);
  matmul_dims.lhs_noncontracting_dim_idx =
      GetNonContractingDims(dot.operand(0)->shape(),
                            dims.lhs_batch_dimensions(),
                            dims.lhs_contracting_dimensions())
          .value()[0];
  matmul_dims.rhs_contracting_dim_idx = dims.rhs_contracting_dimensions(0);
  matmul_dims.rhs_noncontracting_dim_idx =
      GetNonContractingDims(dot.operand(1)->shape(),
                            dims.rhs_batch_dimensions(),
                            dims.rhs_contracting_dimensions())
          .value()[0];

  if (dims.lhs_batch_dimensions_size() > num_split_k_dims) {
    matmul_dims.lhs_batch_dim_idx = *dims.lhs_batch_dimensions().rbegin();
    matmul_dims.rhs_batch_dim_idx = *dims.rhs_batch_dimensions().rbegin();
    // The batch dimension (if present) comes after the split-k dimension (if
    // present, otherwise it's the first dimension).
    matmul_dims.out_batch_dim_idx = num_split_k_dims;
  }

  // Logical output dimensions are always ordered as:
  //   split-K, batch, non-contracting LHS, non-contracting RHS,
  // where split-K and batch are optional.
  matmul_dims.out_rhs_noncontracting_dim_idx = dot.shape().rank() - 1;
  matmul_dims.out_lhs_noncontracting_dim_idx = dot.shape().rank() - 2;

  auto* root = dot.parent()->root_instruction();
  auto iter_spec =
      analysis.IterSpec(TritonFusionAnalysis::Scope::OUTPUT, root,
                        matmul_dims.out_rhs_noncontracting_dim_idx);
  TF_RET_CHECK(iter_spec != nullptr);
  matmul_dims.n = iter_spec->at(0).count;
  // Contracting dimension length.
  if (config.split_k > 1 &&
      dot.operand(1)->operand(0)->opcode() == HloOpcode::kPad) {
    // Unpadded LHS shape:  [..., k, ...]
    // Padded LHS shape:    [..., padded_k, ...]
    // Bitcasted LHS shape: [..., split_k, padded_k / split_k, ...]
    TF_RET_CHECK(dot.operand(1)->opcode() == HloOpcode::kBitcast);
    const Shape& unpadded_rhs_shape =
        dot.operand(1)->operand(0)->operand(0)->shape();
    matmul_dims.k =
        unpadded_rhs_shape.dimensions(dims.rhs_contracting_dimensions(0) - 1);
  } else {
    matmul_dims.k =
        dot.operand(1)->shape().dimensions(dims.rhs_contracting_dimensions(0)) *
        config.split_k;
  }

  auto* lhs_noncontracting_split_spec = GetLhsNoncontractingSplitSpec(
      analysis, matmul_dims.lhs_noncontracting_dim_idx);
  if (lhs_noncontracting_split_spec != nullptr) {
    // Just the fastest-varying part of it if the dimension is split.
    matmul_dims.m = lhs_noncontracting_split_spec->at(0).count;
    matmul_dims.lhs_noncontracting_split =
        lhs_noncontracting_split_spec->at(1).count;
  } else {
    matmul_dims.m = analysis
                        .IterSpec(TritonFusionAnalysis::Scope::OUTPUT, root,
                                  matmul_dims.out_lhs_noncontracting_dim_idx)
                        ->at(0)
                        .count;
  }

  // For now split non-contracting and batch are not supported
  // simultaneously because they are implemented via same mechanism.
  TF_RET_CHECK(!(matmul_dims.out_batch_dim_idx.has_value() &&
                 matmul_dims.lhs_noncontracting_split.has_value()));

  TF_RET_CHECK(matmul_dims.m >= 1);
  TF_RET_CHECK(matmul_dims.n >= 1);
  return std::move(matmul_dims);
}

MatMulLaunchConfig::MatMulLaunchConfig(const TritonGemmConfig& config,
                                       const HloDotInstruction& dot,
                                       const MatMulDims& dims,
                                       const se::DeviceDescription& device_info)
    : grid_m((dims.m + config.block_m - 1) / config.block_m),
      grid_n((dims.n + config.block_n - 1) / config.block_n) {
  int64_t batch_size = dims.lhs_noncontracting_split.value_or(
      dims.out_batch_dim_idx.has_value()
          ? dot.shape().dimensions(*dims.out_batch_dim_idx)
          : 1);
  // X block size is 32-bit, Y and Z are 16-bit. Use X for large dimensions.
  constexpr int64_t kBlockCountYZLimit = 65536;

  // In the imaginary situation where both batch size and grid_m * grid_n
  // are over 65535 we have to give up. Given the minimal m, n block sizes of 16
  // this requires at least 256 GB of output.
  CHECK_LT(batch_size * grid_m * grid_n,
           kBlockCountYZLimit * kBlockCountYZLimit);

  const bool large_batch = batch_size >= kBlockCountYZLimit;
  if (large_batch) {
    batch_program_id_dim = mt::ProgramIDDim::X;
    noncontracting_program_id_dim = mt::ProgramIDDim::Y;
    launch_dims = LaunchDimensions(
        se::BlockDim(batch_size, grid_m * grid_n, config.split_k),
        se::ThreadDim(config.num_warps * WarpSize(device_info), 1, 1));
  } else {
    batch_program_id_dim = mt::ProgramIDDim::Y;
    noncontracting_program_id_dim = mt::ProgramIDDim::X;
    launch_dims = LaunchDimensions(
        se::BlockDim(grid_m * grid_n, batch_size, config.split_k),
        se::ThreadDim(config.num_warps * WarpSize(device_info), 1, 1));
  }
}

absl::Status ValidateMatMulConfig(const TritonGemmConfig& config,
                                  const HloDotInstruction& dot) {
  TF_RET_CHECK(config.split_k >= 1);
  TF_RET_CHECK(config.block_m >= 16);
  TF_RET_CHECK(config.block_k >= 16);
  TF_RET_CHECK(config.block_n >= 16);

  const auto& dims = dot.dot_dimension_numbers();
  int num_batch_dims =
      dims.lhs_batch_dimensions_size() - (config.split_k > 1 ? 1 : 0);
  TF_RET_CHECK(num_batch_dims <= 1);
  if (config.split_k > 1) {
    // Split-K dimension has to be the first batch one and have an index
    // just before the contracting one.
    const int lhs_split_k_dim_idx = dims.lhs_contracting_dimensions(0) - 1;
    const int rhs_split_k_dim_idx = dims.rhs_contracting_dimensions(0) - 1;
    // Size of this dimension has to match the split_k value.
    TF_RET_CHECK(dims.lhs_batch_dimensions(0) == lhs_split_k_dim_idx);
    TF_RET_CHECK(dims.rhs_batch_dimensions(0) == rhs_split_k_dim_idx);
    TF_RET_CHECK(config.split_k ==
                 dot.operand(0)->shape().dimensions(lhs_split_k_dim_idx));
    TF_RET_CHECK(config.split_k ==
                 dot.operand(1)->shape().dimensions(rhs_split_k_dim_idx));
  }

  // Rely on dot decomposer: there is just one contracting and one
  // non-contracting dimension on each side + batch ones optionally.
  TF_RET_CHECK(dims.lhs_contracting_dimensions_size() == 1);
  TF_RET_CHECK(dims.rhs_contracting_dimensions_size() == 1);

  TF_RET_CHECK(dot.operand(0)->shape().rank() ==
               2 + (config.split_k > 1 ? 1 : 0) + num_batch_dims);
  return absl::OkStatus();
}

// if (index < limits[0]) {
//   return choices[0];
// } else if (index < limits[1]) {
//   return choices[1];
// } else if (...) {
// ...
// } else {
//   return choices.back();
// }
absl::StatusOr<Value> EmitMultiSelect(ImplicitLocOpBuilder b, Value index,
                                      ValueRange limits, ValueRange choices) {
  TF_RET_CHECK(choices.size() - 1 == limits.size());
  Value result = choices[0];
  for (int i = 0; i < choices.size() - 1; ++i) {
    result = b.create<ma::SelectOp>(
        b.create<ma::CmpIOp>(ma::CmpIPredicate::slt, index, limits[i]), result,
        choices[i + 1]);
  }
  return result;
}

absl::Status UncompilableMatmul(absl::string_view explanation) {
  absl::Status s = absl::CancelledError(explanation);
  s.SetPayload(kUncompilableFusion, absl::Cord(explanation));
  return s;
}

bool IsFp8Matmul(const HloDotInstruction* dot_instr) {
  return absl::c_all_of(std::array<int, 2>{0, 1}, [&](int idx) {
    return primitive_util::IsF8Type(
        dot_instr->operand(idx)->shape().element_type());
  });
}

class MatMulEmitterHelper {
 public:
  MatMulEmitterHelper(absl::string_view libdevice_path,
                      const se::DeviceDescription& device_info,
                      const HloDotInstruction* dot_instr,
                      ImplicitLocOpBuilder& b, Type index_ty, MatMulDims dims,
                      const MatMulLaunchConfig& launch_config,
                      const TritonFusionAnalysis& analysis)
      : b_(b),
        libdevice_path_(libdevice_path),
        device_info_(device_info),
        dot_instr_(dot_instr),
        index_ty_(index_ty),
        analysis_(analysis),
        dims_(dims),
        launch_config_(launch_config) {}

  // TODO(b/266862493): Accumulator can be integer too.
  // Otherwise only f64 x f64 -> f64 uses f64 accumulator.
  absl::StatusOr<mlir::FloatType> GetDotAccumulatorType() {
    const PrecisionConfig::Algorithm algorithm =
        dot_instr_->precision_config().algorithm();

    if (algorithm == PrecisionConfig::ALG_UNSET) {
      TF_ASSIGN_OR_RETURN(Type dot_output_ty,
                          TritonType(b_, dot_instr_->shape().element_type()));
      // The code below assumes that lhs and rhs have the same type. However
      // it's not always the case with fp8 matmuls, e.g. e4m3×e5m2 is supported
      // at the hardware level. NVidia GPU currently only supports f32
      // accumulator for such matmuls.
      if (IsFp8Matmul(dot_instr_)) {
        return b_.getF32Type();
      }

      // Data type of dot() immediate inputs.
      TF_ASSIGN_OR_RETURN(
          const Type lhs_ty,
          TritonType(b_, dot_instr_->operand(0)->shape().element_type()));
      TF_ASSIGN_OR_RETURN(
          const Type rhs_ty,
          TritonType(b_, dot_instr_->operand(1)->shape().element_type()));
      TF_RET_CHECK(lhs_ty == rhs_ty);
      Type dot_input_ty = lhs_ty;
      // TODO(b/266862493): Accumulator can be integer too.
      // Otherwise only f64 x f64 -> f64 uses f64 accumulator.
      return (dot_output_ty.isF64() && dot_input_ty.isF64()) ? b_.getF64Type()
                                                             : b_.getF32Type();
    }

    absl::StatusOr<PrimitiveType> accum_type =
        algorithm_util::GetDotAccumulatorType(algorithm);
    CHECK(accum_type.ok()) << "Unexpected algorithm: "
                           << PrecisionConfig::Algorithm_Name(algorithm);
    TF_ASSIGN_OR_RETURN(Type mlir_accum_type,
                        TritonType(b_, accum_type.value()));
    if (auto float_accum_type =
            mlir::dyn_cast<mlir::FloatType>(mlir_accum_type)) {
      return float_accum_type;
    }
    LOG(FATAL) << "Only floating point accumulator types are supported for "
                  "now, but we got: "
               << llvm_ir::DumpToString(mlir_accum_type);
  }

  std::vector<const HloInstruction*> EpiloguePostOrderTransitiveOperands(
      const HloInstruction* root) {
    // Collect all instructions of the dot's output scope.
    absl::flat_hash_set<const HloInstruction*> to_order;
    {
      std::queue<const HloInstruction*> to_add;
      if (root != dot_instr_) {
        to_add.push(root);
      }
      while (!to_add.empty()) {
        const HloInstruction* current = to_add.front();
        for (const HloInstruction* operand : current->operands()) {
          if (!to_order.contains(operand)) {
            if (operand != dot_instr_) {
              to_add.push(operand);
            }
          }
        }
        to_order.insert(current);
        to_add.pop();
      }
    }
    // Order them producers before consumers.
    std::vector<const HloInstruction*> to_emit;
    for (const HloInstruction* hlo :
         dot_instr_->parent()->MakeInstructionPostOrder()) {
      if (to_order.contains(hlo)) {
        to_emit.push_back(hlo);
      }
    }
    return to_emit;
  }

  Value MakeInput(const Side& side, int64_t operand_index,
                  absl::flat_hash_map<const HloInstruction*, Value>& values) {
    return *EmitScope(
        b_, libdevice_path_, device_info_, &analysis_, side,
        dot_instr_->parent()->MakeInstructionPostOrderFrom(
            const_cast<HloInstruction&>(*dot_instr_->operand(operand_index))),
        values);
  }

  int64_t GetNonContractingDimIdxForOperandScope(
      TritonFusionAnalysis::Scope scope) {
    if (scope == TritonFusionAnalysis::Scope::LHS) {
      return dims_.lhs_noncontracting_dim_idx;
    } else if (scope == TritonFusionAnalysis::Scope::RHS) {
      return dims_.rhs_noncontracting_dim_idx;
    } else {
      CHECK(false) << "This shouldn't be called for the output scope.";
    }
  }

  // Return the batch stride of the HLO passed as a parameter. If the
  // parameter HLO has no batch dimension, a zero stride is returned.
  // Also sets offset_batch and updates has_batch_offset as a side effect.
  absl::StatusOr<Value> GetBatchStride(const Side& side,
                                       const HloInstruction* hlo_param,
                                       int64_t& offset_batch,
                                       bool& has_batch_offset) {
    int64_t stride_batch = 0;
    if (side.scope != TritonFusionAnalysis::Scope::RHS &&
        dims_.lhs_noncontracting_split) {
      const TensorIterationSpec::DimIterationSpec* spec =
          analysis_.IterSpec(side.scope, hlo_param, side.tiled_dims[0].index);
      if (spec != nullptr) {
        if (spec->size() > 1) {
          // Support one specific kind of output transpose that splits the
          // dimension originating from the split LHS non-contracting one.
          stride_batch = spec->at(1).stride;
        } else {
          // Because the major part of the split is implemented using the
          // batch logic stride_batch is populated here as the stride of
          // the minor part times its size.
          stride_batch = spec->at(0).stride *
                         (spec->at(0).count / *dims_.lhs_noncontracting_split);
        }
        TF_RET_CHECK(stride_batch != 0);
      }
    } else if (side.batch_dim_idx.has_value()) {
      const TensorIterationSpec::DimIterationSpec* spec =
          analysis_.IterSpec(side.scope, hlo_param, *side.batch_dim_idx);
      if (spec != nullptr) {
        stride_batch = spec->at(0).stride;
        offset_batch = spec->at(0).slice_start;
        TF_RET_CHECK(stride_batch != 0);
      }
    }

    has_batch_offset |= stride_batch != 0;
    return Cst(stride_batch);
  }

  // bases: The base pointers of each argument.
  absl::StatusOr<Value> EmitTensorPointer(
      const HloInstruction* hlo, const Side& side, ValueRange bases,
      Value pid_k, std::vector<int32_t>& boundary_checks) {
    // Parameters of MakeTensorPtrOp to be generated by this function.
    Value base;
    std::vector<Value> bounds;
    std::vector<Value> strides;
    std::vector<int32_t> strides_sizes;  // We use it to detect the minor dim.
    // Offsets from tensor origin, same for all thread blocks.
    std::vector<Value> tensor_offsets;
    std::vector<int32_t> block_dims;
    std::vector<int32_t> dim_order;

    // Offsets for a given thread block, typically pid * block size.
    // Used in a one-off AdvanceOp applied to the generated MakeTensorPtrOp.
    std::vector<Value> block_offsets;

    // Concatenations of parameters are handled during generation of block
    // pointers because of a limitation of implementation of block pointers
    // in the Triton compiler: block pointers are not supported inside
    // conditionals.
    // Therefore instead of directly using a conditional to emit a concatenation
    // and emitting its inputs inside the cases a single block pointer is
    // emitted for all inputs, but all its properties (base, strides etc) get
    // generated conditionally on the position of the current thread block
    // within the concatenated dimension.

    // Index of concatenated dimension if present, -1 otherwise.
    int concat_dim_idx;
    // Offsets along the concatenated dimension at which operands change.
    std::vector<Value> concat_boundaries;
    // Block index along the concatenated dimension * block size.
    Value concat_dim_pid_offset;

    if (hlo->opcode() == HloOpcode::kConcatenate) {
      // For now only non-contracting dimension can be concatenated.
      concat_dim_idx = (side.scope == TritonFusionAnalysis::Scope::LHS)
                           ? dims_.lhs_noncontracting_dim_idx
                           : dims_.rhs_noncontracting_dim_idx;
      const DimProperties& properties = [&] {
        for (const DimProperties& dim : side.tiled_dims) {
          if (dim.index == concat_dim_idx) {
            return dim;
          }
        }
        LOG(FATAL) << "Missing dimension.";
      }();
      TF_RET_CHECK(bases.size() == hlo->operand_count());

      concat_boundaries.reserve(hlo->operand_count() - 1);
      for (int i = 0; i < hlo->operand_count() - 1; ++i) {
        const TensorIterationSpec::IterationSpecFragment& fragment =
            analysis_.IterSpec(side.scope, hlo->operand(i), concat_dim_idx)
                ->at(0);
        if (fragment.sliced_count % properties.block_size != 0) {
          return UncompilableMatmul(
              "Operand is not divisible by the block size.");
        }
        concat_boundaries.push_back(
            Cst32(-fragment.slice_start + fragment.sliced_count));
      }

      concat_dim_pid_offset =
          b_.create<ma::MulIOp>(properties.pid, Cst32(properties.block_size));
      TF_ASSIGN_OR_RETURN(base, EmitMultiSelect(b_, concat_dim_pid_offset,
                                                concat_boundaries, bases));
    } else {
      concat_dim_idx = -1;
      base = bases[0];
    }

    auto add_dim = [&](const DimProperties& properties) -> absl::Status {
      if (analysis_.IterSpec(side.scope, hlo, properties.index) == nullptr) {
        return absl::OkStatus();
      }
      Value pid_offset =
          (properties.pid == nullptr)
              ? Cst32(0)
              : b_.create<ma::MulIOp>(properties.pid,
                                      Cst32(properties.block_size));
      std::vector<const HloInstruction*> inputs;
      if (hlo->opcode() == HloOpcode::kConcatenate) {
        inputs.insert(inputs.end(), hlo->operands().cbegin(),
                      hlo->operands().cend());
      } else {
        inputs = {hlo};
      }
      std::vector<const TensorIterationSpec::DimIterationSpec*> specs;
      std::vector<Value> input_strides;
      std::vector<Value> input_offsets;
      std::vector<Value> input_bounds;
      specs.reserve(inputs.size());
      input_strides.reserve(inputs.size());
      input_offsets.reserve(inputs.size());
      input_bounds.reserve(inputs.size());
      for (const HloInstruction* input : inputs) {
        specs.push_back(
            analysis_.IterSpec(side.scope, input, properties.index));
        const auto stride = specs.back()->at(0).stride;
        strides_sizes.push_back(stride);
        input_strides.push_back(Cst64(stride));
        input_offsets.push_back(b_.create<ma::AddIOp>(
            pid_offset, Cst32(specs.back()->at(0).slice_start)));
        input_bounds.push_back(Cst64(specs.back()->at(0).count));
      }
      TF_ASSIGN_OR_RETURN(Value select_value,
                          EmitMultiSelect(b_, concat_dim_pid_offset,
                                          concat_boundaries, input_strides));
      strides.push_back(select_value);
      if (properties.index == concat_dim_idx) {
        TF_ASSIGN_OR_RETURN(
            select_value,
            EmitMultiSelect(b_, pid_offset, concat_boundaries, input_offsets));
        block_offsets.push_back(select_value);
        TF_ASSIGN_OR_RETURN(
            select_value,
            EmitMultiSelect(b_, pid_offset, concat_boundaries, input_bounds));
        bounds.push_back(select_value);
        tensor_offsets.push_back(Cst32(specs.front()->at(0).slice_start));
      } else if (hlo->opcode() == HloOpcode::kDynamicSlice &&
                 (side.scope == TritonFusionAnalysis::Scope::LHS ||
                  side.scope == TritonFusionAnalysis::Scope::RHS) &&
                 properties.index ==
                     GetNonContractingDimIdxForOperandScope(side.scope)) {
        // Here we compute the offset of where we should read the slice from.
        // TODO(b/323255699): Add support for slices of the contracting dim.
        // Dynamic slices are guaranteed to only be offset along the majormost
        // dimension.

        // The only fragment of the non-contracting dim of the dot's input in
        // the current scope:
        TF_RET_CHECK(specs.back()->size() == 1);
        const TensorIterationSpec::IterationSpecFragment
            only_fragment_of_nc_dim = specs.back()->at(0);
        // The majormost dim index in the dynamic slice's output.
        const int majormost_dim = hlo->shape().layout().minor_to_major().back();

        // dynamic slice operands are (input, start_index0, start_index1, ...)
        // so the start index corresponding to the ith dimension is bases[i+1].
        Value majormost_dim_start_index_ptr_val = bases[majormost_dim + 1];
        Value majormost_dim_start_index_val = b_.create<mt::LoadOp>(
            majormost_dim_start_index_ptr_val, mt::CacheModifier::NONE,
            mt::EvictionPolicy::NORMAL,
            /*isVolatile=*/false);
        int64_t majormost_dim_start_index_upper_limit =
            hlo->operand(0)->shape().dimensions(majormost_dim) -
            hlo->dynamic_slice_sizes().at(majormost_dim);
        // We don't want to cast S64 indices to S32, because that could result
        // in an incorrect value.
        if (majormost_dim_start_index_val.getType().isInteger() &&
            majormost_dim_start_index_val.getType().getIntOrFloatBitWidth() ==
                64) {
          return UncompilableMatmul(
              "64 bit dynamic-slice indices are not supported yet.");
        }
        majormost_dim_start_index_val =
            Cast(b_, majormost_dim_start_index_val, b_.getI32Type());
        majormost_dim_start_index_val =
            b_.create<ma::MaxSIOp>(majormost_dim_start_index_val, Cst32(0));
        majormost_dim_start_index_val = b_.create<ma::MinSIOp>(
            majormost_dim_start_index_val,
            Cst32(majormost_dim_start_index_upper_limit));

        // How many "rows" (non-contracting dim values) are there in a slice of
        // size 1?
        int64_t rows_per_majormost_dim = 1;
        for (int i = 0; i < hlo->shape().dimensions().size() - 1; ++i) {
          rows_per_majormost_dim *= hlo->shape().dimensions_minor(i);
        }
        rows_per_majormost_dim =
            rows_per_majormost_dim / only_fragment_of_nc_dim.stride;
        Value rows_per_majormost_dim_val = Cst32(rows_per_majormost_dim);

        Value tensor_offset_val_i32 = b_.create<ma::MulIOp>(
            majormost_dim_start_index_val, rows_per_majormost_dim_val);
        tensor_offsets.push_back(tensor_offset_val_i32);

        // tt.make_tensor_ptr expects an i64 for shape and size, but expects
        // i32 for offsets. We extend the offset to calculate the upper bound.
        Value tensor_offset_val_i64 =
            b_.create<ma::ExtSIOp>(i64_ty_, tensor_offset_val_i32);
        Value sliced_count_val = Cst64(only_fragment_of_nc_dim.sliced_count);
        Value upper_bound_val =
            b_.create<ma::AddIOp>(tensor_offset_val_i64, sliced_count_val);
        bounds.push_back(upper_bound_val);

        block_offsets.push_back(pid_offset);
      } else {
        tensor_offsets.push_back(Cst32(specs.front()->at(0).slice_start));
        block_offsets.push_back(pid_offset);
        int64_t dim_bound = specs.front()->at(0).count;
        if (side.scope == TritonFusionAnalysis::Scope::OUTPUT &&
            properties.index == dims_.out_lhs_noncontracting_dim_idx &&
            specs.front()->size() == 1 &&
            dims_.lhs_noncontracting_split.has_value()) {
          // Dimension of the output produced by the non-contracting LHS one
          // is logically split, major part is addressed using pid_batch.
          dim_bound /= *dims_.lhs_noncontracting_split;
        }
        bounds.push_back(Cst64(dim_bound));
        if (dim_bound % (properties.block_size * properties.split_value) != 0) {
          boundary_checks.push_back(bounds.size() - 1);
        }
        if (hlo->shape().element_type() == PrimitiveType::S4) {
          // For s4 type we need to divide the minor dim bound by 2 because it
          // is the packing dimension. But if the minor dim has length == 1 then
          // the major dim stride is also 1 and it is the packing dimension.
          if (strides_sizes.back() == 1) {
            // For the odd bounds we need to add 1 in advance.
            // Otherwise we will loose the last element.
            bounds[bounds.size() - 1] = Cst64((dim_bound + 1) / 2);
          } else {
            int last_stride_index = strides.size() - 1;
            strides[last_stride_index] =
                b_.create<ma::DivSIOp>(strides[last_stride_index], Cst64(2));
          }
        }
      }
      block_dims.push_back(properties.block_size);
      dim_order.emplace(dim_order.begin(), dim_order.size());
      return absl::OkStatus();
    };

    for (const DimProperties& dim : side.tiled_dims) {
      TF_RETURN_IF_ERROR(add_dim(dim));
    }

    int64_t offset_batch = 0;
    bool has_batch_offset = false;
    Value batch_stride;

    if (hlo->opcode() == HloOpcode::kConcatenate) {
      std::vector<Value> batch_strides;
      batch_strides.reserve(hlo->operands().size());
      for (const HloInstruction* operand : hlo->operands()) {
        TF_ASSIGN_OR_RETURN(
            Value op_stride,
            GetBatchStride(side, operand, offset_batch, has_batch_offset));
        batch_strides.push_back(op_stride);
      }
      TF_ASSIGN_OR_RETURN(batch_stride,
                          EmitMultiSelect(b_, concat_dim_pid_offset,
                                          concat_boundaries, batch_strides));
    } else {
      TF_ASSIGN_OR_RETURN(batch_stride, GetBatchStride(side, hlo, offset_batch,
                                                       has_batch_offset));
    }

    // Avoid generating logic to compute batch offset if unnecessary.
    if (has_batch_offset) {
      Value pid_batch =
          b_.create<mt::GetProgramIdOp>(launch_config_.batch_program_id_dim);

      Value pid_offset_batch = b_.create<ma::MulIOp>(
          b_.create<ma::AddIOp>(Cst(offset_batch), ConvertScalar(pid_batch)),
          batch_stride);

      if (hlo->shape().element_type() == PrimitiveType::S4) {
        pid_offset_batch = b_.create<ma::DivSIOp>(pid_offset_batch, Cst(2));
      }
      base = AddPtr(b_, base, pid_offset_batch);
    }

    if (dims_.out_split_k_dim_idx.has_value()) {
      const TensorIterationSpec::DimIterationSpec* spec = analysis_.IterSpec(
          TritonFusionAnalysis::Scope::OUTPUT, hlo, *dims_.out_split_k_dim_idx);
      if (spec != nullptr) {
        TF_RET_CHECK(pid_k != nullptr);
        base = AddPtr(b_, base,
                      b_.create<ma::MulIOp>(ConvertScalar(pid_k),
                                            Cst(spec->at(0).stride)));
      }
    }

    if (block_dims.empty()) {
      // Load of a scalar.
      return base;
    }
    auto tensor_ptr = mlir::cast<Value>(
        b_.create<mt::MakeTensorPtrOp>(base, bounds, strides, tensor_offsets,
                                       block_dims, dim_order)
            .getResult());
    tensor_ptr = b_.create<mt::AdvanceOp>(tensor_ptr.getType(), tensor_ptr,
                                          block_offsets);
    return tensor_ptr;
  }

 private:
  // Extend int32 indexes to int64, if necessary.
  Value ConvertScalar(Value value) {
    if (index_ty_.getIntOrFloatBitWidth() == 64) {
      return b_.create<ma::ExtSIOp>(index_ty_, value);
    }
    return value;
  }

  Value Cst(int64_t v) { return CreateConst(b_, index_ty_, v); }
  Value Cst32(int32_t v) { return CreateConst(b_, i32_ty_, v); }
  Value Cst64(int64_t v) { return CreateConst(b_, i64_ty_, v); }

  ImplicitLocOpBuilder& b_;
  absl::string_view libdevice_path_;
  const se::DeviceDescription& device_info_;
  const HloDotInstruction* dot_instr_;
  Type index_ty_;
  TritonFusionAnalysis analysis_;
  MatMulDims dims_;
  MatMulLaunchConfig launch_config_;
  Type i32_ty_ = b_.getI32Type();
  Type i64_ty_ = b_.getI64Type();
};

}  // namespace

absl::StatusOr<LaunchDimensions> GetMatMulLaunchDimensions(
    const TritonFusionAnalysis& analysis, const HloFusionAdaptor& fusion,
    const TritonGemmConfig& config, const se::DeviceDescription& device_info) {
  auto dot = HloBfsFindIf(fusion.GetRoots(), fusion, [](auto node) {
    return node.opcode() == HloOpcode::kDot;
  });
  TF_RET_CHECK(dot != std::nullopt);
  const auto& dot_instr =
      *static_cast<const HloDotInstruction*>(&dot->instruction());
  TF_ASSIGN_OR_RETURN(MatMulDims dims,
                      MatMulDims::Create(config, dot_instr, analysis));
  MatMulLaunchConfig launch_config(config, dot_instr, dims, device_info);
  return launch_config.launch_dims;
}

absl::StatusOr<SmallVector<Value>> GetArguments(mlir::triton::FuncOp fn,
                                                const HloInstruction& input) {
  if (input.opcode() == HloOpcode::kParameter) {
    return {{fn.getArgument(input.parameter_number())}};
  } else if (input.opcode() == HloOpcode::kConcatenate ||
             input.opcode() == HloOpcode::kDynamicSlice) {
    // As defined in GemmFusion, all inputs of concatenate and dynamic slice are
    // parameters.
    SmallVector<Value> result;
    for (const HloInstruction* operand : input.operands()) {
      TF_RET_CHECK(operand->opcode() == HloOpcode::kParameter);
      result.push_back(fn.getArgument(operand->parameter_number()));
    }
    return result;
  }
  LOG(FATAL) << "Unexpected opcode: " << input.opcode();
}

// Concatenations can currently only be applied directly to parameters;
// all concatenated parameters share the same block pointer. This function
// returns all inputs of a kernel: concatenations of parameters and standalone
// parameters.
ConstHloInstructionSet ScopeInputs(const TritonFusionAnalysis& analysis,
                                   const TritonFusionAnalysis::Scope scope) {
  ConstHloInstructionSet result;
  for (const HloInstruction* parameter : analysis.ScopeParameters(scope)) {
    if (absl::c_any_of(parameter->users(), [](const HloInstruction* user) {
          return user->opcode() == HloOpcode::kConcatenate ||
                 user->opcode() == HloOpcode::kDynamicSlice;
        })) {
      // Concatenation is always the only user of its parameters by
      // construction.
      CHECK_EQ(parameter->users().size(), 1);
      for (const HloInstruction* operand : parameter->users()[0]->operands()) {
        // All operands of a concatenation have to be computation parameters.
        CHECK_EQ(operand->opcode(), HloOpcode::kParameter);
      }
      result.insert(parameter->users()[0]);
    } else {
      result.insert(parameter);
    }
  }
  return result;
}

// Truncates |input| of F32 type to the number representable in Bf16 toward
// zero.
// It is used for Emit6xBfloat16MatMul.
Value TruncateToBF16TowardsZero(ImplicitLocOpBuilder& b, Value input) {
  ShapedType input_type = mlir::dyn_cast<ShapedType>(input.getType());
  Type input_type_as_i32 = input_type.clone(b.getI32Type());
  Value input_as_i32 = b.create<mt::BitcastOp>(input_type_as_i32, input);
  Value mask = CreateConst<uint32_t>(b, b.getI32Type(), 0xFFFF0000u,
                                     input_type.getShape());
  Value high_bits = b.create<ma::AndIOp>(input_type_as_i32, input_as_i32, mask);

  return b.create<mt::BitcastOp>(input_type, high_bits);
}

// Finds the middle 8 bits of |input|'s mantissa.
// It is used for Emit6xBfloat16MatMul.
Value SoftMiddleEight(ImplicitLocOpBuilder& b, Value input) {
  Value high = TruncateToBF16TowardsZero(b, input);
  return b.create<ma::SubFOp>(input, high);
}

// Finds the low 8 bits of |input|'s mantissa.
// It is used for Emit6xBfloat16MatMul.
Value SoftLowEight(ImplicitLocOpBuilder& b, Value input) {
  // Find the middle bits of the middle bits, and these are the low eight
  // bits.
  return SoftMiddleEight(b, SoftMiddleEight(b, input));
}

// Rounds |input| to BF16 type.
// It is used for Emit6xBfloat16MatMul.
Value RoundToBF16(ImplicitLocOpBuilder& b, Value input) {
  return Cast(b, input, b.getBF16Type());
}

// Checks |input| is finite f32 (not Nan and not infinite).
// It is used for Emit6xBfloat16MatMul and Emit3xBfloat16MatMul.
Value CheckFiniteF32(ImplicitLocOpBuilder& b, Value input) {
  Value positive_inf = CreateConst<float>(
      b, b.getF32Type(), std::numeric_limits<float>::infinity(),
      mlir::cast<ShapedType>(input.getType()).getShape());
  Value abs_input = b.create<mm::AbsFOp>(input);
  return b.create<ma::CmpFOp>(ma::CmpFPredicate::OGT, positive_inf, abs_input);
}

// Leverages BF16 datatype for F32 matmul computation. It follows the guidance
// from https://arxiv.org/pdf/1904.06376.pdf.
absl::StatusOr<Value> Emit6xBfloat16MatMul(ImplicitLocOpBuilder& b, Value lhs,
                                           Value rhs, Value acc) {
  Type f32 = b.getF32Type();
  TF_RET_CHECK(mlir::cast<ShapedType>(lhs.getType()).getElementType() == f32);
  TF_RET_CHECK(mlir::cast<ShapedType>(rhs.getType()).getElementType() == f32);
  TF_RET_CHECK(mlir::cast<ShapedType>(acc.getType()).getElementType() == f32);

  Value lhs_high = RoundToBF16(b, TruncateToBF16TowardsZero(b, lhs));
  Value lhs_middle =
      RoundToBF16(b, TruncateToBF16TowardsZero(b, SoftMiddleEight(b, lhs)));
  Value lhs_low =
      RoundToBF16(b, TruncateToBF16TowardsZero(b, SoftLowEight(b, lhs)));

  Value rhs_high = RoundToBF16(b, TruncateToBF16TowardsZero(b, rhs));
  Value rhs_middle =
      RoundToBF16(b, TruncateToBF16TowardsZero(b, SoftMiddleEight(b, rhs)));
  Value rhs_low =
      RoundToBF16(b, TruncateToBF16TowardsZero(b, SoftLowEight(b, rhs)));

  auto bf16_dot = [&](Value lhs_bf16, Value rhs_bf16,
                      Value accumulator) -> Value {
    return b.create<mt::DotOp>(lhs_bf16, rhs_bf16, accumulator,
                               /*inputPrecision=*/mt::InputPrecision::IEEE,
                               /*maxNumImpreciseAcc=*/0);
  };

  Value local_acc = ZerosLike(b, acc);
  Value result = bf16_dot(lhs_middle, rhs_middle, local_acc);
  result = bf16_dot(lhs_low, rhs_high, result);
  result = bf16_dot(lhs_high, rhs_low, result);
  result = bf16_dot(lhs_middle, rhs_high, result);
  result = bf16_dot(lhs_high, rhs_middle, result);
  // If lhs is 1.0, we will have lhs_high = 1.0 and lhs_low = 0.0.
  // If rhs is +infinity, we will have:
  // +infinity * 1.0 = +infinity
  // +infinity * 0.0 = NaN
  // We would get the wrong result if we sum these partial products. Instead, we
  // must override any accumulated result if the last partial product is
  // non-finite. See b/115844437.
  Value is_finite = CheckFiniteF32(b, result);
  result = b.create<ma::SelectOp>(is_finite, result, ZerosLike(b, result));
  result = bf16_dot(lhs_high, rhs_high, result);
  result = b.create<ma::AddFOp>(acc, result);
  return result;
}

// Compute F32 matmul with 3 BF16 dots. It is less accurate than
// Emit6xBfloat16MatMul.
absl::StatusOr<Value> Emit3xBfloat16MatMul(ImplicitLocOpBuilder& b, Value lhs,
                                           Value rhs, Value acc) {
  Type f32 = b.getF32Type();
  TF_RET_CHECK(mlir::cast<ShapedType>(lhs.getType()).getElementType() == f32);
  TF_RET_CHECK(mlir::cast<ShapedType>(rhs.getType()).getElementType() == f32);
  TF_RET_CHECK(mlir::cast<ShapedType>(acc.getType()).getElementType() == f32);

  Value lhs_high = RoundToBF16(b, TruncateToBF16TowardsZero(b, lhs));
  Value lhs_low = RoundToBF16(b, SoftMiddleEight(b, lhs));

  Value rhs_high = RoundToBF16(b, TruncateToBF16TowardsZero(b, rhs));
  Value rhs_low = RoundToBF16(b, SoftMiddleEight(b, rhs));

  auto bf16_dot = [&](Value lhs_bf16, Value rhs_bf16,
                      Value accumulator) -> Value {
    return b.create<mt::DotOp>(lhs_bf16, rhs_bf16, accumulator,
                               /*inputPrecision=*/mt::InputPrecision::IEEE,
                               /*maxNumImpreciseAcc=*/0);
  };

  Value local_acc = ZerosLike(b, acc);
  Value result = bf16_dot(lhs_low, rhs_high, local_acc);
  result = bf16_dot(lhs_high, rhs_low, result);
  Value is_finite = CheckFiniteF32(b, result);
  result = b.create<ma::SelectOp>(is_finite, result, ZerosLike(b, result));
  result = bf16_dot(lhs_high, rhs_high, result);
  result = b.create<ma::AddFOp>(acc, result);
  return result;
}

namespace {

bool IsTf32Allowed(const HloDotInstruction* dot_instr) {
  const PrecisionConfig::Algorithm algorithm =
      dot_instr->precision_config().algorithm();

  if (algorithm == PrecisionConfig::ALG_UNSET) {
    return tsl::tensor_float_32_execution_enabled() &&
           absl::c_none_of(dot_instr->precision_config().operand_precision(),
                           [](const int precision) {
                             return precision != PrecisionConfig::DEFAULT;
                           });
  }

  return algorithm_util::HasTf32InputType(algorithm);
}

bool Is6xBfloat16MatMul(const HloDotInstruction* dot_instr,
                        mlir::OpBuilder& builder, Value dot_input_lhs,
                        Value dot_input_rhs,
                        const se::DeviceDescription& device_info) {
  const PrecisionConfig::Algorithm algorithm =
      dot_instr->precision_config().algorithm();

  if (algorithm == PrecisionConfig::ALG_UNSET) {
    const HloModule* hlo_module = dot_instr->GetModule();
    Type f32 = builder.getF32Type();
    return hlo_module->config()
               .debug_options()
               .xla_gpu_enable_bf16_6way_gemm() &&
           mlir::cast<ShapedType>(dot_input_lhs.getType()).getElementType() ==
               f32 &&
           mlir::cast<ShapedType>(dot_input_rhs.getType()).getElementType() ==
               f32;
  }

  return algorithm == PrecisionConfig::ALG_DOT_BF16_BF16_F32_X6;
}

bool Is3xBfloat16MatMul(const HloDotInstruction* dot_instr,
                        mlir::OpBuilder& builder, Value dot_input_lhs,
                        Value dot_input_rhs,
                        const se::DeviceDescription& device_info) {
  const PrecisionConfig::Algorithm algorithm =
      dot_instr->precision_config().algorithm();

  if (algorithm == PrecisionConfig::ALG_UNSET) {
    const HloModule* hlo_module = dot_instr->GetModule();
    Type f32 = builder.getF32Type();
    return hlo_module->config()
               .debug_options()
               .xla_gpu_enable_bf16_3way_gemm() &&
           mlir::cast<ShapedType>(dot_input_lhs.getType()).getElementType() ==
               f32 &&
           mlir::cast<ShapedType>(dot_input_rhs.getType()).getElementType() ==
               f32;
  }

  return algorithm == PrecisionConfig::ALG_DOT_BF16_BF16_F32_X3;
}

// This is a heuristic that serves as a proxy for register usage and code size.
//
// We have noticed that tilings with very long LLVM IR code are both slow to
// compile and slow to run. This can be for example due to register spills. So
// we should skip these tilings to save time. But it's better to skip them
// before the LLVM IR is generated. To do that, we came up with a formula that
// strongly correlates with the LLVM IR size. The formula is the size of the two
// input and the output thread block tiles divided by the number of warps. We
// read https://developer.nvidia.com/blog/cutlass-linear-algebra-cuda/ as a
// reference, and found the formula by trial and error.
//
// To regenerate the limit, we have to run an exhaustive search on all tilings
// for a few different HLOs, printing the runtimes and the heuristic values.
//
// From that, we can find a limit, such that all tilings within alpha *
// optimal_runtime have a heuristic value less than or equal to the limit.
//
// In our measurements, all tilings which were within 1.13 * optimal_runtime had
// a complexity_heuristic_value <= kComplexityHeuristicLimit.
//
// See go/tiling-heuristic for more details.
absl::Status CheckGemmTilingComplexityHeuristic(
    const TritonGemmConfig& config) {
  constexpr int64_t kComplexityHeuristicLimit = 9000;
  int64_t complexity_heuristic_value =
      (config.block_m * config.block_n +
       (config.block_m + config.block_n) * config.block_k) /
      config.num_warps;
  VLOG(2) << "Complexity heuristic: " << complexity_heuristic_value;
  if (complexity_heuristic_value > kComplexityHeuristicLimit) {
    return ResourceExhausted("Tiling complexity heuristic exceeded: %d > %d",
                             complexity_heuristic_value,
                             kComplexityHeuristicLimit);
  }
  return absl::OkStatus();
}

class Scopes {
 public:
  Scopes(ImplicitLocOpBuilder& b, const HloInstruction* dot_instr,
         const TritonFusionAnalysis& analysis, const MatMulDims& dims,
         const TritonGemmConfig& config, const MatMulLaunchConfig launch_config,
         bool is_sparse)
      : lhs_(TritonFusionAnalysis::Scope::LHS),
        rhs_(TritonFusionAnalysis::Scope::RHS),
        out_(TritonFusionAnalysis::Scope::OUTPUT) {
    constexpr int group_m = 8;
    const int64_t width = group_m * launch_config.grid_n;

    auto c32 = [&](int64_t v) { return CreateConst(b, b.getI32Type(), v); };

    auto pid_nc = b.create<mt::GetProgramIdOp>(
        launch_config.noncontracting_program_id_dim);
    pid_k_ = (config.split_k > 1)
                 ? b.create<mt::GetProgramIdOp>(mt::ProgramIDDim::Z)
                 : Value{};

    auto group_id = b.create<ma::DivSIOp>(pid_nc, c32(width));
    ma::ConstantOp group_m_op = c32(group_m);
    auto first_pid_m = b.create<ma::MulIOp>(group_id, group_m_op);
    auto sub0 = b.create<ma::SubIOp>(c32(launch_config.grid_m), first_pid_m);
    auto group_size = b.create<ma::SelectOp>(
        b.create<ma::CmpIOp>(ma::CmpIPredicate::slt, sub0, group_m_op), sub0,
        group_m_op);

    pid_m_ = b.create<ma::AddIOp>(first_pid_m,
                                  b.create<ma::RemSIOp>(pid_nc, group_size));

    pid_n_ = b.create<ma::DivSIOp>(b.create<ma::RemSIOp>(pid_nc, c32(width)),
                                   group_size);

    int lhs_non_contracting_block_size = config.block_m;
    int lhs_contracting_block_size = config.block_k;
    int lhs_unpack_bound_idx = 0;
    if (is_int4_param(analysis, TritonFusionAnalysis::Scope::LHS)) {
      auto minor_dim = std::max(dims.lhs_contracting_dim_idx,
                                dims.lhs_noncontracting_dim_idx);
      auto minor_bound = analysis
                             .IterSpec(TritonFusionAnalysis::Scope::LHS,
                                       dot_instr->operand(0), minor_dim)
                             ->at(0)
                             .count;
      if (minor_bound ==
          1) {  // Assuming that the contracting dimension is major.
        lhs_contracting_block_size /= 2;
        lhs_unpack_bound_idx = 1;
      } else if (dims.lhs_contracting_dim_idx >
                 dims.lhs_noncontracting_dim_idx) {
        // lhs is int4 and the contracting dimension is minor.
        lhs_contracting_block_size /= 2;
        lhs_unpack_bound_idx = 1;
      } else {
        // lhs is int4 and the contracting dimension is major.
        lhs_non_contracting_block_size /= 2;
        lhs_unpack_bound_idx = 0;
      }
    }
    if (is_sparse) {
      lhs_contracting_block_size /= 2;
    }
    lhs_.tiled_dims = {
        DimProperties(dims.lhs_noncontracting_dim_idx, pid_m_,
                      lhs_non_contracting_block_size,
                      /*split_value=*/1),
        DimProperties(dims.lhs_contracting_dim_idx, pid_k_,
                      lhs_contracting_block_size, config.split_k)};
    lhs_.batch_dim_idx = dims.lhs_batch_dim_idx;
    lhs_.unpack_dim_idx = lhs_unpack_bound_idx;

    int rhs_contracting_block_size = config.block_k;
    int rhs_non_contracting_block_size = config.block_n;
    int rhs_unpack_bound_idx = 0;
    if (is_int4_param(analysis, TritonFusionAnalysis::Scope::RHS)) {
      auto minor_dim = std::max(dims.rhs_contracting_dim_idx,
                                dims.rhs_noncontracting_dim_idx);
      auto minor_bound = analysis
                             .IterSpec(TritonFusionAnalysis::Scope::RHS,
                                       dot_instr->operand(1), minor_dim)
                             ->at(0)
                             .count;

      if (minor_bound == 1) {  // rhs is int4 and the _minor_ bound is 1.
        rhs_contracting_block_size /= 2;
      } else if (dims.rhs_contracting_dim_idx >
                 dims.rhs_noncontracting_dim_idx) {
        // rhs is int4 and the contracting dimension is minor.
        rhs_contracting_block_size /= 2;
      } else {
        // rhs is int4 and the contracting dimension is major.
        rhs_non_contracting_block_size /= 2;
        rhs_unpack_bound_idx = 1;
      }
    }
    rhs_.tiled_dims = {
        DimProperties(dims.rhs_contracting_dim_idx, pid_k_,
                      rhs_contracting_block_size, config.split_k),
        DimProperties(dims.rhs_noncontracting_dim_idx, pid_n_,
                      rhs_non_contracting_block_size,
                      /*split_value=*/1)};
    rhs_.batch_dim_idx = dims.rhs_batch_dim_idx;
    rhs_.unpack_dim_idx = rhs_unpack_bound_idx;

    out_.tiled_dims = {DimProperties(dims.out_lhs_noncontracting_dim_idx,
                                     pid_m_, config.block_m,
                                     /*split_value=*/1),
                       DimProperties(dims.out_rhs_noncontracting_dim_idx,
                                     pid_n_, config.block_n,
                                     /*split_value=*/1)};
    out_.batch_dim_idx = dims.out_batch_dim_idx;

    if (is_sparse) {
      meta_ = Side{TritonFusionAnalysis::Scope::META,
                   /*tiled_dims=*/
                   {DimProperties(dims.lhs_noncontracting_dim_idx, pid_m_,
                                  config.block_m,
                                  /*split_value=*/1),
                    DimProperties(dims.lhs_contracting_dim_idx, pid_k_,
                                  config.block_k / 16, config.split_k)},
                   dims.lhs_batch_dim_idx};
    }
  }

  std::vector<const Side*> input_scopes() const {
    if (meta_.has_value()) {
      return {&lhs_, &rhs_, &meta_.value()};
    }
    return {&lhs_, &rhs_};
  }
  const Side& lhs() const { return lhs_; }
  const Side& rhs() const { return rhs_; }
  const Side& out() const { return out_; }
  const std::optional<Side>& meta() const { return meta_; }
  const Value& pid_m() const { return pid_m_; }
  const Value& pid_k() const { return pid_k_; }
  const Value& pid_n() const { return pid_n_; }

  static bool is_int4_param(const TritonFusionAnalysis& analysis,
                            TritonFusionAnalysis::Scope scope) {
    const ConstHloInstructionSet& params = analysis.ScopeParameters(scope);
    return params.size() == 1 &&
           (*params.cbegin())->shape().element_type() == S4;
  }

 private:
  Side lhs_;
  Side rhs_;
  Side out_;
  std::optional<Side> meta_;

  Value pid_m_;
  Value pid_k_;
  Value pid_n_;
};

enum MaskExpandDimension { kMajor = 0, kMinor = 1 };

Value EmitMaskOnInput(ImplicitLocOpBuilder& b,
                      MaskExpandDimension expand_dimension, Value input,
                      int denom, Value k, int64_t dims_k, int64_t block_k,
                      Value pid_k) {
  auto c32 = [&](int64_t v) { return CreateConst(b, b.getI32Type(), v); };
  int size = block_k / denom;
  auto elements_in_tile = b.create<ma::SubIOp>(c32(dims_k / denom), k);
  auto cond =
      b.create<ma::CmpIOp>(ma::CmpIPredicate::slt, elements_in_tile, c32(size));
  auto if_op = b.create<mlir::scf::IfOp>(
      cond, /*thenBranch=*/
      [&](mlir::OpBuilder& builder, mlir::Location loc) {
        ImplicitLocOpBuilder b(loc, builder);
        auto range_k = Range(b, size);
        if (pid_k != nullptr) {
          range_k = b.create<ma::AddIOp>(
              range_k, Splat(b, b.create<ma::MulIOp>(pid_k, c32(size)), size));
        }
        auto ty = mlir::cast<mlir::RankedTensorType>(input.getType());
        TensorValue range_expanded = mlir::cast<TensorValue>(
            b.create<mt::ExpandDimsOp>(range_k, expand_dimension).getResult());
        Value mask = b.create<mt::BroadcastOp>(
            ty.clone(b.getI1Type()),
            b.create<ma::CmpIOp>(ma::CmpIPredicate::slt, range_expanded,
                                 Splat(b, elements_in_tile,
                                       range_expanded.getType().getShape())));
        auto result = b.create<ma::SelectOp>(mask, input, ZerosLike(b, input));
        b.create<mlir::scf::YieldOp>(mlir::ValueRange(result));
      },
      /*elseBranch=*/
      [&](mlir::OpBuilder& b, mlir::Location loc) {
        b.create<mlir::scf::YieldOp>(loc, mlir::ValueRange(input));
      });
  return if_op.getResult(0);
}

}  // namespace

// Variable naming: lhs [m, k] x rhs [k, n] -> out [m, n].
absl::Status EmitMatMul(mlir::OpBuilder builder,
                        absl::string_view libdevice_path,
                        const se::DeviceDescription& device_info,
                        const HloFusionInstruction* fusion,
                        mlir::triton::FuncOp fn, const BlockLevelParameters&) {
  auto backend_config =
      fusion->backend_config<GpuBackendConfig>()->fusion_backend_config();

  if (!backend_config.has_triton_gemm_config()) {
    // TODO(bchetioui): consolidate default parameters. At the moment, these
    // may be constructed in two distinct places.
    LOG(WARNING) << "Using fallback triton GEMM config for op "
                 << fusion->name();
    auto& triton_config = *backend_config.mutable_triton_gemm_config();
    triton_config.set_block_m(64);
    triton_config.set_block_k(64);
    triton_config.set_block_n(64);
    triton_config.set_split_k(1);
    triton_config.set_num_stages(1);
    triton_config.set_num_warps(2);
    triton_config.set_num_ctas(1);
  }

  TF_ASSIGN_OR_RETURN(
      TritonGemmConfig config,
      TritonGemmConfig::FromProto(backend_config.triton_gemm_config()));
  TF_ASSIGN_OR_RETURN(auto analysis,
                      TritonFusionAnalysis::Execute(
                          *fusion->called_computation(), config.split_k));

  TF_RETURN_IF_ERROR(CheckGemmTilingComplexityHeuristic(config));

  const HloComputation* computation = fusion->fused_instructions_computation();
  const HloInstruction* instr =
      hlo_query::GetFirstInstructionWithOpcode(*computation, HloOpcode::kDot);
  const HloDotInstruction* dot_instr = DynCast<HloDotInstruction>(instr);
  bool is_sparse = dot_instr->sparse_operands() > 0;

  // Use 32-bit indexing if addressing any of the inputs or the output (which
  // could grow if split_k is set) does not cross the INT_MAX boundary.
  // Otherwise, fall back to 64-bit indexing, which is slower.
  bool use_64bit_indexing =
      ShapeUtil::ElementsIn(dot_instr->operand(0)->shape()) > INT_MAX ||
      ShapeUtil::ElementsIn(dot_instr->operand(1)->shape()) > INT_MAX ||
      ShapeUtil::ElementsIn(dot_instr->shape()) * config.split_k > INT_MAX;
  Type index_ty = builder.getIntegerType(use_64bit_indexing ? 64 : 32);

  const HloInstruction* root = dot_instr->parent()->root_instruction();
  TF_RET_CHECK(!root->shape().IsTuple());

  // TODO(b/320659359) Allow TF32 for 8-bit or less types with F32.
  bool is_unsupported_bitwidth =
      HloBfsAnyOf({dot_instr}, [&](const HloInstruction* node) {
        if (node->opcode() != HloOpcode::kConvert) {
          return false;
        }
        int in_width =
            primitive_util::BitWidth(node->operand(0)->shape().element_type());
        return in_width <= 8 && node->shape().element_type() == F32;
      });

  // We'll be creating a lot of instructions from a single dot, use an
  // implicit loc builder so we don't have to pass around the location all the
  // time.
  auto loc = mlir::NameLoc::get(builder.getStringAttr(dot_instr->name()));
  ImplicitLocOpBuilder b(loc, builder);

  TF_RETURN_IF_ERROR(ValidateMatMulConfig(config, *dot_instr));
  const int split_k = config.split_k;
  const int block_m = config.block_m;
  const int block_k = config.block_k;
  const int block_n = config.block_n;

  TF_ASSIGN_OR_RETURN(const MatMulDims dims,
                      MatMulDims::Create(config, *dot_instr, analysis));
  const MatMulLaunchConfig launch_config(config, *dot_instr, dims, device_info);
  VLOG(6) << analysis.ToString();

  MatMulEmitterHelper emitter(libdevice_path, device_info, dot_instr, b,
                              index_ty, dims, launch_config, analysis);

  TF_ASSIGN_OR_RETURN(mlir::FloatType acc_ty, emitter.GetDotAccumulatorType());

  ma::ConstantOp accumulator_init =
      CreateConst(b, acc_ty, 0, {block_m, block_n});

  // Parameters are passed to the loop in non-trivial order, these maps help
  // finding them and their attributes.
  absl::flat_hash_map<int, const HloInstruction*> iter_args_to_inputs;
  absl::flat_hash_map<int, std::vector<int32_t>> iter_args_to_boundary_checks;

  // Calculate the sizes of the lhs, rhs, meta, and output sides.
  Scopes scopes(b, dot_instr, analysis, dims, config, launch_config, is_sparse);

  auto c32 = [&](int64_t v) { return CreateConst(b, b.getI32Type(), v); };

  constexpr size_t kLhsMetaOperandIdx = HloDotInstruction::kOperands;
  size_t lsize = ScopeInputs(analysis, TritonFusionAnalysis::Scope::LHS).size();
  size_t rsize = ScopeInputs(analysis, TritonFusionAnalysis::Scope::RHS).size();

  absl::flat_hash_map<const HloInstruction*, Type> triton_type_for_input;
  for (const Side& side : {scopes.lhs(), scopes.rhs()}) {
    for (const HloInstruction* input : ScopeInputs(analysis, side.scope)) {
      TF_ASSIGN_OR_RETURN(Type input_ty,
                          TritonType(b, input->shape().element_type()));
      triton_type_for_input.insert({input, input_ty});
    }
  }

  auto body_builder = [&](mlir::OpBuilder&, mlir::Location, Value ki,
                          ValueRange iter_args) -> void {
    SmallVector<Value> iter_args_next;
    iter_args_next.reserve(iter_args.size());
    std::array<absl::flat_hash_map<const HloInstruction*, Value>, 3> values;

    // Load tiles of all parameters of LHS and RHS scopes and advance pointers.
    for (int i = 0; i < iter_args.size() - 1; ++i) {
      const int index = i < lsize ? 0 : i < lsize + rsize ? 1 : 2;
      const Side& side = *(scopes.input_scopes()[index]);

      const HloInstruction* param_hlo = iter_args_to_inputs[i];
      Type param_ty = index == kLhsMetaOperandIdx
                          ? b.getI16Type()
                          : triton_type_for_input.at(param_hlo);
      Type param_storage_ty = StorageType(b, param_ty);
      Value param_value =
          EmitParameterLoad(b, iter_args[i], iter_args_to_boundary_checks[i]);
      if (param_ty != param_storage_ty) {
        // For example cast i8 to i1.
        param_value = Cast(b, param_value, param_ty);
      }

      CHECK(values[index].insert({param_hlo, param_value}).second);
      SmallVector<Value> increments;
      for (const DimProperties& dim : side.tiled_dims) {
        const TensorIterationSpec::DimIterationSpec* spec =
            analysis.IterSpec(side.scope, iter_args_to_inputs[i], dim.index);
        if (spec == nullptr || spec->at(0).stride == 0) {
          continue;
        }
        // Only the contracting dimensions are advanced.
        if (dim.index == (index == 0 || index == kLhsMetaOperandIdx
                              ? dims.lhs_contracting_dim_idx
                              : dims.rhs_contracting_dim_idx)) {
          increments.push_back(c32(dim.block_size * split_k));
        } else {
          increments.push_back(c32(0));
        }
      }
      if (increments.empty()) {
        iter_args_next.push_back(iter_args[i]);
      } else {
        iter_args_next.push_back(b.create<mt::AdvanceOp>(
            iter_args[i].getType(), iter_args[i], increments));
      }
    }

    // Emit all operations of LHS and RHS scopes.
    Value dot_input_lhs = emitter.MakeInput(scopes.lhs(), 0, values[0]);
    Value dot_input_rhs = emitter.MakeInput(scopes.rhs(), 1, values[1]);
    Value dot_input_meta =
        is_sparse ? emitter.MakeInput(*scopes.meta(), 2, values[2]) : Value{};

    // Operation in the fusion before the dot can alter the elements of the
    // tiles that were zero masked during loads. These have to be zeroed here
    // again just before the dot so that they do not affect the output.
    // Only the K dimension needs masking here because unnecessary elements in
    // the other two get discarded by the masked store at the end.
    const bool need_masking = dims.k % (block_k * split_k) > 0;
    if (need_masking) {
      dot_input_lhs = EmitMaskOnInput(b, MaskExpandDimension::kMajor,
                                      dot_input_lhs, is_sparse ? 2 : 1, ki,
                                      dims.k, block_k, scopes.pid_k());
      dot_input_rhs =
          EmitMaskOnInput(b, MaskExpandDimension::kMinor, dot_input_rhs, 1, ki,
                          dims.k, block_k, scopes.pid_k());
      // Masking the metadata is not necessary, as the inputs are masked
      // (i.e. zeroed out), so the padded metadata can hold any values.
    }

    if (is_sparse) {
      iter_args_next.push_back(b.create<mt::gpu::SparseDotOp>(
          dot_input_lhs, dot_input_rhs, iter_args.back(), dot_input_meta));
      b.create<mlir::scf::YieldOp>(iter_args_next);
      return;
    }

    const HloModule* hlo_module = dot_instr->GetModule();
    if (hlo_module->config().debug_options().xla_gpu_enable_bf16_3way_gemm() &&
        hlo_module->config().debug_options().xla_gpu_enable_bf16_6way_gemm()) {
      LOG(WARNING) << "Both BF16 6way gemm and 3way gemm are enabled."
                   << " Fallback to BF16 6way gemm.";
    }

    Value accumulator_next;
    if (Is6xBfloat16MatMul(dot_instr, b, dot_input_lhs, dot_input_rhs,
                           device_info)) {
      absl::StatusOr<Value> accumulator_next_or = Emit6xBfloat16MatMul(
          b, dot_input_lhs, dot_input_rhs, iter_args.back());
      TF_CHECK_OK(accumulator_next_or.status());
      accumulator_next = accumulator_next_or.value();
    } else if (Is3xBfloat16MatMul(dot_instr, b, dot_input_lhs, dot_input_rhs,
                                  device_info)) {
      absl::StatusOr<Value> accumulator_next_or = Emit3xBfloat16MatMul(
          b, dot_input_lhs, dot_input_rhs, iter_args.back());
      TF_CHECK_OK(accumulator_next_or.status());
      accumulator_next = accumulator_next_or.value();
    } else {
      // Execute matrix multiplication of input tiles and pass the accumulator.
      // TODO(manany): Should be looked into once we enable Hopper workloads.
      // maxNumImpreciseAcc flag was introduced for Hopper to accumulate in a
      // lower precision than the output type. The change was introduced here:
      // https://github.com/openai/triton/commit/31b0c521427109a8eda609b58d756c380b21599a
      auto input_precision =
          IsTf32Allowed(dot_instr) && !is_unsupported_bitwidth
              ? mt::InputPrecision::TF32
              : mt::InputPrecision::IEEE;
      // For fp8 matmuls, disable accumulator promotion, as it's what cublas
      // does. It may make sense to enable frequent accumulator promotion at
      // higher matmul precisions set in the config.
      int max_num_imprecise_acc =
          IsFp8Matmul(dot_instr) ? std::numeric_limits<int>::max() : 0;
      accumulator_next =
          b.create<mt::DotOp>(dot_input_lhs, dot_input_rhs, iter_args.back(),
                              /*inputPrecision=*/input_precision,
                              /*maxNumImpreciseAcc=*/max_num_imprecise_acc);
    }
    iter_args_next.push_back(accumulator_next);

    b.create<mlir::scf::YieldOp>(iter_args_next);
    return;
  };

  // Pointers to inputs of LHS scope, then RHS, then the accumulator
  // that change with every loop iteration and are passed between them.
  SmallVector<Value> iter_args;
  iter_args.reserve(lsize + rsize + 1 + is_sparse);

  for (const Side* side : scopes.input_scopes()) {
    for (const HloInstruction* input : ScopeInputs(analysis, side->scope)) {
      TF_RET_CHECK(
          iter_args_to_inputs.insert({iter_args.size(), input}).second);
      TF_ASSIGN_OR_RETURN(SmallVector<Value> arguments,
                          GetArguments(fn, *input));
      TF_ASSIGN_OR_RETURN(Value tensor_ptr,
                          emitter.EmitTensorPointer(
                              input, *side, arguments, scopes.pid_k(),
                              iter_args_to_boundary_checks[iter_args.size()]));
      iter_args.push_back(tensor_ptr);
    }
  }

  iter_args.push_back(accumulator_init);
  Value acc_final = b.create<mlir::scf::ForOp>(
                         /*lowerBound=*/c32(0),
                         /*upperBound=*/c32(dims.k),
                         /*step=*/c32(block_k * split_k),
                         /*iterArgs=*/iter_args, body_builder)
                        .getResult(iter_args.size() - 1);
  absl::flat_hash_map<const HloInstruction*, Value> values_out;
  TF_ASSIGN_OR_RETURN(Type acc_final_ty,
                      TritonType(b, dot_instr->shape().element_type()));
  values_out[dot_instr] = Cast(b, acc_final, acc_final_ty);

  // Emit the output scope.
  if (std::vector<const HloInstruction*> to_emit =
          emitter.EpiloguePostOrderTransitiveOperands(root);
      !to_emit.empty()) {
    for (const HloInstruction* input :
         ScopeInputs(analysis, TritonFusionAnalysis::Scope::OUTPUT)) {
      std::vector<int32_t> boundary_checks;
      TF_ASSIGN_OR_RETURN(SmallVector<Value> arguments,
                          GetArguments(fn, *input));
      TF_ASSIGN_OR_RETURN(
          Value tensor_pointer,
          emitter.EmitTensorPointer(input, scopes.out(), arguments,
                                    scopes.pid_k(), boundary_checks));
      TF_RET_CHECK(values_out
                       .insert({input, EmitParameterLoad(b, tensor_pointer,
                                                         boundary_checks)})
                       .second);
    }
    TF_RETURN_IF_ERROR(EmitScope(b, libdevice_path, device_info, &analysis,
                                 scopes.out(), to_emit, values_out)
                           .status());
  }

  // Emit tensor store operations for all outputs.
  for (int i = 0;
       i < fn.getNumArguments() - dot_instr->parent()->num_parameters(); ++i) {
    const HloInstruction* producer =
        root->shape().IsTuple() ? root->operand(i) : root;
    std::vector<int32_t> boundary_checks;
    TF_ASSIGN_OR_RETURN(
        Value tensor_pointer,
        emitter.EmitTensorPointer(
            producer, scopes.out(),
            {fn.getArgument(i + dot_instr->parent()->num_parameters())},
            scopes.pid_k(), boundary_checks));
    b.create<mt::StoreOp>(tensor_pointer, values_out[producer], boundary_checks,
                          mt::CacheModifier::NONE, mt::EvictionPolicy::NORMAL);
  }
  return absl::OkStatus();
}

// Computes the base pointer offset for the given tile multi-index and hlo shape
// taking into account the physical layout of the hlo buffer.
absl::StatusOr<Value> ComputeBasePtrOffset(
    ImplicitLocOpBuilder b, ValueRange tile_multi_index,
    const TiledHloInstruction& tiled_hlo) {
  const Shape& shape = tiled_hlo.hlo()->shape();
  Shape linear_shape = ShapeUtil::MakeShape(shape.element_type(),
                                            {ShapeUtil::ElementsIn(shape)});

  // Bitcast map gives an indexing map from the parameter shape (multi-index) to
  // a linear index respecting physical layout of the memory.
  auto bitcast_map = GetBitcastMap(shape, linear_shape, b.getContext());

  TF_ASSIGN_OR_RETURN(IndexingMap tile_offsets_indexing,
                      tiled_hlo.tile_offsets_indexing());

  auto compose_indexing_maps =
      ComposeIndexingMaps(tile_offsets_indexing, bitcast_map);
  compose_indexing_maps.Simplify();

  return b.create<ma::IndexCastUIOp>(
      b.getI64Type(), mlir_converter::ApplyIndexing(compose_indexing_maps,
                                                    /*dims=*/tile_multi_index,
                                                    /*symbols=*/{}, b)[0]);
}

namespace ir_emitter_triton_internal {

SmallVector<Value, 3> ComputeDelinearizedTileIndex(
    ImplicitLocOpBuilder& b,
    absl::Span<const int64_t> num_output_tiles_per_dim) {
  Value pid = b.create<ma::IndexCastUIOp>(
      b.getIndexType(), b.create<mt::GetProgramIdOp>(mt::ProgramIDDim::X));

  // Delinearize the block id.
  mlir::AffineExpr program_id = mlir::getAffineDimExpr(0, b.getContext());
  auto tile_exprs =
      DelinearizeIndex(num_output_tiles_per_dim, program_id, b.getContext());

  IndexingMap program_id_to_root_tile_offset = IndexingMap::FromTensorSizes(
      mlir::AffineMap::get(/*dimCount=*/1, /*symbolCount=*/0, tile_exprs,
                           b.getContext()),
      /*dim_upper_bounds=*/{Product(num_output_tiles_per_dim)},
      /*symbol_upper_bounds=*/{});

  return mlir_converter::ApplyIndexing(program_id_to_root_tile_offset,
                                       /*dims=*/pid,
                                       /*symbols=*/{}, b);
}

absl::StatusOr<MakeTensorPtrOpAndBoundaryChecks> CreateMakeTensorPtrOp(
    ImplicitLocOpBuilder& b, ValueRange tile_multi_index,
    const TiledHloInstruction& tiled_hlo, Value parent_base_ptr) {
  const llvm::SmallVector<int64_t>& tile_strides = tiled_hlo.tile_strides();
  const Shape& shape = tiled_hlo.hlo()->shape();

  // Compute physical strides of the tile. `tile_strides` contains strides for
  // individual dimensions. We need to convert them to strides in the buffer
  // taking into account physical layout.
  // TODO(b/331332678): Compute indexing maps to physical layout indexing in
  // SymbolicTileAnalysis.
  llvm::SmallVector<Value> strides(tile_strides.size());
  int64_t current_stride = 1;
  for (int64_t cur_dim : LayoutUtil::MinorToMajor(shape)) {
    strides[cur_dim] =
        CreateConst(b, b.getI64Type(), tile_strides[cur_dim] * current_stride);
    current_stride *= shape.dimensions(cur_dim);
  }

  TF_ASSIGN_OR_RETURN(IndexingMap tile_offsets_indexing,
                      tiled_hlo.tile_offsets_indexing());
  auto tile_offsets_as_indices =
      mlir_converter::ApplyIndexing(tile_offsets_indexing,
                                    /*dims=*/tile_multi_index,
                                    /*symbols=*/{}, b);

  // Triton requires that all block dimensions are a power of 2.
  SmallVector<int64_t> padded_tile_sizes =
      GetPaddedTileSizes(tiled_hlo.tile_sizes());

  llvm::SmallVector<Value> residual_shape;
  llvm::SmallVector<Value> offsets;
  llvm::SmallVector<int32_t> order;
  llvm::SmallVector<int32_t> boundary_checks;

  for (int dim_idx = 0; dim_idx < padded_tile_sizes.size(); ++dim_idx) {
    // In general, there are two options for computing a block:
    //
    //   - Output a TensorPtr whose base pointer is the base pointer of the
    //     TiledHloInstruction and provide the necessary offsets so that Triton
    //     can compute the pointer to the block specific to the given pid. This
    //     option yields simpler code, but has limitations. Triton cannot handle
    //     many combinations of strides and offsets. E.g., if we want to slice
    //     [10] with [1:5:2], we have no way of specifying this: Triton
    //     always multiplies the stride by the offset, and with a stride of 2
    //     it's not possible to start reading from element 1.
    //
    //   - Output a TensorPtr that points directly to the tile specific to the
    //     pid. All offset computation is done in advance. MakeTensorPtrOp
    //     sees 0 offsets. This allows Triton to read any block regardless of
    //     strides size or offsets. To make sure that masking is correct, we
    //     compute a "residual shape" which is the original parent shape minus
    //     the offsets.
    Value parent_size =
        CreateConst(b, b.getI64Type(), shape.dimensions(dim_idx));
    Value offset = b.create<ma::IndexCastOp>(b.getI64Type(),
                                             tile_offsets_as_indices[dim_idx]);
    residual_shape.push_back(b.create<ma::SubIOp>(parent_size, offset));
    offsets.push_back(CreateConst(b, b.getI32Type(), 0));

    // TODO(b/342989850): Clarify and comment what `order` exactly is. It's not
    // entirely clear from the Triton docs.
    order.insert(order.begin(), dim_idx);

    if (shape.dimensions(dim_idx) % padded_tile_sizes[dim_idx] != 0) {
      boundary_checks.push_back(dim_idx);
    }
  }

  TF_ASSIGN_OR_RETURN(Value ptr_offset,
                      ComputeBasePtrOffset(b, tile_multi_index, tiled_hlo));
  auto tile_ptr = AddPtr(b, parent_base_ptr, ptr_offset);

  return MakeTensorPtrOpAndBoundaryChecks{
      b.create<mt::MakeTensorPtrOp>(
          /*base=*/tile_ptr,
          /*shape=*/residual_shape,
          /*strides=*/strides,
          /*offsets=*/offsets,
          /*tensorShape=*/llvm::to_vector_of<int32_t>(padded_tile_sizes),
          /*order=*/order),
      boundary_checks};
}

}  // namespace ir_emitter_triton_internal

absl::Status EmitGeneric(mlir::OpBuilder builder,
                         absl::string_view libdevice_path,
                         const se::DeviceDescription& device_info,
                         const HloFusionInstruction* fusion,
                         mlir::triton::FuncOp fn,
                         const BlockLevelParameters& block_level_parameters) {
  const HloComputation* computation = fusion->fused_instructions_computation();
  SymbolicTileAnalysisOrError symbolic_tile_analysis_or =
      SymbolicTileAnalysis::AnalyzeComputation(
          *computation, builder.getContext(),
          TritonEmitterConstraints::GetBuilder(device_info));
  if (std::holds_alternative<FusionDecision>(symbolic_tile_analysis_or)) {
    return Internal(
        "Unsupported fusion in EmitGeneric: %s",
        std::get<FusionDecision>(symbolic_tile_analysis_or).Explain());
  }

  const auto& symbolic_tile_analysis =
      std::get<SymbolicTileAnalysis>(symbolic_tile_analysis_or);
  const HloInstruction* root = computation->root_instruction();
  auto loc = mlir::NameLoc::get(builder.getStringAttr(root->name()));
  ImplicitLocOpBuilder b(loc, builder);

  TF_ASSIGN_OR_RETURN(TiledHloComputation tiled_hlo_computation,
                      symbolic_tile_analysis.ComputeTiledHloInstructions(
                          block_level_parameters.output_tile_sizes,
                          /*constraints_are_known_satisfied=*/false,
                          /*compute_all_tile_offset_indexing_maps=*/true));

  SmallVector<Value, 3> tile_multi_index =
      ir_emitter_triton_internal::ComputeDelinearizedTileIndex(
          b, tiled_hlo_computation.num_output_tiles_per_dim());

  TF_ASSIGN_OR_RETURN(
      Value result,
      EmitTiledScope(b, libdevice_path, device_info, fusion,
                     tiled_hlo_computation, fn, tile_multi_index));

  // Some types are stored using different types, e.g. i1 is stored in memory
  // as i8. It's important to type checking that we perform a conversion before
  // storing if the type of the result does not match the type of the output
  // pointer.
  Type result_element_type =
      mlir::cast<ShapedType>(result.getType()).getElementType();
  Type result_storage_type = StorageType(b, result_element_type);

  if (result_element_type != result_storage_type) {
    result = Cast(b, result, result_storage_type);
  }

  const auto& tiled_hlo = *tiled_hlo_computation.GetRoot();
  TF_ASSIGN_OR_RETURN(auto make_tensor,
                      ir_emitter_triton_internal::CreateMakeTensorPtrOp(
                          b, tile_multi_index, tiled_hlo,
                          fn.getArgument(computation->num_parameters())));
  b.create<mt::StoreOp>(make_tensor.op, result, make_tensor.boundary_checks,
                        mt::CacheModifier::NONE, mt::EvictionPolicy::NORMAL);

  return absl::OkStatus();
}

void LoadMlirDialectsForTriton(mlir::MLIRContext& mlir_context) {
  mlir_context
      .loadDialect<mt::TritonDialect, mt::gpu::TritonGPUDialect,
                   mlir::arith::ArithDialect, mlir::affine::AffineDialect,
                   mlir::LLVM::LLVMDialect, xla::gpu::XlaGpuDialect>();
  mlir::DialectRegistry registry;
  mlir::func::registerInlinerExtension(registry);
  mlir::LLVM::registerInlinerInterface(registry);
  mlir_context.appendDialectRegistry(registry);
}

// Simplified copy of translateLLVMToLLVMIR which in addition takes
// path to libdevice directly as an argument.
absl::StatusOr<std::unique_ptr<llvm::Module>> TranslateLLVMToLLVMIR(
    llvm::LLVMContext* llvmContext, mlir::ModuleOp module,
    absl::string_view libdevice_path) {
  mlir::DialectRegistry registry;
  mlir::registerBuiltinDialectTranslation(registry);
  mlir::registerLLVMDialectTranslation(registry);
  mlir::registerNVVMDialectTranslation(registry);
  mlir::registerROCDLDialectTranslation(registry);
  module->getContext()->appendDialectRegistry(registry);

  std::unique_ptr<llvm::Module> llvmModule =
      mlir::translateModuleToLLVMIR(module, *llvmContext);
  if (!llvmModule) {
    return Internal("Failed to emit LLVM IR.");
  }
  // TODO: b/363203060 - Upstream Triton sets specific flags for the LLVM
  // optimizer to get best performance. Figure out if we can gain any of it by
  // propagating these flags to
  // xla/service/gpu/llvm_gpu_backend/gpu_backend_lib.cc.
  return llvmModule;
}

absl::Status CreateInternalError(std::string_view message,
                                 const HloFusionInstruction* fusion,
                                 mlir::ModuleOp triton_module) {
  std::string err;
  llvm::raw_string_ostream os(err);
  os << message << "\n";
  os << "fusion instruction: " << fusion->ToString() << "\n";
  os << "HLO module to reproduce:\n"
     << ExtractInstructionIntoNewModule(*fusion)->ToString();
  os << "triton_module: \n";
  triton_module->print(os, mlir::OpPrintingFlags().enableDebugInfo(true, true));
  return absl::InternalError(err);
}

absl::Status DoSupportType(const DebugOptions& debug_options,
                           PrimitiveType type) {
  if (type == S4 && !debug_options.xla_gpu_enable_triton_gemm_int4()) {
    return absl::FailedPreconditionError(
        "Int4 support is not enabled in the debug options.");
  }
  return absl::OkStatus();
}

absl::StatusOr<mlir::OwningOpRef<mlir::ModuleOp>> CreateTritonModule(
    absl::string_view fn_name, const HloFusionInstruction* fusion,
    const se::DeviceDescription& device_info,
    const BlockLevelParameters& block_level_parameters,
    mlir::MLIRContext& mlir_context) {
  LoadMlirDialectsForTriton(mlir_context);

  const HloComputation* hlo_computation =
      fusion->fused_instructions_computation();

  mlir::OpBuilder b(&mlir_context);
  auto loc = mlir::NameLoc::get(b.getStringAttr(hlo_computation->name()));
  mlir::OwningOpRef<mlir::ModuleOp> triton_module =
      llvm_ir::CreateMlirModuleOp(loc);
  b.setInsertionPointToEnd(triton_module->getBody());

  const auto debug_options = fusion->GetModule()->config().debug_options();
  // Build Triton kernel.
  SmallVector<Type> fn_arg_types;
  for (HloInstruction* p : hlo_computation->parameter_instructions()) {
    PrimitiveType type = p->shape().element_type();
    TF_RETURN_IF_ERROR(DoSupportType(debug_options, type));
    Type ir_type;
    if (type == U16) {
      ir_type = b.getI16Type();
    } else {
      TF_ASSIGN_OR_RETURN(ir_type, TritonType(b, type));
    }
    fn_arg_types.push_back(
        mt::PointerType::get(StorageType(b, ir_type), mn::kGlobalMemorySpace));
  }

  for (const ShapeUtil::IndexedShape& s :
       ShapeUtil::GetLeafShapes(fusion->shape())) {
    TF_ASSIGN_OR_RETURN(Type triton_ty, TritonType(b, s.shape.element_type()));
    fn_arg_types.push_back(mt::PointerType::get(StorageType(b, triton_ty),
                                                mn::kGlobalMemorySpace));
  }

  auto fn = b.create<mt::FuncOp>(loc, fn_name,
                                 b.getFunctionType(fn_arg_types, std::nullopt));
  for (int i = 0; i < fn.getNumArguments(); ++i) {
    fn.setArgAttr(i, "tt.divisibility", b.getIntegerAttr(b.getI32Type(), 16));
  }
  fn.addEntryBlock();
  b.setInsertionPointToStart(&fn.front());

  std::string libdevice_path =
      GetLibdevicePath(fusion->GetModule()->config(), device_info);

  auto backend_config =
      fusion->backend_config<GpuBackendConfig>()->fusion_backend_config();
  absl::string_view fusion_kind = backend_config.kind();

  if (fusion_kind == kTritonGemmFusionKind) {
    TF_RETURN_IF_ERROR(EmitMatMul(b, libdevice_path, device_info, fusion, fn,
                                  block_level_parameters));
  } else if (fusion_kind == kTritonFusionKind) {
    TF_RETURN_IF_ERROR(EmitGeneric(b, libdevice_path, device_info, fusion, fn,
                                   block_level_parameters));
  } else {
    return Internal("Unsupported fusion kind: %s", fusion_kind);
  }

  b.create<mt::ReturnOp>(loc);

  if (mlir::failed(mlir::verify(*triton_module))) {
    return CreateInternalError(
        "Failed to verify Triton module for fusion:", fusion, *triton_module);
  }

  mlir::PassManager pm(&mlir_context);
  pm.addPass(mlir::createCanonicalizerPass());
  pm.addPass(mlir::createCSEPass());
  if (mlir::failed(pm.run(triton_module.get()))) {
    return CreateInternalError(
        "Failed to create Triton module for fusion:", fusion, *triton_module);
  }

  auto dump_triton_ir = [&]() {
    std::string triton_ir;
    llvm::raw_string_ostream os(triton_ir);
    triton_module->print(os,
                         mlir::OpPrintingFlags().enableDebugInfo(true, true));
    return triton_ir;
  };
  VLOG(6) << dump_triton_ir();
  if (DumpingEnabledForHloModule(*hlo_computation->parent())) {
    DumpToFileInDirOrStdout(*hlo_computation->parent(), "triton_ir", "ttir",
                            dump_triton_ir());
  }

  return std::move(triton_module);
}

absl::StatusOr<TritonWrapperResult> TritonWrapper(
    absl::string_view fn_name, const HloFusionInstruction* fusion,
    const se::GpuComputeCapability& cc,
    const se::DeviceDescription& device_info,
    const BlockLevelParameters& block_level_parameters,
    llvm::Module* llvm_module, mlir::MLIRContext& mlir_context) {
  if (std::holds_alternative<se::CudaComputeCapability>(cc)) {
    auto ccCuda = std::get<se::CudaComputeCapability>(cc);
    if (!ccCuda.IsAtLeastAmpere()) {
      return absl::FailedPreconditionError(absl::StrCat(
          "Triton support is only enabled for Ampere GPUs (compute ",
          "capability 8.0) and up, but got compute capability ", ccCuda.major,
          ".", ccCuda.minor, "."));
    }
  }

  TF_ASSIGN_OR_RETURN(auto triton_module,
                      CreateTritonModule(fn_name, fusion, device_info,
                                         block_level_parameters, mlir_context));

  VLOG(3) << fusion->ToString(HloPrintOptions::ShortParsable());
  VLOG(3) << fusion->fused_instructions_computation()->ToString(
      HloPrintOptions::ShortParsable());

  // Compile Triton kernel to LLVM.
  const HloModule* hlo_module = fusion->GetModule();
  return CompileTritonToLLVM(hlo_module->config(), hlo_module->name(), cc,
                             device_info, block_level_parameters,
                             triton_module.get(), llvm_module, mlir_context);
}

// TODO(b/325220878): Replace TritonGemmConfig with a more generic abstraction.
absl::StatusOr<TritonWrapperResult> CompileTritonToLLVM(
    const HloModuleConfig& hlo_config, absl::string_view hlo_module_name,
    const se::GpuComputeCapability& cc,
    const se::DeviceDescription& device_info,
    const BlockLevelParameters& block_level_parameters,
    mlir::ModuleOp triton_module, llvm::Module* llvm_module,
    mlir::MLIRContext& mlir_context, bool emit_kernel) {
  if (std::holds_alternative<se::CudaComputeCapability>(cc)) {
    auto ccCuda = std::get<se::CudaComputeCapability>(cc);
    if (!ccCuda.IsAtLeastAmpere()) {
      return absl::FailedPreconditionError(absl::StrCat(
          "Triton support is only enabled for Ampere GPUs (compute ",
          "capability 8.0) and up, but got compute capability ", ccCuda.major,
          ".", ccCuda.minor, "."));
    }
  }

  bool should_verify =
      (hlo_config.debug_options().xla_gpu_llvm_verification_level() >= 1);
#ifndef NDEBUG
  should_verify = true;
#endif

  mlir::PassManager pm(&mlir_context);
  pm.enableVerifier(should_verify);

  std::optional<llvm::raw_fd_ostream> log_stream;
  if (hlo_config.debug_options().xla_gpu_dump_llvmir()) {
    const std::string basename =
        absl::StrCat(absl::string_view(tsl::io::Basename(hlo_module_name)),
                     ".triton-passes.log");
    std::string outputs_dir;
    if (!tsl::io::GetTestUndeclaredOutputsDir(&outputs_dir)) {
      outputs_dir = hlo_config.debug_options().xla_dump_to();
    }
    if (!outputs_dir.empty()) {
      std::string path = tsl::io::JoinPath(outputs_dir, basename);
      std::error_code err;
      log_stream.emplace(path, err, llvm::sys::fs::OF_None);
      if (err) {
        log_stream.reset();
        LOG(ERROR) << err.message();
      } else {
        pm.getContext()->disableMultithreading();
        auto print_always = [](mlir::Pass*, mlir::Operation*) { return true; };
        pm.enableIRPrinting(/*shouldPrintBeforePass=*/print_always,
                            /*shouldPrintAfterPass=*/print_always,
                            /*printModuleScope=*/true,
                            /*printAfterOnlyOnChange=*/false,
                            /*printAfterOnlyOnFailure=*/true, *log_stream,
                            /*opPrintingFlags=*/{});
      }
    } else {
      LOG(ERROR) << "--xla_gpu_dump_llvmir is set, but neither the environment "
                 << "variable TEST_UNDECLARED_OUTPUTS_DIR nor the flag "
                 << "--xla_dump_to is set, so the llvm dumps are disabled.";
    }
  }

  // Lower affine expressions into arithmetic ops.
  pm.addPass(mlir::createLowerAffinePass());

  // Lower xla_gpu.apply_indexing into arithmetic ops.
  pm.addPass(CreateSimplifyAffinePass());

  mlir::triton::nvidia_gpu::ClusterInfo cluster_info;
  if (!CreateTritonPipeline(pm, device_info, block_level_parameters,
                            cluster_info)
           .ok()) {
    return Internal("Failed to create Triton pipeline.");
  }
  // Triton generates pointers to the global address space, while XLA needs a
  // kernel signature with pointers to the generic address space.
  pm.addPass(CreateGeneralizeKernelSignaturePass());
  // llvm::Linker::linkModules() segfaults if we don't strip locations.
  pm.addPass(mlir::createStripDebugInfoPass());

  if (log_stream.has_value()) {
    pm.printAsTextualPipeline(log_stream.value());
    log_stream->write("\n\n", 2);
  }
  bool succeeded = mlir::succeeded(pm.run(triton_module));

  if (log_stream.has_value()) {
    log_stream->flush();
  }

  if (!succeeded) {
    return Internal("Failed to compile Triton kernel.");
  }

  const int shared_mem_bytes =
      triton_module->getAttrOfType<mlir::IntegerAttr>("triton_gpu.shared")
          .getInt();
  VLOG(2) << "Shared memory usage: " << shared_mem_bytes << " B";
  if (std::holds_alternative<se::CudaComputeCapability>(cc) &&
      shared_mem_bytes > device_info.shared_memory_per_block_optin()) {
    return absl::ResourceExhaustedError(absl::StrFormat(
        "Shared memory size limit exceeded: requested %d, available: %d",
        shared_mem_bytes, device_info.shared_memory_per_block_optin()));
  }

  if (emit_kernel) {
    TF_ASSIGN_OR_RETURN(
        std::unique_ptr<llvm::Module> ll_triton_module,
        TranslateLLVMToLLVMIR(&llvm_module->getContext(), triton_module,
                              GetLibdevicePath(hlo_config, device_info)));
    VLogModule(5, *ll_triton_module);
    if (should_verify) {
      VerifyModule(*ll_triton_module);
    }

    // Integrate LLVM matmul kernel into XLA's LLVM module.
    ll_triton_module->eraseNamedMDNode(
        ll_triton_module->getNamedMetadata("nvvm.annotations"));
    ll_triton_module->setDataLayout(llvm_module->getDataLayout());
    ll_triton_module->setTargetTriple(llvm_module->getTargetTriple());
    // Use override flag because libdevice functions can be present in both.
    TF_RET_CHECK(
        !llvm::Linker::linkModules(*llvm_module, std::move(ll_triton_module),
                                   llvm::Linker::Flags::OverrideFromSrc));

    VLogModule(5, *llvm_module);
    if (should_verify) {
      VerifyModule(*llvm_module);
    }
  }

  // `cluster_info` must be read after pm.run().
  std::optional<se::ClusterDim> cluster_dim;
  if (block_level_parameters.num_ctas > 1) {
    VLOG(3) << "num_ctas: " << block_level_parameters.num_ctas
            << ", cluster_info: " << cluster_info.clusterDimX << ","
            << cluster_info.clusterDimY << "," << cluster_info.clusterDimZ;
    if (cluster_info.clusterDimX > 1 || cluster_info.clusterDimY > 1 ||
        cluster_info.clusterDimZ > 1) {
      cluster_dim =
          se::ClusterDim(cluster_info.clusterDimX, cluster_info.clusterDimY,
                         cluster_info.clusterDimZ);
    }
  } else {
    TF_RET_CHECK(cluster_info.clusterDimX == 1 &&
                 cluster_info.clusterDimY == 1 &&
                 cluster_info.clusterDimZ == 1);
  }
  return {{shared_mem_bytes, cluster_dim}};
}

}  // namespace gpu
}  // namespace xla
