# Project setup
project(protecc C)

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
        find_program(PROTECC_CLANG clang)
        find_program(PROTECC_BPFTOOL bpftool)
        set(PROTECC_BPFTOOL_USABLE FALSE)
        set(PROTECC_BPF_SMOKE_OBJECTS_ARG "")

        # Keep this target available for pipelines that build it explicitly.
        add_custom_target(protecc_bpf_verify_obj)

        if(PROTECC_BPFTOOL AND EXISTS /sys/kernel/btf/vmlinux)
            execute_process(
                COMMAND ${PROTECC_BPFTOOL} btf dump file /sys/kernel/btf/vmlinux format c
                OUTPUT_QUIET
                ERROR_QUIET
                RESULT_VARIABLE PROTECC_BPFTOOL_CHECK_RC
            )

            if(PROTECC_BPFTOOL_CHECK_RC EQUAL 0)
                set(PROTECC_BPFTOOL_USABLE TRUE)
            else()
                message(STATUS "bpftool is present but not usable for this kernel; protecc_bpf_verify will be configured as skip-only")
            endif()
        endif()

        if(PROTECC_CLANG AND PROTECC_BPFTOOL_USABLE)
            if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|amd64|AMD64")
                set(PROTECC_BPF_ARCH "x86")
            elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
                set(PROTECC_BPF_ARCH "arm64")
            elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "arm")
                set(PROTECC_BPF_ARCH "arm")
            elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "riscv64")
                set(PROTECC_BPF_ARCH "riscv")
            else()
                message(WARNING "Unknown architecture ${CMAKE_SYSTEM_PROCESSOR}, defaulting to x86 for protecc BPF tests")
                set(PROTECC_BPF_ARCH "x86")
            endif()

            set(PROTECC_BPF_TEST_DIR ${CMAKE_CURRENT_BINARY_DIR}/tests/bpf)
            set(PROTECC_BPF_VMLINUX ${PROTECC_BPF_TEST_DIR}/vmlinux.h)
            set(PROTECC_BPF_SMOKE_SOURCES
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/bpf/protecc-verifier-path.bpf.c
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/bpf/protecc-verifier-net.bpf.c
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/bpf/protecc-verifier-mount.bpf.c
            )
            set(PROTECC_BPF_SMOKE_OBJECTS "")
            file(MAKE_DIRECTORY ${PROTECC_BPF_TEST_DIR})

            add_custom_command(
                OUTPUT ${PROTECC_BPF_VMLINUX}
                COMMAND ${PROTECC_BPFTOOL} btf dump file /sys/kernel/btf/vmlinux format c > ${PROTECC_BPF_VMLINUX}
                COMMENT "Generating protecc BPF vmlinux.h"
                VERBATIM
            )

            foreach(PROTECC_BPF_SMOKE_SRC ${PROTECC_BPF_SMOKE_SOURCES})
                get_filename_component(PROTECC_BPF_SMOKE_NAME ${PROTECC_BPF_SMOKE_SRC} NAME_WE)
                set(PROTECC_BPF_SMOKE_OBJ ${PROTECC_BPF_TEST_DIR}/${PROTECC_BPF_SMOKE_NAME}.bpf.o)
                list(APPEND PROTECC_BPF_SMOKE_OBJECTS ${PROTECC_BPF_SMOKE_OBJ})

                add_custom_command(
                    OUTPUT ${PROTECC_BPF_SMOKE_OBJ}
                    COMMAND ${PROTECC_CLANG} -g -O2 -target bpf
                        -D__TARGET_ARCH_${PROTECC_BPF_ARCH}
                        -I${PROTECC_BPF_TEST_DIR}
                        -I${CMAKE_CURRENT_SOURCE_DIR}/include
                        -I/usr/include
                        -c ${PROTECC_BPF_SMOKE_SRC}
                        -o ${PROTECC_BPF_SMOKE_OBJ}
                    DEPENDS ${PROTECC_BPF_SMOKE_SRC} ${PROTECC_BPF_VMLINUX}
                    COMMENT "Building protecc eBPF verifier smoke object: ${PROTECC_BPF_SMOKE_NAME}"
                    VERBATIM
                )
            endforeach()

            add_custom_target(protecc_bpf_verify_obj_build DEPENDS ${PROTECC_BPF_SMOKE_OBJECTS})
            add_dependencies(protecc_bpf_verify_obj protecc_bpf_verify_obj_build)

            string(JOIN ";" PROTECC_BPF_SMOKE_OBJECTS_ARG ${PROTECC_BPF_SMOKE_OBJECTS})
        else()
            message(STATUS "protecc_bpf_verify will run in skip-only mode (BPF compile prerequisites unavailable)")
        endif()

        add_test(
            NAME protecc_bpf_verify
            COMMAND /bin/sh ${CMAKE_CURRENT_SOURCE_DIR}/tests/bpf/verify_protecc_bpf.sh ${PROTECC_BPF_SMOKE_OBJECTS_ARG}
        )
        set_tests_properties(protecc_bpf_verify PROPERTIES SKIP_RETURN_CODE 77)
    endif()
endif()
