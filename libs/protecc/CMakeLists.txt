# Project setup
project(protecc C)

include(ChefBpf)

chef_bpf_init_cache_vars()

# Create the protecc library
add_library(protecc STATIC
    matchers/dfa.c
    matchers/glob.c
    matchers/trie.c
    profiles/paths/dfa.c
    profiles/paths/dfatrie.c
    profiles/paths/node.c
    profiles/paths/parser.c
    profiles/paths/trie.c
    profiles/rules/dfa.c
    profiles/builder.c
    profiles/mount.c
    profiles/net.c
    profiles/path.c
    profiles/utils.c
    protecc.c
)

# Include directories
target_include_directories(protecc PUBLIC include)
target_include_directories(protecc PRIVATE .)
target_link_libraries(protecc PUBLIC platform)

# Add tests if building tests is enabled
option(PROTECC_BUILD_TESTS "Build protecc tests" ON)
option(PROTECC_BUILD_EBPF_VERIFY_TESTS "Build and run protecc eBPF verifier smoke test" ON)

if(PROTECC_BUILD_TESTS)
    # Test executable
    add_executable(protecc_test
        tests/test_main.c
        tests/test_basic.c
        tests/test_wildcards.c
        tests/test_charset.c
        tests/test_dfa.c
        tests/test_trie.c
        tests/test_profile_builder.c
        tests/test_profile_builder_paths.c
        tests/test_profile_builder_serialization.c
        tests/test_profile_builder_negatives.c
        tests/test_profile_builder_net.c
        tests/test_profile_builder_mount.c
    )

    target_link_libraries(protecc_test PRIVATE protecc)
    target_include_directories(protecc_test PRIVATE include .)

    add_test(NAME protecc_test COMMAND protecc_test)

    if(PROTECC_BUILD_EBPF_VERIFY_TESTS)
        if(WIN32)
            message(STATUS "protecc_bpf_verify is disabled on Windows")
        else()
        find_program(PROTECC_CLANG clang)
        find_program(PROTECC_BPFTOOL bpftool)
        chef_bpf_probe_bpftool("${PROTECC_BPFTOOL}" PROTECC_BPFTOOL_USABLE PROTECC_BPFTOOL_CAN_DUMP_BTF)
        chef_bpf_check_bpffs(PROTECC_BPFFS_AVAILABLE)
        set(PROTECC_BPF_SMOKE_OBJECTS_ARG "")

        # Keep this target available for pipelines that build it explicitly.
        add_custom_target(protecc_bpf_verify_obj)

        if(NOT PROTECC_BPFFS_AVAILABLE)
            message(STATUS "bpffs is not available at ${CHEF_BPFFS_PATH}; protecc_bpf_verify will be configured as skip-only")
        endif()

        if(PROTECC_BPFTOOL AND NOT PROTECC_BPFTOOL_CAN_DUMP_BTF)
            message(STATUS "bpftool is present but not usable for kernel BTF at ${CHEF_BTF_FILE}; protecc_bpf_verify will be configured as skip-only")
        endif()

        if(PROTECC_CLANG AND PROTECC_BPFTOOL_USABLE AND PROTECC_BPFFS_AVAILABLE)
            chef_bpf_detect_target_arch(PROTECC_BPF_ARCH)
            if(PROTECC_BPF_ARCH STREQUAL "x86" AND NOT CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|amd64|AMD64|x86|i386|i686")
                message(WARNING "Unknown architecture ${CMAKE_SYSTEM_PROCESSOR}, defaulting to x86 for protecc BPF tests")
            endif()

            set(PROTECC_BPF_TEST_DIR ${CMAKE_CURRENT_BINARY_DIR}/tests/bpf)
            set(PROTECC_BPF_VMLINUX ${PROTECC_BPF_TEST_DIR}/vmlinux.h)
            set(PROTECC_BPF_SMOKE_SOURCES
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/bpf/protecc-verifier-path.bpf.c
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/bpf/protecc-verifier-net.bpf.c
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/bpf/protecc-verifier-mount.bpf.c
            )
            file(MAKE_DIRECTORY ${PROTECC_BPF_TEST_DIR})

            chef_bpf_prepare_vmlinux_header("${PROTECC_BPFTOOL}" ${PROTECC_BPFTOOL_CAN_DUMP_BTF} ${PROTECC_BPF_VMLINUX} PROTECC_BPF_VMLINUX_READY)
            if(NOT PROTECC_BPF_VMLINUX_READY)
                message(STATUS "protecc_bpf_verify will run in skip-only mode (failed to prepare vmlinux.h)")
                set(PROTECC_BPF_SMOKE_OBJECTS_ARG "")
            else()
                set(PROTECC_BPF_INCLUDE_DIRS
                    ${PROTECC_BPF_TEST_DIR}
                    ${CMAKE_CURRENT_SOURCE_DIR}/include
                    /usr/include
                )

                chef_bpf_add_object_commands(
                    CLANG "${PROTECC_CLANG}"
                    TARGET_ARCH "${PROTECC_BPF_ARCH}"
                    OUTPUT_DIR "${PROTECC_BPF_TEST_DIR}"
                    VMLINUX_HEADER "${PROTECC_BPF_VMLINUX}"
                    SOURCES ${PROTECC_BPF_SMOKE_SOURCES}
                    INCLUDE_DIRS ${PROTECC_BPF_INCLUDE_DIRS}
                    OUT_OBJECTS PROTECC_BPF_SMOKE_OBJECTS
                )

                add_custom_target(protecc_bpf_verify_obj_build DEPENDS ${PROTECC_BPF_SMOKE_OBJECTS})
                add_dependencies(protecc_bpf_verify_obj protecc_bpf_verify_obj_build)

                string(JOIN ";" PROTECC_BPF_SMOKE_OBJECTS_ARG ${PROTECC_BPF_SMOKE_OBJECTS})
            endif()
        else()
            message(STATUS "protecc_bpf_verify will run in skip-only mode (BPF compile prerequisites unavailable or bpffs missing at ${CHEF_BPFFS_PATH})")
        endif()

        add_test(
            NAME protecc_bpf_verify
            COMMAND /bin/sh ${CMAKE_CURRENT_SOURCE_DIR}/tests/bpf/verify_protecc_bpf.sh ${PROTECC_BPF_SMOKE_OBJECTS_ARG}
        )
        set_tests_properties(
            protecc_bpf_verify
            PROPERTIES
                SKIP_RETURN_CODE 77
                ENVIRONMENT "CHEF_BPFFS_PATH=${CHEF_BPFFS_PATH};CHEF_BTF_FILE=${CHEF_BTF_FILE}"
        )
        endif()
    endif()
endif()
