include_guard(GLOBAL)

include(CMakeParseArguments)

function(chef_bpf_init_cache_vars)
    set(CHEF_BPFFS_PATH "/sys/fs/bpf" CACHE PATH "Path to mounted bpffs used by eBPF runtime and tests")
    set(CHEF_BTF_FILE "/sys/kernel/btf/vmlinux" CACHE FILEPATH "Path to kernel BTF file used to generate vmlinux.h")
    set(CHEF_VMLINUX_HEADER_BASE_URL "https://raw.githubusercontent.com/libbpf/vmlinux.h/main/include" CACHE STRING "Base URL for fallback pre-generated vmlinux.h headers")
endfunction()

function(chef_bpf_check_bpffs out_available)
    if(EXISTS "${CHEF_BPFFS_PATH}")
        set(${out_available} TRUE PARENT_SCOPE)
    else()
        set(${out_available} FALSE PARENT_SCOPE)
    endif()
endfunction()

function(chef_bpf_probe_bpftool bpftool_path out_usable out_can_dump)
    set(_usable FALSE)
    set(_can_dump FALSE)

    if(bpftool_path)
        execute_process(
            COMMAND ${bpftool_path} version
            OUTPUT_QUIET
            ERROR_QUIET
            RESULT_VARIABLE _bpftool_version_rc
        )

        if(_bpftool_version_rc EQUAL 0)
            set(_usable TRUE)
        endif()
    endif()

    if(bpftool_path AND EXISTS "${CHEF_BTF_FILE}")
        execute_process(
            COMMAND ${bpftool_path} btf dump file ${CHEF_BTF_FILE} format c
            OUTPUT_QUIET
            ERROR_QUIET
            RESULT_VARIABLE _bpftool_check_rc
        )

        if(_bpftool_check_rc EQUAL 0)
            set(_can_dump TRUE)
        endif()
    endif()

    set(${out_usable} ${_usable} PARENT_SCOPE)
    set(${out_can_dump} ${_can_dump} PARENT_SCOPE)
endfunction()

function(chef_bpf_detect_target_arch out_arch)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|amd64|AMD64")
        set(_arch "x86")
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
        set(_arch "arm64")
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "arm")
        set(_arch "arm")
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "riscv64")
        set(_arch "riscv")
    else()
        set(_arch "x86")
    endif()

    set(${out_arch} "${_arch}" PARENT_SCOPE)
endfunction()

function(chef_bpf_vmlinux_arch_candidates out_candidates)
    string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" _host_arch)

    if(_host_arch MATCHES "x86_64|amd64")
        set(_candidates "x86_64;x86")
    elseif(_host_arch MATCHES "x86|i386|i686")
        set(_candidates "x86;x86_64")
    elseif(_host_arch MATCHES "aarch64")
        set(_candidates "aarch64;arm64;arm")
    elseif(_host_arch MATCHES "arm64")
        set(_candidates "arm64;aarch64;arm")
    elseif(_host_arch MATCHES "arm")
        set(_candidates "arm;arm64;aarch64")
    elseif(_host_arch MATCHES "riscv64")
        set(_candidates "riscv64;riscv")
    elseif(_host_arch MATCHES "riscv")
        set(_candidates "riscv;riscv64")
    else()
        set(_candidates "x86")
    endif()

    set(${out_candidates} "${_candidates}" PARENT_SCOPE)
endfunction()

function(chef_bpf_prepare_vmlinux_header bpftool_path can_dump_btf output_header out_ready)
    if(can_dump_btf)
        add_custom_command(
            OUTPUT ${output_header}
            COMMAND ${bpftool_path} btf dump file ${CHEF_BTF_FILE} format c > ${output_header}
            COMMENT "Generating vmlinux.h from kernel BTF: ${CHEF_BTF_FILE}"
            VERBATIM
        )

        set(${out_ready} TRUE PARENT_SCOPE)
        return()
    endif()

    chef_bpf_vmlinux_arch_candidates(_vmlinux_arch_candidates)

    set(_vmlinux_downloaded FALSE)
    foreach(_vmlinux_arch ${_vmlinux_arch_candidates})
        set(_vmlinux_url "${CHEF_VMLINUX_HEADER_BASE_URL}/${_vmlinux_arch}/vmlinux.h")
        file(
            DOWNLOAD
            "${_vmlinux_url}"
            "${output_header}"
            STATUS _vmlinux_download_status
            LOG _vmlinux_download_log
            TLS_VERIFY ON
        )
        list(GET _vmlinux_download_status 0 _vmlinux_download_rc)

        if(_vmlinux_download_rc EQUAL 0)
            file(READ "${output_header}" _vmlinux_header_probe LIMIT 256)
            string(STRIP "${_vmlinux_header_probe}" _vmlinux_header_probe)

            # Some libbpf vmlinux.h paths are indirections that only contain a versioned header filename.
            if(_vmlinux_header_probe MATCHES "^vmlinux_[A-Za-z0-9_.-]+\\.h$")
                set(_vmlinux_versioned_url "${CHEF_VMLINUX_HEADER_BASE_URL}/${_vmlinux_arch}/${_vmlinux_header_probe}")
                file(
                    DOWNLOAD
                    "${_vmlinux_versioned_url}"
                    "${output_header}"
                    STATUS _vmlinux_versioned_status
                    LOG _vmlinux_versioned_log
                    TLS_VERIFY ON
                )
                list(GET _vmlinux_versioned_status 0 _vmlinux_versioned_rc)

                if(_vmlinux_versioned_rc EQUAL 0)
                    message(STATUS "Using fallback vmlinux.h from ${_vmlinux_versioned_url}")
                    set(_vmlinux_downloaded TRUE)
                    break()
                endif()
            else()
                message(STATUS "Using fallback vmlinux.h from ${_vmlinux_url}")
                set(_vmlinux_downloaded TRUE)
                break()
            endif()

        endif()
    endforeach()

    if(NOT _vmlinux_downloaded)
        message(STATUS "Failed to fetch fallback vmlinux.h from ${CHEF_VMLINUX_HEADER_BASE_URL}")
    endif()

    set(${out_ready} ${_vmlinux_downloaded} PARENT_SCOPE)
endfunction()

function(chef_bpf_add_object_commands)
    set(_options)
    set(_one_value_args CLANG TARGET_ARCH OUTPUT_DIR VMLINUX_HEADER OUT_OBJECTS)
    set(_multi_value_args SOURCES INCLUDE_DIRS EXTRA_FLAGS)
    cmake_parse_arguments(CHEF_BPF_OBJ "${_options}" "${_one_value_args}" "${_multi_value_args}" ${ARGN})

    if(NOT CHEF_BPF_OBJ_CLANG OR NOT CHEF_BPF_OBJ_TARGET_ARCH OR NOT CHEF_BPF_OBJ_OUTPUT_DIR OR NOT CHEF_BPF_OBJ_VMLINUX_HEADER OR NOT CHEF_BPF_OBJ_OUT_OBJECTS)
        message(FATAL_ERROR "chef_bpf_add_object_commands requires CLANG, TARGET_ARCH, OUTPUT_DIR, VMLINUX_HEADER, and OUT_OBJECTS")
    endif()

    set(_objects "")
    foreach(_source ${CHEF_BPF_OBJ_SOURCES})
        get_filename_component(_name ${_source} NAME_WE)
        set(_object ${CHEF_BPF_OBJ_OUTPUT_DIR}/${_name}.bpf.o)

        set(_include_flags "")
        foreach(_include_dir ${CHEF_BPF_OBJ_INCLUDE_DIRS})
            list(APPEND _include_flags -I${_include_dir})
        endforeach()

        add_custom_command(
            OUTPUT ${_object}
            COMMAND ${CHEF_BPF_OBJ_CLANG} -g -O2 -target bpf
                -D__TARGET_ARCH_${CHEF_BPF_OBJ_TARGET_ARCH}
                ${_include_flags}
                ${CHEF_BPF_OBJ_EXTRA_FLAGS}
                -c ${_source}
                -o ${_object}
            DEPENDS ${_source} ${CHEF_BPF_OBJ_VMLINUX_HEADER}
            COMMENT "Building BPF object: ${_object}"
            VERBATIM
        )

        list(APPEND _objects ${_object})
    endforeach()

    set(${CHEF_BPF_OBJ_OUT_OBJECTS} ${_objects} PARENT_SCOPE)
endfunction()

function(chef_bpf_add_skeleton_commands)
    set(_options)
    set(_one_value_args BPFTOOL OUTPUT_DIR OUT_SKELETONS)
    set(_multi_value_args OBJECTS)
    cmake_parse_arguments(CHEF_BPF_SKEL "${_options}" "${_one_value_args}" "${_multi_value_args}" ${ARGN})

    if(NOT CHEF_BPF_SKEL_BPFTOOL OR NOT CHEF_BPF_SKEL_OUTPUT_DIR OR NOT CHEF_BPF_SKEL_OUT_SKELETONS)
        message(FATAL_ERROR "chef_bpf_add_skeleton_commands requires BPFTOOL, OUTPUT_DIR, and OUT_SKELETONS")
    endif()

    set(_skeletons "")
    foreach(_object ${CHEF_BPF_SKEL_OBJECTS})
        get_filename_component(_name ${_object} NAME_WE)
        set(_skeleton ${CHEF_BPF_SKEL_OUTPUT_DIR}/${_name}.skel.h)

        add_custom_command(
            OUTPUT ${_skeleton}
            COMMAND ${CHEF_BPF_SKEL_BPFTOOL} gen skeleton ${_object} > ${_skeleton}
            DEPENDS ${_object}
            COMMENT "Generating BPF skeleton: ${_skeleton}"
            VERBATIM
        )

        list(APPEND _skeletons ${_skeleton})
    endforeach()

    set(${CHEF_BPF_SKEL_OUT_SKELETONS} ${_skeletons} PARENT_SCOPE)
endfunction()

function(chef_bpf_add_program_commands)
    set(_options)
    set(_one_value_args CLANG BPFTOOL TARGET_ARCH OUTPUT_DIR VMLINUX_HEADER OUT_OBJECTS OUT_SKELETONS)
    set(_multi_value_args SOURCES INCLUDE_DIRS EXTRA_FLAGS)
    cmake_parse_arguments(CHEF_BPF_PROG "${_options}" "${_one_value_args}" "${_multi_value_args}" ${ARGN})

    if(NOT CHEF_BPF_PROG_CLANG OR NOT CHEF_BPF_PROG_BPFTOOL OR NOT CHEF_BPF_PROG_TARGET_ARCH OR NOT CHEF_BPF_PROG_OUTPUT_DIR OR NOT CHEF_BPF_PROG_VMLINUX_HEADER OR NOT CHEF_BPF_PROG_OUT_OBJECTS OR NOT CHEF_BPF_PROG_OUT_SKELETONS)
        message(FATAL_ERROR "chef_bpf_add_program_commands requires CLANG, BPFTOOL, TARGET_ARCH, OUTPUT_DIR, VMLINUX_HEADER, OUT_OBJECTS, and OUT_SKELETONS")
    endif()

    chef_bpf_add_object_commands(
        CLANG "${CHEF_BPF_PROG_CLANG}"
        TARGET_ARCH "${CHEF_BPF_PROG_TARGET_ARCH}"
        OUTPUT_DIR "${CHEF_BPF_PROG_OUTPUT_DIR}"
        VMLINUX_HEADER "${CHEF_BPF_PROG_VMLINUX_HEADER}"
        SOURCES ${CHEF_BPF_PROG_SOURCES}
        INCLUDE_DIRS ${CHEF_BPF_PROG_INCLUDE_DIRS}
        EXTRA_FLAGS ${CHEF_BPF_PROG_EXTRA_FLAGS}
        OUT_OBJECTS _chef_bpf_program_objects
    )

    chef_bpf_add_skeleton_commands(
        BPFTOOL "${CHEF_BPF_PROG_BPFTOOL}"
        OUTPUT_DIR "${CHEF_BPF_PROG_OUTPUT_DIR}"
        OBJECTS ${_chef_bpf_program_objects}
        OUT_SKELETONS _chef_bpf_program_skeletons
    )

    set(${CHEF_BPF_PROG_OUT_OBJECTS} ${_chef_bpf_program_objects} PARENT_SCOPE)
    set(${CHEF_BPF_PROG_OUT_SKELETONS} ${_chef_bpf_program_skeletons} PARENT_SCOPE)
endfunction()
