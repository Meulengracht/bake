project (containerv C)

find_path(LIBBPF_INCLUDE_DIR NAMES bpf/libbpf.h)
find_library(LIBBPF_LIBRARY NAMES bpf libbpf)

# Add PID1 service subdirectory
add_subdirectory(pid1)

# Internal shared helpers (not part of public API)
add_subdirectory(common)

# Guest-side PID1 daemon (used inside VMs/guests)
add_subdirectory(pid1d)

# Add rootfs and disk utilities
add_subdirectory(disk)

set(CV_SRCS shared.c)
set(ADDITIONAL_LIBS containerv-disk containerv-pid1)
if(LIBBPF_INCLUDE_DIR AND LIBBPF_LIBRARY)
    add_subdirectory(ebpf)
    set(ADDITIONAL_LIBS ${ADDITIONAL_LIBS} containerv-ebpf)
else()
    message(WARNING "libbpf not found. BPF-based features will be disabled.")
    set(CV_SRCS ${CV_SRCS} bpf-stub.c)
endif()

if (UNIX)
    add_subdirectory(linux)
    add_subdirectory(policies)
    
    set(ADDITIONAL_LIBS ${ADDITIONAL_LIBS} containerv-linux containerv-policy)
    
    # Propagate BPF variables from linux subdirectory to parent scope
    if(BPF_PROGRAMS_AVAILABLE)
        set(BPF_PROGRAMS_AVAILABLE TRUE PARENT_SCOPE)
        set(BPF_OUTPUT_DIR ${BPF_OUTPUT_DIR} PARENT_SCOPE)
    endif()
elseif(WIN32)
    add_subdirectory(windows)
    add_subdirectory(policies)
    set(ADDITIONAL_LIBS ${ADDITIONAL_LIBS} containerv-windows containerv-policy)
endif()

add_library(containerv STATIC ${CV_SRCS})
target_link_libraries(containerv PUBLIC ${ADDITIONAL_LIBS})
target_include_directories(containerv PUBLIC include)
