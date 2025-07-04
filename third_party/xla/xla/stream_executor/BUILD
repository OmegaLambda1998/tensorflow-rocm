load("@bazel_skylib//:bzl_library.bzl", "bzl_library")
load("@local_tsl//tsl/platform:build_config.bzl", "tf_proto_library")
load("@local_tsl//tsl/platform:build_config_root.bzl", "if_static")
load("@local_tsl//tsl/platform:rules_cc.bzl", "cc_library")
load("//xla:xla.bzl", "xla_cc_test")
load("//xla/stream_executor:build_defs.bzl", "stream_executor_build_defs_bzl_deps", "stream_executor_friends", "stream_executor_internal")
load("//xla/tsl:tsl.bzl", "if_google", "if_oss", "internal_visibility")
load("//xla/tsl:tsl.default.bzl", "filegroup")

package(
    # copybara:uncomment default_applicable_licenses = ["//tensorflow:license"],
    default_visibility = internal_visibility([":friends"]),
    licenses = ["notice"],
)

# StreamExecutor clients that can depend on targets defined in stream_executor package.
package_group(
    name = "friends",
    packages = stream_executor_friends(),
)

# StreamExecutor platform-dependent implementations. We restrict visibility of all internal
# implementation interfaces to internal users (everything in `stream_executor::internal` namespace).
package_group(
    name = "internal",
    packages = stream_executor_internal(),
)

bzl_library(
    name = "build_defs_bzl",
    srcs = ["build_defs.bzl"],
    deps = [
        "@local_config_cuda//cuda:build_defs_bzl",
        "@local_tsl//tsl/platform:rules_cc_bzl",
        "@local_tsl//tsl/platform/default:cuda_build_defs_bzl",
    ] + stream_executor_build_defs_bzl_deps(),
)

#===--------------------------------------------------------------------------------------------===#
# StreamExecutor public API
#===--------------------------------------------------------------------------------------------===#

# If you want to use StreamExecutor you have to depend on the "core" `xla/stream_executor` target
# defined below which exports StreamExecutor public API headers and also provides implemenetation.

# StreamExecutor also have a small number of public libraries that do not depend on StreamExecutor
# itself (e.g. `device_description`), this is mostly a result of StreamExecutor being a hardware
# abstraction layer for XLA (and Tensorflow).

# StreamExecutor itself is a small abstrtaction layer on top of platform-specific API
# implementations (e.g. see `stream_executor/cuda` folder for CUDA-specific details), and should
# not contribute a lot to binary size or compilation time.

# We bundle headers into filegroups for internal use only (we re-export the same set of headers
# from multiple targets), and all external clients should depend on one of the public `cc_library`
# targets that have dependencies required for compiling headers (e.g. absl dependencies). These
# filegroup roughly correspond to "StreamExecutor components" that are available to the clients.

# These are the headers that constitute StreamExecutor public API. Clients should not depend on
# this filegroup directly, but instead depend on a `stream_executor` target that implements these
# headers.
# TODO(ezhulenev): Remove from public API headers that are exported via standalone public libraries,
# e.g. `platform` and `platform_manager` should be added with an explicit dependency.
filegroup(
    name = "stream_executor_api_headers",
    srcs = [
        "device_description.h",
        "device_memory.h",
        "device_memory_allocator.h",
        "kernel.h",
        "launch_dim.h",
        "platform.h",
        "platform_manager.h",
        "scratch_allocator.h",
        "stream.h",
        "stream_executor.h",
    ],
    visibility = ["//visibility:private"],
)

cc_library(
    name = "stream_executor",
    hdrs = [
        ":stream_executor_api_headers",
    ],
    deps = [
        ":allocator_stats",
        ":blas",
        ":command_buffer",
        ":data_type",
        ":device_description_proto_cc",
        ":dnn",
        ":event",
        ":event_based_timer",
        ":fft",
        ":host_memory_allocation",  # build_cleaner: keep
        ":host_or_device_scalar",
        ":kernel_spec",
        ":memory_allocation",
        ":module_spec",
        ":numeric_options",
        ":platform",
        ":semantic_version",
        ":stream_common",
        ":stream_executor_common",
        ":stream_executor_h",
        "//xla/stream_executor/platform",
        "//xla/tsl/framework:device_id",
        "//xla/tsl/framework:device_type",
        "//xla/tsl/lib/gtl:int_type",
        "@com_google_absl//absl/algorithm:container",
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/container:inlined_vector",
        "@com_google_absl//absl/container:node_hash_map",
        "@com_google_absl//absl/functional:any_invocable",
        "@com_google_absl//absl/log",
        "@com_google_absl//absl/log:check",
        "@com_google_absl//absl/memory",
        "@com_google_absl//absl/meta:type_traits",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/synchronization",
        "@com_google_absl//absl/types:span",
        "@local_tsl//tsl/platform:env",
        "@local_tsl//tsl/platform:errors",
        "@local_tsl//tsl/platform:logging",
        "@local_tsl//tsl/platform:ml_dtypes",
        "@local_tsl//tsl/platform:status",
        "@local_tsl//tsl/platform:statusor",
        "@local_tsl//tsl/platform:thread_annotations",
        "@local_tsl//tsl/platform:types",
        "@local_tsl//tsl/protobuf:dnn_proto_cc",
    ] + if_static([
        ":stream_executor_impl",
    ]) + if_google([
        "@com_google_protobuf//:wrappers_cc_proto",  # indirectly-used by dnn.h
    ]),
)

#===--------------------------------------------------------------------------------------------===#
# StreamExecutor public libraries
#===--------------------------------------------------------------------------------------------===#

tf_proto_library(
    name = "device_description_proto",
    srcs = ["device_description.proto"],
    make_default_target_header_only = True,
    protodeps = ["//xla:autotune_results_proto"],
)

cc_library(
    name = "device_description",
    srcs = ["device_description.cc"],
    hdrs = ["device_description.h"],
    deps = [
        ":device_description_proto_cc",
        ":launch_dim",
        ":semantic_version",
        "//xla/tsl/lib/math:math_util",
        "@com_google_absl//absl/algorithm:container",
        "@com_google_absl//absl/strings",
        "@local_tsl//tsl/platform:logging",
    ],
)

cc_library(
    name = "device_memory",
    hdrs = ["device_memory.h"],
    deps = [
        "@com_google_absl//absl/base:core_headers",
        "@local_tsl//tsl/platform:logging",
    ],
)

cc_library(
    name = "device_memory_handle",
    srcs = ["device_memory_handle.cc"],
    hdrs = ["device_memory_handle.h"],
    deps = [
        ":device_memory",
        ":stream_executor_h",
    ],
)

cc_library(
    name = "module_spec",
    hdrs = ["module_spec.h"],
    deps = [
        "@com_google_absl//absl/types:span",
        "@local_tsl//tsl/platform:logging",
    ],
)

cc_library(
    name = "scoped_module_handle",
    hdrs = ["scoped_module_handle.h"],
    deps = [
        ":module_spec",
        ":stream_executor_h",
        "@com_google_absl//absl/log:check",
    ],
)

cc_library(
    name = "stream_finder",
    srcs = ["stream_finder.cc"],
    hdrs = ["stream_finder.h"],
    deps = [
        ":platform",
        ":stream",
        ":stream_executor_h",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
    ],
)

cc_library(
    name = "mock_platform",
    testonly = True,
    hdrs = ["mock_platform.h"],
    deps = [
        ":device_description",
        ":platform",
        ":stream_executor_h",
        "//xla:test",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
    ],
)

cc_library(
    name = "mock_stream",
    testonly = True,
    hdrs = ["mock_stream.h"],
    deps = [
        ":device_description",
        ":device_memory",
        ":event",
        ":event_based_timer",
        ":kernel",
        ":launch_dim",
        ":platform",
        ":stream",
        "//xla:test",
        "@com_google_absl//absl/functional:any_invocable",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings:string_view",
    ],
)

cc_library(
    name = "mock_stream_executor",
    testonly = True,
    hdrs = ["mock_stream_executor.h"],
    deps = [
        ":allocator_stats",
        ":blas",
        ":command_buffer",
        ":device_description",
        ":device_memory",
        ":dnn",
        ":event",
        ":fft",
        ":kernel",
        ":kernel_spec",
        ":launch_dim",
        ":memory_allocation",
        ":module_spec",
        ":platform",
        ":stream",
        ":stream_executor_h",
        "//xla:test",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/types:span",
    ] + if_google(["@com_google_protobuf//:wrappers_cc_proto"]),
)

cc_library(
    name = "data_type",
    hdrs = ["data_type.h"],
    deps = [
        "@local_tsl//tsl/platform:ml_dtypes",
        "@local_tsl//tsl/protobuf:dnn_proto_cc",
    ],
)

cc_library(
    name = "device_memory_allocator",
    hdrs = ["device_memory_allocator.h"],
    deps = [
        ":device_memory",
        ":platform",
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/log",
        "@com_google_absl//absl/log:check",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings:str_format",
        "@com_google_absl//absl/synchronization",
        "@com_google_absl//absl/types:span",
        "@local_tsl//tsl/platform:errors",
        "@local_tsl//tsl/platform:numbers",
        "@local_tsl//tsl/platform:status",
        "@local_tsl//tsl/platform:statusor",
    ],
)

cc_library(
    name = "stream_executor_memory_allocator",
    srcs = ["stream_executor_memory_allocator.cc"],
    hdrs = ["stream_executor_memory_allocator.h"],
    deps = [
        ":device_memory",
        ":device_memory_allocator",
        ":platform",
        ":stream",
        ":stream_executor_h",
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings:str_format",
        "@com_google_absl//absl/synchronization",
        "@com_google_absl//absl/types:span",
        "@local_tsl//tsl/platform:logging",
        "@local_tsl//tsl/platform:numbers",
        "@local_tsl//tsl/platform:statusor",
    ],
)

cc_library(
    name = "host_memory_allocation",
    srcs = ["host_memory_allocation.cc"],
    hdrs = ["host_memory_allocation.h"],
    deps = [
        ":memory_allocation",
        ":stream_executor_h",
    ],
)

cc_library(
    name = "host_or_device_scalar",
    hdrs = ["host_or_device_scalar.h"],
    deps = [
        ":device_memory",
        "@com_google_absl//absl/log:check",
    ],
)

cc_library(
    name = "launch_dim",
    hdrs = ["launch_dim.h"],
    deps = ["@com_google_absl//absl/strings"],
)

cc_library(
    name = "memory_allocation",
    hdrs = ["memory_allocation.h"],
)

cc_library(
    name = "platform_manager",
    srcs = ["platform_manager.cc"],
    hdrs = ["platform_manager.h"],
    deps = [
        ":platform",
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/log:check",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:str_format",
        "@com_google_absl//absl/synchronization",
        "@local_tsl//tsl/platform:errors",
        "@local_tsl//tsl/platform:statusor",
    ],
)

cc_library(
    name = "numeric_options",
    hdrs = ["numeric_options.h"],
)

cc_library(
    name = "platform",
    srcs = ["platform.cc"],
    hdrs = ["platform.h"],
    deps = [
        ":device_description",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
    ],
)

#===--------------------------------------------------------------------------------------------===#
# StreamExecutor plugins
#===--------------------------------------------------------------------------------------------===#

# TODO(ezhulenev): Today all StreamExecutor plugins are linked by default into the `stream_executor`
# target and leak into "core" APIs. We should decouple all plugins into optional dependencies, and
# make sure that they are not exposed via "core" APIs (se::Stream, se::StreamExecutor, etc.).

cc_library(
    name = "blas",
    srcs = ["blas.cc"],
    hdrs = ["blas.h"],
    deps = [
        ":data_type",
        ":device_memory",
        ":numeric_options",
        ":scratch_allocator",
        ":stream",
        "//xla/stream_executor/platform",
        "@com_google_absl//absl/log",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/types:span",
        "@local_tsl//tsl/platform:errors",
        "@local_tsl//tsl/protobuf:dnn_proto_cc",
    ],
)

cc_library(
    name = "dnn",
    srcs = ["dnn.cc"],
    hdrs = ["dnn.h"],
    deps = [
        ":data_type",
        ":device_description_proto_cc",
        ":device_memory",
        ":numeric_options",
        ":scratch_allocator",
        ":stream",
        "//xla/stream_executor/platform",
        "//xla/tsl/lib/strings:proto_serialization",
        "@com_google_absl//absl/algorithm:container",
        "@com_google_absl//absl/container:btree",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/log",
        "@com_google_absl//absl/log:check",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:str_format",
        "@com_google_absl//absl/types:span",
        "@eigen_archive//:eigen3",  # buildcleaner: keep
        "@local_tsl//tsl/platform:logging",
        "@local_tsl//tsl/platform:ml_dtypes",
        "@local_tsl//tsl/platform:status",
        "@local_tsl//tsl/platform:statusor",
        "@local_tsl//tsl/protobuf:dnn_proto_cc",
    ] + if_google(["@com_google_protobuf//:wrappers_cc_proto"]),
)

cc_library(
    name = "fft",
    hdrs = ["fft.h"],
    deps = [
        "//xla/stream_executor/platform",
    ],
)

cc_library(
    name = "lazy_op_runner",
    hdrs = ["lazy_op_runner.h"],
    deps = [
        ":dnn",
        ":stream",
        ":stream_executor_h",
        "@com_google_absl//absl/base",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@local_tsl//tsl/platform:statusor",
        "@local_tsl//tsl/protobuf:dnn_proto_cc",
    ],
)

#===--------------------------------------------------------------------------------------------===#
# StreamExecutor platform-dependent interfaces
#===--------------------------------------------------------------------------------------------===#

# Only platform-dependent StreamExecutor implementations (e.g. StreamExecutor for GPUs) and targets
# defined by StreamExecutor itself (e.g. `event`, `kernel`, etc.) can depend on internal
# interfaces (interfaces that define platform-specific API).
#
# External clients of StreamExecutor should depend on `stream_executor` target (links StreamExecutor
# implementation in static build configuration).

cc_library(
    name = "stream_executor_h",
    hdrs = [
        "stream_executor.h",
    ],
    deps = [
        ":allocator_stats",
        ":blas",
        ":command_buffer",
        ":device_memory",
        ":dnn",
        ":event",
        ":fft",
        ":kernel",
        ":kernel_spec",
        ":memory_allocation",
        ":module_spec",
        ":platform",
        ":stream",
        "@com_google_absl//absl/log",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/types:span",
    ],
)

cc_library(
    name = "event",
    hdrs = ["event.h"],
    deps = [
        "@com_google_absl//absl/status",
    ],
)

cc_library(
    name = "stream",
    hdrs = ["stream.h"],
    deps = [
        ":device_description",
        ":device_memory",
        ":event",
        ":event_based_timer",
        ":kernel",
        ":launch_dim",
        ":platform",
        "@com_google_absl//absl/functional:any_invocable",
        "@com_google_absl//absl/log:check",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings:string_view",
        "@com_google_absl//absl/types:span",
    ],
)

#===--------------------------------------------------------------------------------------------===#
# StreamExecutor internal implementation (visible to StreamExecutor platform implementations)
#===--------------------------------------------------------------------------------------------===#

cc_library(
    name = "plugin_registry",
    srcs = ["plugin_registry.cc"],
    hdrs = ["plugin_registry.h"],
    visibility = [":internal"],
    deps = [
        ":blas",
        ":dnn",
        ":fft",
        ":platform",
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/log",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings:str_format",
        "@com_google_absl//absl/synchronization",
    ],
)

#===--------------------------------------------------------------------------------------------===#
# StreamExecutor private implementation (has private visibility)
#===--------------------------------------------------------------------------------------------===#

cc_library(
    name = "allocator_stats",
    srcs = ["allocator_stats.cc"],
    hdrs = ["allocator_stats.h"],
    visibility = [":internal"],
    deps = ["@com_google_absl//absl/strings:str_format"],
)

cc_library(
    name = "event_based_timer",
    hdrs = ["event_based_timer.h"],
    deps = [
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/time",
    ],
)

cc_library(
    name = "command_buffer",
    hdrs = ["command_buffer.h"],
    visibility = [":internal"],
    deps = [
        ":device_memory",
        ":kernel",
        ":launch_dim",
        ":platform",
        "//xla/tsl/lib/gtl:int_type",
        "@com_google_absl//absl/functional:any_invocable",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/types:span",
        "@local_tsl//tsl/platform:errors",
    ],
    alwayslink = True,
)

cc_library(
    name = "trace_command_buffer_factory",
    srcs = ["trace_command_buffer_factory.cc"],
    hdrs = ["trace_command_buffer_factory.h"],
    visibility = [":internal"],
    deps = [
        ":command_buffer",
        ":stream",
        ":stream_executor_h",
        "@com_google_absl//absl/functional:any_invocable",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@local_tsl//tsl/platform:errors",
        "@local_tsl//tsl/platform:statusor",
    ],
)

cc_library(
    name = "executor_cache",
    srcs = ["executor_cache.cc"],
    hdrs = ["executor_cache.h"],
    deps = [
        ":stream_executor_h",
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings:str_format",
        "@com_google_absl//absl/synchronization",
        "@local_tsl//tsl/platform:logging",
        "@local_tsl//tsl/platform:statusor",
    ],
    alwayslink = True,
)

cc_library(
    name = "kernel_spec",
    srcs = ["kernel_spec.cc"],
    hdrs = ["kernel_spec.h"],
    deps = [
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/types:span",
        "@local_tsl//tsl/platform:logging",
    ],
)

cc_library(
    name = "kernel",
    srcs = ["kernel.cc"],
    hdrs = ["kernel.h"],
    deps = [
        ":device_memory",
        ":kernel_spec",
        ":launch_dim",
        "@com_google_absl//absl/container:inlined_vector",
        "@com_google_absl//absl/meta:type_traits",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/types:span",
        "@local_tsl//tsl/platform:logging",
    ],
    alwayslink = True,
)

cc_library(
    name = "typed_kernel_factory",
    hdrs = ["typed_kernel_factory.h"],
    deps = [
        ":kernel",
        ":kernel_spec",
        ":stream_executor_h",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings:string_view",
        "@com_google_absl//absl/types:span",
        "@local_tsl//tsl/platform:errors",
        "@local_tsl//tsl/platform:statusor",
    ],
)

cc_library(
    name = "semantic_version",
    srcs = ["semantic_version.cc"],
    hdrs = ["semantic_version.h"],
    deps = [
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:str_format",
        "@com_google_absl//absl/strings:string_view",
        "@local_tsl//tsl/platform:statusor",
    ],
)

cc_test(
    name = "semantic_version_test",
    srcs = ["semantic_version_test.cc"],
    deps = [
        ":semantic_version",
        "@com_google_absl//absl/hash:hash_testing",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:string_view",
        "@com_google_googletest//:gtest_main",
        "@local_tsl//tsl/platform:status_matchers",
        "@local_tsl//tsl/platform:test",
        "@local_tsl//tsl/platform:test_main",
    ],
)

cc_library(
    name = "scratch_allocator",
    hdrs = ["scratch_allocator.h"],
    deps = [
        ":device_memory",
        ":device_memory_allocator",
        "@com_google_absl//absl/container:inlined_vector",
        "@com_google_absl//absl/status:statusor",
        "@local_tsl//tsl/platform:statusor",
    ],
)

cc_library(
    name = "stream_executor_common",
    srcs = ["stream_executor_common.cc"],
    hdrs = ["stream_executor_common.h"],
    deps = [
        ":device_description",
        ":platform",
        ":stream_executor_h",
        "//xla/tsl/util:env_var",
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/log",
        "@com_google_absl//absl/log:check",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/synchronization",
        "@local_tsl//tsl/platform:status",
    ],
)

cc_library(
    name = "stream_common",
    srcs = ["stream_common.cc"],
    hdrs = ["stream_common.h"],
    deps = [
        ":blas",
        ":device_description",
        ":fft",
        ":platform",
        ":stream",
        ":stream_executor_h",
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/log:check",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings:str_format",
        "@com_google_absl//absl/strings:string_view",
        "@com_google_absl//absl/synchronization",
        "@local_tsl//tsl/platform:logging",
        "@local_tsl//tsl/platform:statusor",
        "@local_tsl//tsl/platform:thread_annotations",
    ],
)
#===--------------------------------------------------------------------------------------------===#

# We have a separate `stream_executor_impl` target because in open source we are building multiple
# shared libraries and then link them together (this is an implementation detail of Tensorflow
# framework), and we take extra care not to define symbols in multiple objects. Otherwise we can
# end up with static singletons declared in multiple objects, ODR violations, and many other bad
# things that lead to nearly impossible to debug run time crashes.
cc_library(
    name = "stream_executor_impl",
    deps = [
        ":allocator_stats",
        ":blas",  # build_cleaner: keep
        ":command_buffer",  # build_cleaner: keep
        ":device_description",
        ":device_memory",
        ":dnn",
        ":executor_cache",
        ":fft",
        ":host_memory_allocation",  # build_cleaner: keep
        ":kernel",
        ":kernel_spec",
        ":launch_dim",
        ":platform",
        ":platform_manager",
        ":scratch_allocator",
        ":stream_common",
        ":stream_executor_common",
        ":stream_executor_h",
    ] + if_oss(["@local_tsl//tsl/protobuf:dnn_proto_cc_impl"]),
)

#===--------------------------------------------------------------------------------------------===#
# StreamExecutor tests
#===--------------------------------------------------------------------------------------------===#

xla_cc_test(
    name = "kernel_test",
    srcs = ["kernel_test.cc"],
    deps = [
        ":device_memory",
        ":kernel_spec",
        ":stream_executor",
        ":typed_kernel_factory",
        "//xla/stream_executor/host:host_platform",
        "@local_tsl//tsl/platform:test",
        "@local_tsl//tsl/platform:test_benchmark",
        "@local_tsl//tsl/platform:test_main",
    ],
)

xla_cc_test(
    name = "stream_executor_test",
    srcs = ["stream_executor_test.cc"],
    deps = [
        ":stream_executor",
        "//xla/stream_executor/host:host_platform",
        "@com_google_absl//absl/status:statusor",
        "@local_tsl//tsl/platform:statusor",
        "@local_tsl//tsl/platform:test",
        "@local_tsl//tsl/platform:test_main",
    ],
)

xla_cc_test(
    name = "stream_test",
    size = "small",
    srcs = ["stream_test.cc"],
    deps = [
        ":stream_executor",
        "//xla/stream_executor/host:host_platform",
        "@com_google_absl//absl/log:check",
        "@local_tsl//tsl/platform:statusor",
        "@local_tsl//tsl/platform:test",
        "@local_tsl//tsl/platform:test_main",
    ],
)

xla_cc_test(
    name = "dnn_test",
    size = "small",
    srcs = ["dnn_test.cc"],
    deps = [
        ":dnn",
        "@local_tsl//tsl/platform:test",
        "@local_tsl//tsl/platform:test_main",
    ],
)

xla_cc_test(
    name = "scoped_module_handle_test",
    srcs = ["scoped_module_handle_test.cc"],
    deps = [
        ":device_description",
        ":mock_stream_executor",
        ":module_spec",
        ":scoped_module_handle",
        "@com_google_absl//absl/status",
        "@local_tsl//tsl/platform:test",
        "@local_tsl//tsl/platform:test_main",
    ],
)

xla_cc_test(
    name = "device_memory_handle_test",
    srcs = ["device_memory_handle_test.cc"],
    deps = [
        ":device_memory",
        ":device_memory_handle",
        ":mock_stream_executor",
        "@local_tsl//tsl/platform:test",
        "@local_tsl//tsl/platform:test_main",
    ],
)

xla_cc_test(
    name = "device_description_test",
    srcs = ["device_description_test.cc"],
    deps = [
        ":device_description",
        ":semantic_version",
        "@local_tsl//tsl/platform:test",
        "@local_tsl//tsl/platform:test_main",
    ],
)

xla_cc_test(
    name = "executor_cache_test",
    srcs = ["executor_cache_test.cc"],
    deps = [
        ":executor_cache",
        ":mock_stream_executor",
        ":stream",
        "@com_google_absl//absl/log",
        "@local_tsl//tsl/platform:statusor",
        "@local_tsl//tsl/platform:test",
        "@local_tsl//tsl/platform:test_main",
    ],
)

xla_cc_test(
    name = "stream_finder_test",
    srcs = ["stream_finder_test.cc"],
    deps = [
        ":mock_platform",
        ":mock_stream",
        ":mock_stream_executor",
        ":stream_finder",
        "//xla:test",
        "@com_google_absl//absl/status",
        "@local_tsl//tsl/platform:statusor",
        "@local_tsl//tsl/platform:test",
        "@local_tsl//tsl/platform:test_main",
    ],
)

#===--------------------------------------------------------------------------------------------===#
# Aliases for StreamExecutor platforms
#===--------------------------------------------------------------------------------------------===#

alias(
    name = "cuda_platform",
    actual = "//xla/stream_executor/cuda:all_runtime",
)

alias(
    name = "rocm_platform",
    actual = "//xla/stream_executor/rocm:all_runtime",
)

alias(
    name = "sycl_platform",
    actual = "//xla/stream_executor/sycl:all_runtime",
)
