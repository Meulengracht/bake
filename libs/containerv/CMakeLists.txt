project (containerv C)

set(ADDITIONAL_LIBS)
if (UNIX)
    add_subdirectory(ebpf)
    add_subdirectory(linux)
    add_subdirectory(policies)
    
    set(ADDITIONAL_LIBS ${ADDITIONAL_LIBS} containerv-linux containerv-ebpf)
    
    # Propagate BPF variables from linux subdirectory to parent scope
    if(BPF_PROGRAMS_AVAILABLE)
        set(BPF_PROGRAMS_AVAILABLE TRUE PARENT_SCOPE)
        set(BPF_OUTPUT_DIR ${BPF_OUTPUT_DIR} PARENT_SCOPE)
    endif()
elseif(WIN32)
    add_subdirectory(windows)
    set(ADDITIONAL_LIBS ${ADDITIONAL_LIBS} containerv-windows)
endif()

add_library(containerv STATIC
    shared.c
)
target_link_libraries(containerv PUBLIC ${ADDITIONAL_LIBS})
target_include_directories(containerv PUBLIC include)
