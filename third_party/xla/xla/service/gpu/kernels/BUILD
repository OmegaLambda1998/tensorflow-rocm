load("@local_config_cuda//cuda:build_defs.bzl", "cuda_library")
load("@local_config_rocm//rocm:build_defs.bzl", "if_rocm_is_configured")
load(
    "@local_tsl//tsl/platform/default:cuda_build_defs.bzl",
    "if_cuda_is_configured",
)
load("//xla:xla.bzl", "xla_cc_binary")
load("//xla/service/gpu:build_defs.bzl", "gpu_kernel_library")
load("//xla/stream_executor:build_defs.bzl", "if_gpu_is_configured")
load("//xla/tests:build_defs.bzl", "DEFAULT_DISABLED_BACKENDS", "xla_test")
load("//xla/tsl:tsl.bzl", "if_windows")

package(
    # copybara:uncomment default_applicable_licenses = ["//tensorflow:license"],
    default_visibility = ["//visibility:private"],
    licenses = ["notice"],
)

package_group(
    name = "friends",
    includes = ["//xla:friends"],
)

cc_library(
    name = "custom_kernel_fusion",
    srcs = ["custom_kernel_fusion.cc"],
    hdrs = ["custom_kernel_fusion.h"],
    visibility = [":friends"],
    deps = [
        ":custom_kernel",
        "//xla/hlo/ir:hlo",
        "//xla/stream_executor:device_description",
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/synchronization",
        "@local_tsl//tsl/platform:logging",
    ],
)

cc_library(
    name = "custom_kernel_fusion_pattern",
    srcs = ["custom_kernel_fusion_pattern.cc"],
    hdrs = ["custom_kernel_fusion_pattern.h"],
    visibility = [":friends"],
    deps = [
        "//xla/hlo/ir:hlo",
        "//xla/service/gpu:backend_configs_cc",
        "//xla/stream_executor:device_description",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/types:span",
    ],
)

cc_library(
    name = "custom_kernel",
    srcs = ["custom_kernel.cc"],
    hdrs = ["custom_kernel.h"],
    visibility = [":friends"],
    deps = [
        "//xla/stream_executor",
        "//xla/stream_executor:kernel_spec",
        "@com_google_absl//absl/strings:str_format",
    ],
)

# Bundle all custom fusions into a single target, so we can link all fusions and patterns by adding
# a single dependency.
cc_library(
    name = "custom_fusion_library",
    tags = ["gpu"],
    visibility = [":friends"],
    deps = if_cuda_is_configured([":cutlass_gemm_fusion"]),
)

cc_library(
    name = "cutlass_gemm_fusion",
    srcs = ["cutlass_gemm_fusion.cc"],
    hdrs = ["cutlass_gemm_fusion.h"],
    tags = [
        "cuda-only",
        "gpu",
    ],
    deps = [
        ":custom_kernel",
        ":custom_kernel_fusion",
        ":custom_kernel_fusion_pattern",
        ":cutlass_gemm",
        ":cutlass_gemm_custom_kernel",
        "//xla:shape_util",
        "//xla:xla_data_proto_cc",
        "//xla/hlo/ir:hlo",
        "//xla/service:pattern_matcher",
        "//xla/service/gpu:backend_configs_cc",
        "//xla/stream_executor:device_description",
        "@com_google_absl//absl/algorithm:container",
        "@com_google_absl//absl/container:flat_hash_set",
        "@com_google_absl//absl/log",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/types:span",
        "@local_tsl//tsl/platform:errors",
        "@local_tsl//tsl/platform:statusor",
    ],
    alwayslink = 1,  # static fusion registration
)

xla_test(
    name = "cutlass_gemm_fusion_test",
    srcs = ["cutlass_gemm_fusion_test.cc"],
    backends = ["gpu"],
    # TODO(b/332820384): Enable when it passes on H100.
    disabled_backends = DEFAULT_DISABLED_BACKENDS + ["gpu_h100"],
    tags = ["cuda-only"],
    deps = [
        ":custom_kernel_fusion_pattern",
        ":cutlass_gemm_custom_kernel",
        ":cutlass_gemm_fusion",
        "//xla:array",
        "//xla:array2d",
        "//xla:array3d",
        "//xla:error_spec",
        "//xla:literal_util",
        "//xla:types",
        "//xla:xla_data_proto_cc",
        "//xla/service/gpu:gpu_device_info_for_tests",
        "//xla/service/gpu/transforms:custom_kernel_fusion_rewriter",
        "//xla/tests:hlo_test_base",
        "//xla/tests:verified_hlo_module",
        "@com_google_absl//absl/log",
        "@com_google_absl//absl/status:statusor",
        "@com_google_googletest//:gtest_main",
        "@local_tsl//tsl/platform:test",
        "@local_tsl//tsl/platform:test_main",
    ],
)

cc_library(
    name = "topk_kernel",
    srcs = ["topk_kernel.cc"],
    hdrs = ["topk_kernel.h"],
    compatible_with = [],
    tags = ["gpu"],
    deps = [
        ":topk_kernel_gpu",
        "//xla:shape_util",
        "//xla:types",
        "//xla:util",
        "//xla:xla_data_proto_cc",
        "//xla/stream_executor",  # build_cleaner: keep
        "//xla/stream_executor:platform",
        "//xla/stream_executor:typed_kernel_factory",
        "//xla/stream_executor/gpu:gpu_stream_header",
        "//xla/stream_executor/gpu:gpu_types_header",
        "@com_google_absl//absl/numeric:bits",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@local_tsl//tsl/platform:errors",
        "@local_tsl//tsl/platform:logging",
        "@local_tsl//tsl/platform:statusor",
    ],
)

gpu_kernel_library(
    name = "topk_kernel_gpu",
    srcs = [
        "topk_kernel.cu.h",
        "topk_kernel_bfloat16.cu.cc",
        "topk_kernel_float.cu.cc",
    ],
    hdrs = ["topk_kernel_common.h"],
    compatible_with = [],
    deps = [
        "//xla:types",
        "//xla/stream_executor/gpu:gpu_types_header",
        "//xla/tsl/lib/math:math_util",
    ] + if_cuda_is_configured([
        "@local_config_cuda//cuda:cuda_headers",
    ]) + if_rocm_is_configured([
        "@local_config_rocm//rocm:rocm_headers",
    ]),
)

xla_test(
    name = "topk_kernel_test",
    srcs = ["topk_kernel_test.cc"],
    backends = ["gpu"],
    deps = [
        ":topk_kernel",
        "//xla:types",
        "//xla:xla_data_proto_cc",
        "//xla/stream_executor",  # build_cleaner: keep
        "//xla/stream_executor:device_memory_handle",
        "//xla/stream_executor:platform_manager",
        "//xla/stream_executor/gpu:gpu_init",
        "//xla/stream_executor/gpu:gpu_types_header",
        "//xla/stream_executor/host:host_platform",
        "@com_google_absl//absl/log:check",
        "@com_google_absl//absl/random",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/time",
        "@local_tsl//tsl/platform:statusor",
        "@local_tsl//tsl/platform:test",
        "@local_tsl//tsl/platform:test_benchmark",
        "@local_tsl//tsl/platform:test_main",
    ],
)

cc_library(
    name = "topk_custom_kernel",
    srcs = ["topk_custom_kernel.cc"],
    hdrs = ["topk_custom_kernel.h"],
    local_defines = if_cuda_is_configured(["GOOGLE_CUDA=1"]) + if_rocm_is_configured([
        "TENSORFLOW_USE_ROCM=1",
    ]),
    visibility = [":friends"],
    deps = [
        ":custom_kernel",
        "//xla:types",
        "//xla:xla_data_proto_cc",
        "//xla/stream_executor",
        "//xla/stream_executor:kernel_spec",
        "@com_google_absl//absl/numeric:bits",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@local_tsl//tsl/platform:statusor",
    ] + if_gpu_is_configured([
        ":topk_kernel_gpu",
    ]),
)

xla_test(
    name = "topk_custom_kernel_test",
    srcs = ["topk_custom_kernel_test.cc"],
    backends = ["gpu"],
    deps = [
        ":topk_custom_kernel",
        "//xla:types",
        "//xla:xla_data_proto_cc",
        "//xla/service:platform_util",
        "//xla/stream_executor",
        "//xla/stream_executor:platform",
        "//xla/stream_executor:platform_manager",
        "//xla/stream_executor/cuda:cuda_platform",
        "//xla/tsl/lib/core:status_test_util",
        "@com_google_absl//absl/random",
        "@com_google_absl//absl/strings",
        "@com_google_googletest//:gtest",
        "@local_tsl//tsl/platform:path",
        "@local_tsl//tsl/platform:statusor",
        "@local_tsl//tsl/platform:test",
        "@local_tsl//tsl/platform:test_main",
    ],
)

#===--------------------------------------------------------------------------------------------===#
# CUTLASS Gemm <-> xla::gpu::kernel::CustomKernel adaptor
#===--------------------------------------------------------------------------------------------===#

cc_library(
    name = "cutlass_gemm_custom_kernel",
    srcs = ["cutlass_gemm_custom_kernel.cc"],
    hdrs = ["cutlass_gemm_custom_kernel.h"],
    tags = [
        "cuda-only",
        "gpu",
    ],
    deps = [
        ":custom_kernel",
        ":cutlass_gemm",
        ":cutlass_gemm_kernels",  # build_cleaner: keep
        "//xla:xla_data_proto_cc",
        "//xla/stream_executor",
        "//xla/stream_executor:kernel_spec",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/log",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@local_config_cuda//cuda:cuda_headers",
    ],
)

xla_test(
    name = "cutlass_gemm_custom_kernel_test",
    srcs = ["cutlass_gemm_custom_kernel_test.cc"],
    backends = ["gpu"],
    data = [":cutlass_gemm_kernel_f32xf32_to_f32.so"],
    tags = ["cuda-only"],
    deps = [
        ":cutlass_gemm_custom_kernel",
        "//xla:xla_data_proto_cc",
        "//xla/stream_executor",
        "//xla/stream_executor:platform",
        "//xla/stream_executor:platform_manager",
        "//xla/stream_executor/cuda:cuda_platform",
        "//xla/tsl/lib/core:status_test_util",
        "@local_tsl//tsl/platform:path",
        "@local_tsl//tsl/platform:statusor",
        "@local_tsl//tsl/platform:test",
        "@local_tsl//tsl/platform:test_main",
    ],
)

xla_cc_binary(
    name = "cutlass_gemm_custom_kernel_benchmarks",
    testonly = 1,
    srcs = ["cutlass_gemm_custom_kernel_benchmarks.cc"],
    tags = [
        "cuda-only",
        "gpu",
    ],
    deps = [
        ":cutlass_gemm_custom_kernel",
        "//xla:xla_data_proto_cc",
        "//xla/service:gpu_plugin",
        "//xla/stream_executor",
        "//xla/stream_executor:platform",
        "//xla/stream_executor:platform_manager",
        "//xla/stream_executor/cuda:cuda_platform",
        "@local_tsl//tsl/platform:status",
        "@local_tsl//tsl/platform:statusor",
        "@local_tsl//tsl/platform:test",
        "@local_tsl//tsl/platform:test_benchmark",
        "@local_tsl//tsl/platform:test_main",
    ],
)

#===--------------------------------------------------------------------------------------------===#
# CUTLASS GemmUniversal-base kernels <-> StreamExecutor adaptor
#===--------------------------------------------------------------------------------------------===#

cc_library(
    name = "cutlass_gemm",
    srcs = ["cutlass_gemm.cc"],
    hdrs = ["cutlass_gemm.h"],
    deps = ["@local_tsl//tsl/platform:logging"],
)

cuda_library(
    name = "cutlass_gemm_adaptor",
    hdrs = ["cutlass_gemm_adaptor.cu.h"],
    copts = if_windows(
        [],
        ["-Wno-unknown-attributes"],
    ),  # __grid_constant__ is not supported by clang
    tags = ["cuda-only"],
    deps = [
        ":cutlass_gemm",
        "@cutlass_archive//:cutlass",
    ],
)

cuda_library(
    name = "cutlass_gemm_epilogue",
    tags = ["cuda-only"],
    # TODO(ezhulenev): Update to regular hdrs after fixing CUTLASS headers.
    textual_hdrs = ["cutlass_gemm_epilogue.cu.h"],
    deps = ["@cutlass_archive//:cutlass"],
)

#===--------------------------------------------------------------------------------------------===#
# CUTLASS Gemm kernels implementation
#===--------------------------------------------------------------------------------------------===#

# We split each individual kernel into a separate targets to compile them all in parallel. We also
# do not have any dependencies except CUTLASS itself to reduce the number of recompilations.

cc_library(
    name = "cutlass_gemm_kernels",
    tags = [
        "cuda-only",
        "gpu",
    ],
    deps = [
        ":cutlass_gemm_kernel_bf16xbf16_to_bf16",
        ":cutlass_gemm_kernel_bf16xbf16_to_f32",
        ":cutlass_gemm_kernel_bf16xs8_to_f32",
        ":cutlass_gemm_kernel_f32xbf16_to_f32",
        ":cutlass_gemm_kernel_f32xf32_to_f32",
    ],
)

# CUTLASS requires all loops to be unrolled, and in some kernels defined below we force Clang/LLVM
# to unroll them with extra compiler options because by default LLVM is not as aggressive with loop
# unrolling as NVCC.

# TODO(ezhulenev): Write a build rule to simplify kernel target declarations.

cuda_library(
    name = "cutlass_gemm_kernel_bf16xbf16_to_bf16",
    srcs = ["cutlass_gemm_kernel_bf16xbf16_to_bf16.cu.cc"],
    copts = [
        "-mllvm",
        "-unroll-threshold=100000",
    ] + if_windows(
        [],
        ["-Wno-unknown-attributes"],
    ),
    tags = ["cuda-only"],
    deps = [
        ":cutlass_gemm_adaptor",
        "@cutlass_archive//:cutlass",
        "@local_config_cuda//cuda:cuda_headers",
    ],
)

cuda_library(
    name = "cutlass_gemm_kernel_f32xf32_to_f32",
    srcs = ["cutlass_gemm_kernel_f32xf32_to_f32.cu.cc"],
    copts = if_windows(
        [],
        ["-Wno-unknown-attributes"],
    ),
    tags = ["cuda-only"],
    deps = [
        ":cutlass_gemm_adaptor",
        "@cutlass_archive//:cutlass",
        "@local_config_cuda//cuda:cuda_headers",
    ],
)

cuda_library(
    name = "cutlass_gemm_kernel_bf16xbf16_to_f32",
    srcs = ["cutlass_gemm_kernel_bf16xbf16_to_f32.cu.cc"],
    copts = [
        "-mllvm",
        "-unroll-threshold=100000",
    ] + if_windows(
        [],
        ["-Wno-unknown-attributes"],
    ),
    tags = ["cuda-only"],
    deps = [
        ":cutlass_gemm_adaptor",
        "@cutlass_archive//:cutlass",
        "@local_config_cuda//cuda:cuda_headers",
    ],
)

cuda_library(
    name = "cutlass_gemm_kernel_f32xbf16_to_f32",
    srcs = ["cutlass_gemm_kernel_f32xbf16_to_f32.cu.cc"],
    copts = [
        "-mllvm",
        "-unroll-threshold=100000",
    ] + if_windows(
        [],
        ["-Wno-unknown-attributes"],
    ),
    tags = ["cuda-only"],
    deps = [
        ":cutlass_gemm_adaptor",
        "@cutlass_archive//:cutlass",
        "@local_config_cuda//cuda:cuda_headers",
    ],
)

cuda_library(
    name = "cutlass_gemm_kernel_bf16xs8_to_f32",
    srcs = ["cutlass_gemm_kernel_bf16xs8_to_f32.cu.cc"],
    copts = ["-Wno-unknown-attributes -mllvm -unroll-threshold=100000"],
    tags = [
        "cuda-only",
        "gpu",
    ],
    deps = [
        ":cutlass_gemm_adaptor",
        "@cutlass_archive//:cutlass",
        "@local_config_cuda//cuda:cuda_headers",
    ],
)

#===--------------------------------------------------------------------------------------------===#
# CUTLASS Gemm kernel libraries
#===--------------------------------------------------------------------------------------------===#

cc_binary(
    name = "cutlass_gemm_kernel_f32xf32_to_f32.so",
    srcs = ["cutlass_gemm_kernel_f32xf32_to_f32.cc"],
    linkshared = True,
    linkstatic = False,
    tags = [
        "cuda-only",
        "gpu",
    ],
    deps = [":cutlass_gemm"],
)
