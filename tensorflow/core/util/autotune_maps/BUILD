# Defines data structures that store autotuning results.
# The autotuning results can be serialized as a string, allowing you to save
# and later restore them.

# Placeholder: load py_proto_library
load(
    "//tensorflow:tensorflow.bzl",
    "tf_cc_test",
    "tf_cuda_library",
    "tf_cuda_only_cc_test",
)

# TODO(ruochengw): Currently only supports contrib's fused_conv2d_bias_activation_op.
# We plan to add more ops and move fused_conv2d_bias_activation_op back into core library.
load(
    "//tensorflow/core/platform:build_config.bzl",
    "tf_proto_library",
    # copybara:uncomment "tf_pyclif_proto_library",
)
load(
    "//tensorflow/core/platform:rules_cc.bzl",
    "cc_library",
)

package(
    # copybara:uncomment default_applicable_licenses = ["//tensorflow:license"],
    default_visibility = ["//tensorflow:__subpackages__"],
    licenses = ["notice"],
)

cc_library(
    name = "conv_autotune_maps",
    hdrs = [
        "conv_autotune_maps.h",
    ],
    deps = [
        "//tensorflow/core/kernels:gpu_util_hdrs",
    ],
)

cc_library(
    name = "conv_map_wrapper",
    srcs = ["conv_map_wrapper.cc"],
    hdrs = ["conv_map_wrapper.h"],
    visibility = [
        "//tensorflow:__subpackages__",
        "//waymo/ml/deploy:__subpackages__",
    ],
    deps = [
        ":autotune_map_proto_cc",
        ":conv_parameters_proto_cc",
        "@com_google_absl//absl/log:check",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@local_tsl//tsl/protobuf:dnn_proto_cc",
        "@local_xla//xla/tsl/lib/strings:proto_serialization",
    ],
)

tf_cc_test(
    name = "conv_map_wrapper_test",
    srcs = ["conv_map_wrapper_test.cc"],
    deps = [
        ":autotune_map_proto_cc",
        ":conv_map_wrapper",
        ":conv_parameters_proto_cc",
        "@com_google_googletest//:gtest_main",
        "@local_tsl//tsl/platform:statusor",
        "@local_tsl//tsl/protobuf:dnn_proto_cc",
        "@local_xla//xla:test",
    ],
)

tf_proto_library(
    name = "conv_parameters_proto",
    srcs = [
        "conv_parameters.proto",
    ],
    protodeps = [
        "//tensorflow/core/framework:types_proto",
        "@local_tsl//tsl/protobuf:dnn_proto",
    ],
)

# This ensures the compilation of custom op fused_conv2d_bias_activation_op.
# TODO(ruochengw): Remove this target once we move fused_conv2d_bias_activation_op back in core library.
cc_library(
    name = "conv_parameters_hdrs",
    hdrs = ["conv_parameters.h"],
    deps = [
        ":conv_parameters_proto_cc",
    ],
)

# For a more maintainable build this target should not exist and the headers
# should  be split into the existing cc_library targets, but this change was
# automatically  done so that we can remove long standing issues and complexity
# in the build system. It's up to the OWNERS of this package to get rid of it or
# not. The use of the textual_hdrs attribute is discouraged, use hdrs instead.
# Here it is used to avoid header parsing errors in packages where the feature
# parse_headers was enabled since loose headers were not being parsed. See
# go/loose-lsc-one-target-approach for more details.
cc_library(
    name = "loose_headers",
    tags = ["avoid_dep"],
    textual_hdrs = [
        "conv_autotune_maps.h",
        "conv_parameters.h",
    ],
    visibility = ["//visibility:private"],
)

tf_cuda_library(
    name = "conv_parameters",
    srcs = ["conv_parameters.cc"],
    hdrs = [
        "conv_parameters.h",
    ],
    cuda_deps = [
        "@local_xla//xla/tsl/lib/strings:proto_serialization",
    ],
    deps = [
        ":conv_parameters_proto_cc",
        "//tensorflow/core/platform:hash",
        "//tensorflow/core/platform:protobuf",
        "//tensorflow/core/platform:stream_executor",
        "@com_google_absl//absl/strings:str_format",
        "@com_google_absl//absl/types:optional",
    ],
)

tf_proto_library(
    name = "autotune_map_proto",
    srcs = [
        "autotune_map.proto",
    ],
    protodeps = [
        "//tensorflow/core/util/autotune_maps:conv_parameters_proto",
        "@local_tsl//tsl/protobuf:dnn_proto",
    ],
    visibility = [
        "//waymo/ml/deploy/benchmark:__subpackages__",
        "//waymo/ml/deploy/system/autotuning:__subpackages__",
    ],
)

# copybara:uncomment_begin(google-only)
# py_proto_library(
#     name = "autotune_map_py_pb2",
#     has_services = 0,
#     api_version = 2,
#     visibility = ["//waymo/ml/deploy/system/autotuning:__subpackages__"],
#     deps = [":autotune_map_proto"],
# )
#
# tf_pyclif_proto_library(
#     name = "autotune_map_pyclif",
#     proto_lib = ":autotune_map_proto",
#     visibility = [
#         "//waymo/ml/deploy/benchmark:__subpackages__",
#         "//waymo/ml/deploy/system/autotuning:__subpackages__",
#     ],
# )
# copybara:uncomment_end

tf_cuda_library(
    name = "autotune_serialize",
    srcs = [
        "autotune_serialize.cc",
    ],
    hdrs = [
        "autotune_serialize.h",
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":autotune_map_proto_cc",
        ":conv_autotune_maps",
        ":conv_parameters",
        ":conv_parameters_proto_cc",
        "//tensorflow/core:framework",
        "//tensorflow/core/platform:status",
        "//tensorflow/core/platform:str_util",
        "@local_tsl//tsl/protobuf:dnn_proto_cc",
        "@local_xla//xla:status_macros",
        "@local_xla//xla/stream_executor:dnn",
        "@local_xla//xla/stream_executor:platform_manager",
        "@local_xla//xla/stream_executor/gpu:gpu_init",
        "@local_xla//xla/tsl/lib/strings:proto_serialization",
    ],
)

tf_cuda_only_cc_test(
    name = "autotune_serialize_test",
    size = "small",
    srcs = ["autotune_serialize_test.cc"],
    features = ["-layering_check"],
    tags = ["cuda-only"],
    deps = [
        ":autotune_serialize",
        ":conv_autotune_maps",
        ":conv_parameters",
        ":conv_parameters_proto_cc",
        "//tensorflow/core:test",
        "//tensorflow/core:test_main",
        "//tensorflow/core/platform:status_matchers",
        "@local_xla//xla/stream_executor/gpu:gpu_init",
    ],
)
