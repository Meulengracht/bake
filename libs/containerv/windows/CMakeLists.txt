add_library(containerv-windows STATIC
    rootfs/debootstrap.c
    container-options.c
    container.c
    hcs.c
    monitoring.c
    network.c
    resource-limits.c
    rootfs.c
    volume-manager.c
)
target_include_directories(containerv-windows PRIVATE ../include)
target_link_libraries(containerv-windows PUBLIC platform vlog)
target_link_libraries(containerv-windows PUBLIC advapi32 ole32)

# Link Windows HCS/HyperV libraries
if(WIN32)
    target_link_libraries(containerv-windows PUBLIC 
        vmcompute      # Host Compute Service
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
endif()
