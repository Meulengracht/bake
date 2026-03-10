# Build BPF programs for LSM filesystem enforcement
#
# This CMake file handles compilation of BPF programs using clang and bpftool.
# The BPF programs are compiled to BPF bytecode and then skeletons are generated
# for easy integration with the C code.

include(ChefBpf)

chef_bpf_init_cache_vars()

find_program(CLANG clang)
find_program(BPFTOOL bpftool)
find_program(LLVM_STRIP llvm-strip)

chef_bpf_probe_bpftool("${BPFTOOL}" BPFTOOL_USABLE BPFTOOL_CAN_DUMP_BTF)

if(CLANG AND BPFTOOL_USABLE)
    message(STATUS "BPF tools found: clang=${CLANG}, bpftool=${BPFTOOL}")
    chef_bpf_check_bpffs(BPF_BPFFS_AVAILABLE)
    if(BPF_BPFFS_AVAILABLE)
        message(STATUS "bpffs detected at ${CHEF_BPFFS_PATH} (supports LXD security.delegate_bpf setups)")
    else()
        message(STATUS "bpffs not detected at ${CHEF_BPFFS_PATH}; build will continue, but runtime program pinning may fail")
    endif()
    
    set(BPF_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR})
    set(BPF_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/bpf)
    
    # Create output directory
    file(MAKE_DIRECTORY ${BPF_OUTPUT_DIR})
    
    # Detect target architecture for BPF
    chef_bpf_detect_target_arch(BPF_ARCH)
    if(BPF_ARCH STREQUAL "x86" AND NOT CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|amd64|AMD64|x86|i386|i686")
        message(WARNING "Unknown architecture ${CMAKE_SYSTEM_PROCESSOR}, defaulting to x86 for BPF")
    endif()
    
    message(STATUS "BPF target architecture: ${BPF_ARCH}")
    
    # BPF source files
    set(BPF_SOURCES
        ${BPF_SOURCE_DIR}/fs-lsm.bpf.c
        ${BPF_SOURCE_DIR}/net-lsm.bpf.c
        ${BPF_SOURCE_DIR}/mount-lsm.bpf.c
    )
    
    # Generate the vmlinux.h header for BPF
    set(VMLINUX_HEADER ${BPF_OUTPUT_DIR}/vmlinux.h)
    chef_bpf_prepare_vmlinux_header("${BPFTOOL}" ${BPFTOOL_CAN_DUMP_BTF} ${VMLINUX_HEADER} BPF_VMLINUX_READY)
    if(NOT BPF_VMLINUX_READY)
        message(STATUS "BPF LSM programs will not be built because vmlinux.h generation failed")
        set(BPF_PROGRAMS_AVAILABLE FALSE PARENT_SCOPE)
        return()
    endif()
    
    set(BPF_INCLUDE_DIRS
        ${BPF_OUTPUT_DIR}
        ${CMAKE_SOURCE_DIR}/libs/protecc/include
        /usr/include
        /usr/include/${CMAKE_LIBRARY_ARCHITECTURE}
    )

    chef_bpf_add_program_commands(
        CLANG "${CLANG}"
        BPFTOOL "${BPFTOOL}"
        TARGET_ARCH "${BPF_ARCH}"
        OUTPUT_DIR "${BPF_OUTPUT_DIR}"
        VMLINUX_HEADER "${VMLINUX_HEADER}"
        SOURCES ${BPF_SOURCES}
        INCLUDE_DIRS ${BPF_INCLUDE_DIRS}
        OUT_OBJECTS BPF_OBJECTS
        OUT_SKELETONS BPF_SKELETONS
    )

    # Create custom target for BPF programs
    add_custom_target(bpf_programs ALL
        DEPENDS ${BPF_SKELETONS}
    )
    
    # Set property to indicate BPF programs are available
    set(BPF_PROGRAMS_AVAILABLE TRUE PARENT_SCOPE)
    set(BPF_SKEL_HEADERS ${BPF_SKELETONS} PARENT_SCOPE)
    set(BPF_OUTPUT_DIR ${BPF_OUTPUT_DIR} PARENT_SCOPE)
    
else()
    if(NOT CLANG)
        message(STATUS "clang not found - BPF LSM programs will not be built")
    elseif(NOT BPFTOOL_USABLE)
        message(STATUS "bpftool not found or unusable - BPF LSM programs will not be built (bpftool with working 'version' and 'gen skeleton' support is required)")
    else()
        message(STATUS "BPF prerequisites unavailable - BPF LSM programs will not be built")
    endif()
    set(BPF_PROGRAMS_AVAILABLE FALSE PARENT_SCOPE)
endif()
