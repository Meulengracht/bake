# Build BPF programs for LSM filesystem enforcement
#
# This CMake file handles compilation of BPF programs using clang and bpftool.
# The BPF programs are compiled to BPF bytecode and then skeletons are generated
# for easy integration with the C code.

find_program(CLANG clang)
find_program(BPFTOOL bpftool)
find_program(LLVM_STRIP llvm-strip)

if(CLANG AND BPFTOOL)
    message(STATUS "BPF tools found: clang=${CLANG}, bpftool=${BPFTOOL}")
    
    set(BPF_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR})
    set(BPF_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/bpf)
    
    # Create output directory
    file(MAKE_DIRECTORY ${BPF_OUTPUT_DIR})
    
    # BPF source file
    set(BPF_SOURCE ${BPF_SOURCE_DIR}/fs_lsm.bpf.c)
    set(BPF_OBJECT ${BPF_OUTPUT_DIR}/fs_lsm.bpf.o)
    set(BPF_SKEL ${BPF_OUTPUT_DIR}/fs_lsm.skel.h)
    
    # Compile BPF program to object file
    add_custom_command(
        OUTPUT ${BPF_OBJECT}
        COMMAND ${CLANG} -g -O2 -target bpf
            -D__TARGET_ARCH_x86
            -I/usr/include
            -I/usr/include/x86_64-linux-gnu
            -c ${BPF_SOURCE}
            -o ${BPF_OBJECT}
        DEPENDS ${BPF_SOURCE}
        COMMENT "Building BPF object: ${BPF_OBJECT}"
        VERBATIM
    )
    
    # Generate BPF skeleton header
    add_custom_command(
        OUTPUT ${BPF_SKEL}
        COMMAND ${BPFTOOL} gen skeleton ${BPF_OBJECT} > ${BPF_SKEL}
        DEPENDS ${BPF_OBJECT}
        COMMENT "Generating BPF skeleton: ${BPF_SKEL}"
        VERBATIM
    )
    
    # Create custom target for BPF programs
    add_custom_target(bpf_programs ALL
        DEPENDS ${BPF_SKEL}
    )
    
    # Set property to indicate BPF programs are available
    set(BPF_PROGRAMS_AVAILABLE TRUE PARENT_SCOPE)
    set(BPF_SKEL_HEADER ${BPF_SKEL} PARENT_SCOPE)
    set(BPF_OUTPUT_DIR ${BPF_OUTPUT_DIR} PARENT_SCOPE)
    
else()
    message(STATUS "BPF tools not found - BPF LSM programs will not be built (clang and bpftool required)")
    set(BPF_PROGRAMS_AVAILABLE FALSE PARENT_SCOPE)
endif()
