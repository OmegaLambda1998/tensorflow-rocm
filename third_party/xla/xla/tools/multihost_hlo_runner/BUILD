load("@bazel_skylib//rules:build_test.bzl", "build_test")
load("@local_config_cuda//cuda:build_defs.bzl", "if_cuda")
load("@local_config_rocm//rocm:build_defs.bzl", "if_rocm")
load("@local_tsl//tsl/platform:build_config_root.bzl", "tf_gpu_tests_tags")
load("@local_tsl//tsl/platform:rules_cc.bzl", "cc_library")
load("//xla:xla.bzl", "xla_cc_binary")
load("//xla/tests:build_defs.bzl", "xla_test")
load("//xla/tsl:tsl.bzl", "if_cuda_or_rocm", "if_google")

package(
    # copybara:uncomment default_applicable_licenses = ["//tensorflow:license"],
    default_visibility = ["//xla:internal"],
    licenses = ["notice"],
)

build_test(
    name = "hlo_runner_main_build_test",
    tags = [
        "cpu",
        "gpu",  # TODO(ddunleavy): this configuration of tags will only build on GPU in OSS.
    ],
    targets = [
        ":hlo_runner_main",
    ],
)

cc_library(
    name = "hlo_runner_main_lib",
    testonly = True,
    srcs = ["hlo_runner_main.cc"],
    compatible_with = None,
    tags = [
        "no_mac",
    ],
    deps = [
        ":create_client",
        ":functional_hlo_runner",
        "//xla:debug_options_flags",
        "//xla:status_macros",
        "//xla/pjrt:pjrt_client",
        "//xla/pjrt/distributed",
        "//xla/pjrt/distributed:client",
        "//xla/pjrt/distributed:key_value_store_interface",
        "//xla/pjrt/distributed:service",
        "//xla/service:cpu_plugin",
        "//xla/tsl/util:command_line_flags",
        "@com_google_absl//absl/log:check",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/time",
        "@local_tsl//tsl/platform:errors",
        "@local_tsl//tsl/platform:logging",
        "@local_tsl//tsl/platform:platform_port",
        "@local_tsl//tsl/platform:status",
        "@local_tsl//tsl/platform:statusor",
    ] + if_cuda_or_rocm([
        "//xla/service:gpu_plugin",
    ]) + if_cuda([
        "//xla/stream_executor:cuda_platform",
    ] + if_google(
        [
            "//third_party/py/jax/jaxlib:gpu_kernels",  # fixdeps: keep
        ],
    )) + if_rocm([
        "//xla/stream_executor:rocm_platform",
    ]),
    alwayslink = True,
)

xla_cc_binary(
    name = "hlo_runner_main",
    testonly = True,
    tags = [
        "no_mac",
    ],
    deps = [
        ":hlo_runner_main_lib",
    ],
)

xla_cc_binary(
    name = "hlo_runner_main_gpu",
    testonly = True,
    tags = [
        "cuda-only",
        "gpu",
        "no_mac",
    ] + tf_gpu_tests_tags(),
    deps = [
        ":hlo_runner_main_lib",
        "//xla/service:gpu_plugin",
        "//xla/stream_executor:cuda_platform",
    ] + if_google([
        "//third_party/py/jax/jaxlib:gpu_kernels",  # fixdeps: keep
    ]),
)

cc_library(
    name = "create_client",
    srcs = ["create_client.cc"],
    hdrs = ["create_client.h"],
    deps = [
        "//xla:status_macros",
        "//xla:xla_proto_cc",
        "//xla/pjrt:pjrt_client",
        "//xla/pjrt:pjrt_compiler",
        "//xla/pjrt/cpu:cpu_client",
        "//xla/pjrt/distributed",
        "//xla/pjrt/distributed:client",
        "//xla/pjrt/distributed:key_value_store_interface",
        "//xla/pjrt/distributed:service",
        "//xla/pjrt/gpu:se_gpu_pjrt_client",
        "@com_google_absl//absl/log",
        "@com_google_absl//absl/log:check",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings:string_view",
        "@com_google_absl//absl/time",
        "@local_tsl//tsl/platform:status",
        "@local_tsl//tsl/platform:statusor",
    ],
)

cc_library(
    name = "functional_hlo_runner",
    srcs = ["functional_hlo_runner.cc"],
    hdrs = ["functional_hlo_runner.h"],
    deps = [
        "//xla:literal",
        "//xla:literal_util",
        "//xla:shape_util",
        "//xla:status_macros",
        "//xla:util",
        "//xla:xla_data_proto_cc",
        "//xla:xla_proto_cc",
        "//xla/client:executable_build_options",
        "//xla/client:xla_computation",
        "//xla/hlo/ir:hlo",
        "//xla/hlo/pass:hlo_pass_pipeline",
        "//xla/pjrt:host_memory_spaces",
        "//xla/pjrt:pjrt_client",
        "//xla/pjrt:pjrt_compiler",
        "//xla/pjrt:pjrt_executable",
        "//xla/pjrt:pjrt_future",
        "//xla/pjrt/distributed:key_value_store_interface",
        "//xla/service:computation_layout",
        "//xla/service:computation_placer_hdr",
        "//xla/service:hlo_module_config",
        "//xla/service:hlo_parser",
        "//xla/service:hlo_proto_cc",
        "//xla/tests:test_utils",
        "//xla/tools:hlo_control_flow_flattening",
        "@com_google_absl//absl/algorithm:container",
        "@com_google_absl//absl/container:btree",
        "@com_google_absl//absl/log",
        "@com_google_absl//absl/log:check",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/synchronization",
        "@com_google_absl//absl/types:span",
        "@local_tsl//tsl/platform:env",
        "@local_tsl//tsl/platform:errors",
        "@local_tsl//tsl/platform:logging",
        "@local_tsl//tsl/platform:status",
        "@local_tsl//tsl/platform:statusor",
    ],
)

xla_test(
    name = "functional_hlo_runner_test",
    srcs = ["functional_hlo_runner_test.cc"],
    backend_tags = {
        "gpu": [
            "multi_gpu_h100",
            "no_oss",
            "nomsan",
        ],
    },
    backends = [
        "cpu",
        "gpu",
    ],
    data = [
        "data/multiple_gemm_fusions.hlo",
        "data/sharded_16_devices.hlo",
        "data/sharded_2_devices.hlo",
        "data/sharded_computation.hlo",
        "data/sharded_unoptimized_hlo_snapshot.pbtxt",
        "data/single_device.hlo",
        "data/single_device_tupled.hlo",
    ],
    tags = ["no_mac"],
    deps = [
        ":create_client",
        ":functional_hlo_runner",
        "//xla:debug_options_flags",
        "//xla:xla_proto_cc",
        "//xla/pjrt:pjrt_client",
        "//xla/tests:filecheck",
        "//xla/tsl/lib/core:status_test_util",
        "//xla/tsl/util:command_line_flags",
        "@com_google_absl//absl/log:check",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:str_format",
        "@com_google_absl//absl/time",
        "@com_google_googletest//:gtest",
        "@local_tsl//tsl/platform:env",
        "@local_tsl//tsl/platform:errors",
        "@local_tsl//tsl/platform:path",
        "@local_tsl//tsl/platform:statusor",
        "@local_tsl//tsl/platform:subprocess",
        "@local_tsl//tsl/platform:test",
    ],
)
