add_library(containerv-windows STATIC
    container-options.c
    container.c
    gcs-bridge.c
    gcs-process.c
    hcs.c
    layerchain.c
    lcow-storage.c
    layers.c
    monitoring.c
    network.c
    resource-limits.c
    security.c
    utils.c
    volume-manager.c
)
target_include_directories(containerv-windows PRIVATE ../include)
foreach(script storage network)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "lcow-${script}.sh")
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/lcow-${script}.sh" script_text)
    string(REPLACE "\\" "\\\\" script_text "${script_text}")
    string(REPLACE "\"" "\\\"" script_text "${script_text}")
    string(REPLACE "\r" "" script_text "${script_text}")
    string(REPLACE "\n" "\\n\"\n\"" script_text "${script_text}")
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/lcow-${script}-script.h"
        "static const char containerv_lcow_${script}_script[] = \"${script_text}\";\n")
endforeach()
target_include_directories(containerv-windows PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(containerv-windows PUBLIC platform vlog)
target_link_libraries(containerv-windows PRIVATE containerv-common containerv-disk)
target_link_libraries(containerv-windows PRIVATE jansson)
target_link_libraries(containerv-windows PUBLIC advapi32 ole32 virtdisk)

# Link Windows HCS/HyperV libraries
target_link_libraries(containerv-windows PUBLIC 
    onecore        # Core Windows APIs 
    kernel32       # Windows kernel APIs
    ws2_32         # Winsock2 for networking
    iphlpapi       # IP Helper API for network management
    wininet        # WinInet for HTTP downloads
    shlwapi        # Shell lightweight utility APIs
    shell32        # Windows Shell API
    psapi          # Process Status API for memory info
    pdh            # Performance Data Helper for monitoring
)
