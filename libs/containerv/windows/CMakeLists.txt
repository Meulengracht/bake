add_library(containerv-windows STATIC
    container-options.c
    container.c
    bpf-manager-stub.c
    hcs.c
    layers.c
    monitoring.c
    network.c
    resource-limits.c
    security.c
    volume-manager.c
)
target_include_directories(containerv-windows PRIVATE ../include)
target_link_libraries(containerv-windows PUBLIC platform vlog)
target_link_libraries(containerv-windows PRIVATE containerv-common)
# PID1 abstraction library (used by Windows backend for process lifecycle management)
target_link_libraries(containerv-windows PRIVATE containerv-pid1)
target_link_libraries(containerv-windows PRIVATE jansson)
target_link_libraries(containerv-windows PUBLIC advapi32 ole32)

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
    virtdisk       # Virtual Disk API for VHD management
)
