load("//xla:xla.bzl", "xla_cc_test")

package(
    # copybara:uncomment default_applicable_licenses = ["//tensorflow:license"],
    default_visibility = ["//xla/service/gpu/fusions:__pkg__"],
    licenses = ["notice"],
)

cc_library(
    name = "in_place_dynamic_update_slice",
    srcs = ["in_place_dynamic_update_slice.cc"],
    hdrs = ["in_place_dynamic_update_slice.h"],
    deps = [
        "//xla/hlo/ir:hlo",
        "//xla/service/gpu:hlo_fusion_analysis",
        "//xla/service/gpu:ir_emission_utils",
        "//xla/service/gpu:ir_emitter",
        "//xla/service/gpu:ir_emitter_context",
        "//xla/service/gpu:launch_dimensions",
        "//xla/service/gpu/fusions:fusion_emitter",
        "//xla/service/gpu/model:indexing_analysis",
        "//xla/service/llvm_ir:dynamic_update_slice_util",
        "//xla/service/llvm_ir:fused_ir_emitter",
        "//xla/service/llvm_ir:ir_array",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@llvm-project//llvm:Support",
        "@llvm-project//llvm:ir_headers",
        "@llvm-project//mlir:IR",
    ],
)

xla_cc_test(
    name = "in_place_dynamic_update_slice_test",
    srcs = ["in_place_dynamic_update_slice_test.cc"],
    deps = [
        ":in_place_dynamic_update_slice",
        "//xla/service/gpu:gpu_device_info_for_tests",
        "//xla/service/gpu:hlo_fusion_analysis",
        "//xla/service/gpu/fusions",
        "//xla/service/gpu/model:affine_map_printer",
        "//xla/service/gpu/model:indexing_test_utils",
        "//xla/stream_executor:device_description",
        "//xla/tests:hlo_test_base",
        "//xla/tests:xla_internal_test_main",
        "@com_google_googletest//:gtest",
        "@llvm-project//mlir:IR",
        "@local_tsl//tsl/platform:statusor",
    ],
)

cc_library(
    name = "loop",
    srcs = ["loop.cc"],
    hdrs = ["loop.h"],
    deps = [
        "//xla:shape_util",
        "//xla:util",
        "//xla/hlo/ir:hlo",
        "//xla/service/gpu:gpu_fusible",
        "//xla/service/gpu:hlo_fusion_analysis",
        "//xla/service/gpu:hlo_traversal",
        "//xla/service/gpu:ir_emission_utils",
        "//xla/service/gpu:ir_emitter",
        "//xla/service/gpu:ir_emitter_context",
        "//xla/service/gpu:launch_dimensions",
        "//xla/service/gpu:parallel_loop_emitter",
        "//xla/service/gpu/fusions:fusion_emitter",
        "//xla/service/gpu/model:indexing_analysis",
        "//xla/service/llvm_ir:fused_ir_emitter",
        "//xla/service/llvm_ir:ir_array",
        "@com_google_absl//absl/log",
        "@com_google_absl//absl/log:check",
        "@com_google_absl//absl/numeric:bits",
        "@com_google_absl//absl/status",
        "@llvm-project//llvm:ir_headers",
        "@llvm-project//mlir:IR",
        "@local_tsl//tsl/platform:macros",
        "@local_tsl//tsl/platform:statusor",
    ],
)

xla_cc_test(
    name = "loop_test",
    srcs = ["loop_test.cc"],
    deps = [
        "//xla:status_macros",
        "//xla/service/gpu:gpu_device_info_for_tests",
        "//xla/service/gpu:hlo_fusion_analysis",
        "//xla/service/gpu/fusions",
        "//xla/service/gpu/fusions:fusion_emitter",
        "//xla/service/gpu/model:affine_map_printer",
        "//xla/service/gpu/model:indexing_test_utils",
        "//xla/stream_executor:device_description",
        "//xla/tests:hlo_test_base",
        "//xla/tests:xla_internal_test_main",
        "@com_google_absl//absl/status:statusor",
        "@com_google_googletest//:gtest",
        "@llvm-project//mlir:IR",
        "@local_tsl//tsl/platform:statusor",
    ],
)

cc_library(
    name = "scatter",
    srcs = ["scatter.cc"],
    hdrs = ["scatter.h"],
    deps = [
        ":loop",
        "//xla:shape_util",
        "//xla:xla_data_proto_cc",
        "//xla/hlo/ir:hlo",
        "//xla/service/gpu:gpu_fusible",
        "//xla/service/gpu:hlo_fusion_analysis",
        "//xla/service/gpu:ir_emission_utils",
        "//xla/service/gpu:ir_emitter",
        "//xla/service/gpu:ir_emitter_context",
        "//xla/service/gpu:launch_dimensions",
        "//xla/service/gpu:parallel_loop_emitter",
        "//xla/service/gpu/fusions:fusion_emitter",
        "//xla/service/gpu/model:indexing_analysis",
        "//xla/service/llvm_ir:fused_ir_emitter",
        "//xla/service/llvm_ir:ir_array",
        "//xla/service/llvm_ir:llvm_util",
        "@com_google_absl//absl/log:check",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/types:span",
        "@llvm-project//llvm:Support",
        "@llvm-project//llvm:ir_headers",
        "@llvm-project//mlir:IR",
        "@local_tsl//tsl/platform:statusor",
    ],
)

xla_cc_test(
    name = "scatter_test",
    srcs = ["scatter_test.cc"],
    deps = [
        ":scatter",
        "//xla/service/gpu:gpu_device_info_for_tests",
        "//xla/service/gpu:hlo_fusion_analysis",
        "//xla/service/gpu/fusions",
        "//xla/service/gpu/model:affine_map_printer",
        "//xla/service/gpu/model:indexing_test_utils",
        "//xla/stream_executor:device_description",
        "//xla/tests:hlo_test_base",
        "//xla/tests:xla_internal_test_main",
        "@com_google_googletest//:gtest",
        "@llvm-project//mlir:IR",
        "@local_tsl//tsl/platform:statusor",
    ],
)

cc_library(
    name = "tiling_util",
    srcs = ["tiling_util.cc"],
    hdrs = ["tiling_util.h"],
    visibility = ["//xla/service/gpu:__subpackages__"],
    deps = [
        "//xla:shape_util",
        "//xla:util",
        "//xla:xla_data_proto_cc",
        "//xla/service/gpu:ir_emission_utils",
        "//xla/service/gpu:target_util",
        "//xla/service/gpu/model:indexing_analysis",
        "//xla/service/llvm_ir:ir_array",
        "//xla/service/llvm_ir:kernel_support_library",
        "//xla/service/llvm_ir:llvm_loop",
        "//xla/service/llvm_ir:llvm_util",
        "//xla/stream_executor:device_description",
        "@com_google_absl//absl/container:inlined_vector",
        "@com_google_absl//absl/log:check",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:str_format",
        "@com_google_absl//absl/types:span",
        "@llvm-project//llvm:Support",
        "@llvm-project//llvm:ir_headers",
        "@llvm-project//mlir:IR",
        "@local_tsl//tsl/platform:statusor",
    ],
)

cc_library(
    name = "reduction",
    srcs = ["reduction.cc"],
    hdrs = ["reduction.h"],
    deps = [
        ":tiling_util",
        "//xla:shape_util",
        "//xla:status_macros",
        "//xla:util",
        "//xla:xla_data_proto_cc",
        "//xla/hlo/ir:hlo",
        "//xla/service:buffer_assignment",
        "//xla/service/gpu:hlo_fusion_analysis",
        "//xla/service/gpu:hlo_traversal",
        "//xla/service/gpu:ir_emission_utils",
        "//xla/service/gpu:ir_emitter",
        "//xla/service/gpu:ir_emitter_context",
        "//xla/service/gpu:kernel_arguments",
        "//xla/service/gpu:kernel_reuse_cache",
        "//xla/service/gpu:launch_dimensions",
        "//xla/service/gpu:parallel_loop_emitter",
        "//xla/service/gpu:reduction_utils",
        "//xla/service/gpu:target_util",
        "//xla/service/gpu/fusions:fusion_emitter",
        "//xla/service/gpu/fusions:reduction_base",
        "//xla/service/gpu/fusions:thunk_util",
        "//xla/service/gpu/runtime:kernel_thunk",
        "//xla/service/gpu/runtime:thunk",
        "//xla/service/llvm_ir:fused_ir_emitter",
        "//xla/service/llvm_ir:ir_array",
        "//xla/service/llvm_ir:kernel_support_library",
        "//xla/service/llvm_ir:llvm_loop",
        "//xla/service/llvm_ir:llvm_util",
        "//xla/service/llvm_ir:loop_emitter",
        "//xla/stream_executor:device_description",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/container:flat_hash_set",
        "@com_google_absl//absl/container:inlined_vector",
        "@com_google_absl//absl/container:node_hash_map",
        "@com_google_absl//absl/log:check",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/types:span",
        "@llvm-project//llvm:Support",
        "@llvm-project//llvm:ir_headers",
        "@llvm-project//mlir:Support",
        "@local_tsl//tsl/platform:errors",
        "@local_tsl//tsl/platform:logging",
        "@local_tsl//tsl/platform:status",
        "@local_tsl//tsl/platform:statusor",
    ],
)

xla_cc_test(
    name = "reduction_test",
    srcs = ["reduction_test.cc"],
    deps = [
        ":reduction",
        "//xla/hlo/ir:hlo",
        "//xla/service/gpu:gpu_device_info_for_tests",
        "//xla/service/gpu:hlo_fusion_analysis",
        "//xla/service/gpu:ir_emitter_context",
        "//xla/service/gpu/fusions:fusion_emitter",
        "//xla/service/gpu/model:indexing_analysis",
        "//xla/service/gpu/model:indexing_test_utils",
        "//xla/stream_executor:device_description",
        "//xla/tests:hlo_test_base",
        "//xla/tests:xla_internal_test_main",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_googletest//:gtest",
        "@llvm-project//mlir:IR",
    ],
)

cc_library(
    name = "concatenate",
    srcs = ["concatenate.cc"],
    hdrs = ["concatenate.h"],
    deps = [
        "//xla:shape_util",
        "//xla/hlo/ir:hlo",
        "//xla/service/gpu:hlo_fusion_analysis",
        "//xla/service/gpu:ir_emission_utils",
        "//xla/service/gpu:ir_emitter",
        "//xla/service/gpu:ir_emitter_context",
        "//xla/service/gpu:launch_dimensions",
        "//xla/service/gpu:parallel_loop_emitter",
        "//xla/service/gpu/fusions:fusion_emitter",
        "//xla/service/gpu/model:indexing_analysis",
        "//xla/service/llvm_ir:fused_ir_emitter",
        "//xla/service/llvm_ir:ir_array",
        "//xla/service/llvm_ir:loop_emitter",
        "@com_google_absl//absl/algorithm:container",
        "@com_google_absl//absl/status",
        "@llvm-project//llvm:Support",
        "@llvm-project//llvm:ir_headers",
        "@llvm-project//mlir:IR",
        "@local_tsl//tsl/platform:errors",
        "@local_tsl//tsl/platform:statusor",
    ],
)

xla_cc_test(
    name = "concatenate_test",
    srcs = ["concatenate_test.cc"],
    deps = [
        ":concatenate",
        "//xla/service/gpu:gpu_device_info_for_tests",
        "//xla/service/gpu:hlo_fusion_analysis",
        "//xla/service/gpu/fusions",
        "//xla/service/gpu/model:affine_map_printer",
        "//xla/service/gpu/model:indexing_test_utils",
        "//xla/stream_executor:device_description",
        "//xla/tests:hlo_test_base",
        "//xla/tests:xla_internal_test_main",
        "@com_google_googletest//:gtest",
        "@llvm-project//mlir:IR",
    ],
)

cc_library(
    name = "transpose",
    srcs = ["transpose.cc"],
    hdrs = ["transpose.h"],
    deps = [
        ":tiling_util",
        "//xla:permutation_util",
        "//xla:shape_util",
        "//xla:xla_data_proto_cc",
        "//xla/hlo/ir:hlo",
        "//xla/service/gpu:hlo_fusion_analysis",
        "//xla/service/gpu:ir_emission_utils",
        "//xla/service/gpu:ir_emitter",
        "//xla/service/gpu:ir_emitter_context",
        "//xla/service/gpu:launch_dimensions",
        "//xla/service/gpu:target_util",
        "//xla/service/gpu/fusions:fusion_emitter",
        "//xla/service/gpu/model:indexing_analysis",
        "//xla/service/llvm_ir:fused_ir_emitter",
        "//xla/service/llvm_ir:ir_array",
        "//xla/service/llvm_ir:llvm_util",
        "//xla/service/llvm_ir:loop_emitter",
        "//xla/stream_executor:device_description",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/container:inlined_vector",
        "@com_google_absl//absl/log:check",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/types:span",
        "@llvm-project//llvm:Support",
        "@llvm-project//llvm:ir_headers",
        "@llvm-project//mlir:IR",
        "@local_tsl//tsl/platform:statusor",
    ],
)

xla_cc_test(
    name = "transpose_test",
    srcs = ["transpose_test.cc"],
    deps = [
        ":transpose",
        "//xla:status_macros",
        "//xla/service/gpu:gpu_device_info_for_tests",
        "//xla/service/gpu:hlo_fusion_analysis",
        "//xla/service/gpu/fusions",
        "//xla/service/gpu/model:indexing_test_utils",
        "//xla/stream_executor:device_description",
        "//xla/tests:hlo_test_base",
        "//xla/tests:xla_internal_test_main",
        "@com_google_absl//absl/status:statusor",
        "@com_google_googletest//:gtest",
        "@llvm-project//mlir:IR",
        "@local_tsl//tsl/platform:statusor",
    ],
)

cc_library(
    name = "input_slices",
    srcs = ["input_slices.cc"],
    hdrs = ["input_slices.h"],
    deps = [
        "//xla:shape_util",
        "//xla:util",
        "//xla/hlo/ir:hlo",
        "//xla/service:elemental_ir_emitter",
        "//xla/service/gpu:hlo_fusion_analysis",
        "//xla/service/gpu:ir_emission_utils",
        "//xla/service/gpu:ir_emitter",
        "//xla/service/gpu:ir_emitter_context",
        "//xla/service/gpu:launch_dimensions",
        "//xla/service/gpu:parallel_loop_emitter",
        "//xla/service/gpu/fusions:fusion_emitter",
        "//xla/service/gpu/model:indexing_analysis",
        "//xla/service/llvm_ir:fused_ir_emitter",
        "//xla/service/llvm_ir:ir_array",
        "//xla/service/llvm_ir:kernel_support_library",
        "//xla/service/llvm_ir:llvm_loop",
        "@com_google_absl//absl/log",
        "@com_google_absl//absl/log:check",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/types:span",
        "@llvm-project//llvm:ir_headers",
        "@llvm-project//mlir:IR",
        "@local_tsl//tsl/platform:statusor",
    ],
)

xla_cc_test(
    name = "input_slices_test",
    srcs = ["input_slices_test.cc"],
    deps = [
        ":input_slices",
        "//xla/service/gpu:gpu_device_info_for_tests",
        "//xla/service/gpu:hlo_fusion_analysis",
        "//xla/service/gpu/fusions",
        "//xla/service/gpu/model:affine_map_printer",
        "//xla/service/gpu/model:indexing_test_utils",
        "//xla/stream_executor:device_description",
        "//xla/tests:hlo_test_base",
        "//xla/tests:xla_internal_test_main",
        "@com_google_googletest//:gtest",
        "@llvm-project//mlir:IR",
    ],
)
