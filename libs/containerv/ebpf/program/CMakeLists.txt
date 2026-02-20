# Build BPF programs for LSM filesystem enforcement
#
# This CMake file handles compilation of BPF programs using clang and bpftool.
# The BPF programs are compiled to BPF bytecode and then skeletons are generated
# for easy integration with the C code.

find_program(CLANG clang)
find_program(BPFTOOL bpftool)
find_program(LLVM_STRIP llvm-strip)

if(CLANG AND BPFTOOL AND EXISTS /sys/kernel/btf/vmlinux)
    message(STATUS "BPF tools found: clang=${CLANG}, bpftool=${BPFTOOL}")
    
    set(BPF_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR})
    set(BPF_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/bpf)
    
    # Create output directory
    file(MAKE_DIRECTORY ${BPF_OUTPUT_DIR})
    
    # Detect target architecture for BPF
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|amd64|AMD64")
        set(BPF_ARCH "x86")
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
        set(BPF_ARCH "arm64")
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "arm")
        set(BPF_ARCH "arm")
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "riscv64")
        set(BPF_ARCH "riscv")
    else()
        message(WARNING "Unknown architecture ${CMAKE_SYSTEM_PROCESSOR}, defaulting to x86 for BPF")
        set(BPF_ARCH "x86")
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
    add_custom_command(
        OUTPUT ${VMLINUX_HEADER}
        COMMAND ${BPFTOOL} btf dump file /sys/kernel/btf/vmlinux format c > ${VMLINUX_HEADER}
        COMMENT "Generating vmlinux.h for BPF: ${VMLINUX_HEADER}"
        VERBATIM
    )
    
    set(BPF_OBJECTS "")
    set(BPF_SKELETONS "")

    foreach(BPF_SOURCE ${BPF_SOURCES})
        get_filename_component(BPF_NAME ${BPF_SOURCE} NAME_WE)
        set(BPF_OBJECT ${BPF_OUTPUT_DIR}/${BPF_NAME}.bpf.o)
        set(BPF_SKEL ${BPF_OUTPUT_DIR}/${BPF_NAME}.skel.h)

        list(APPEND BPF_OBJECTS ${BPF_OBJECT})
        list(APPEND BPF_SKELETONS ${BPF_SKEL})

        add_custom_command(
            OUTPUT ${BPF_OBJECT}
            COMMAND ${CLANG} -g -O2 -target bpf
                -D__TARGET_ARCH_${BPF_ARCH}
                -I${BPF_OUTPUT_DIR}
                -I${CMAKE_SOURCE_DIR}/libs/protecc/include
                -I/usr/include
                -I/usr/include/${CMAKE_LIBRARY_ARCHITECTURE}
                -c ${BPF_SOURCE}
                -o ${BPF_OBJECT}
            DEPENDS ${BPF_SOURCE} ${VMLINUX_HEADER}
            COMMENT "Building BPF object: ${BPF_OBJECT}"
            VERBATIM
        )

        add_custom_command(
            OUTPUT ${BPF_SKEL}
            COMMAND ${BPFTOOL} gen skeleton ${BPF_OBJECT} > ${BPF_SKEL}
            DEPENDS ${BPF_OBJECT}
            COMMENT "Generating BPF skeleton: ${BPF_SKEL}"
            VERBATIM
        )
    endforeach()

    # Create custom target for BPF programs
    add_custom_target(bpf_programs ALL
        DEPENDS ${BPF_SKELETONS}
    )
    
    # Set property to indicate BPF programs are available
    set(BPF_PROGRAMS_AVAILABLE TRUE PARENT_SCOPE)
    set(BPF_SKEL_HEADERS ${BPF_SKELETONS} PARENT_SCOPE)
    set(BPF_OUTPUT_DIR ${BPF_OUTPUT_DIR} PARENT_SCOPE)
    
else()
    if(NOT (CLANG AND BPFTOOL))
        message(STATUS "BPF tools not found - BPF LSM programs will not be built (clang and bpftool required)")
    else()
        message(STATUS "Kernel BTF not found at /sys/kernel/btf/vmlinux - BPF LSM programs will not be built")
    endif()
    set(BPF_PROGRAMS_AVAILABLE FALSE PARENT_SCOPE)
endif()
