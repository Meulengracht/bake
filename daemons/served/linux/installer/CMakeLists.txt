add_library(served-installer STATIC
    install.c
    uninstall.c
    verify.c
)
target_include_directories(served-installer PRIVATE ../include ${CMAKE_BINARY_DIR}/protocols)
target_link_libraries(served-installer PUBLIC fridge common platform gracht vlog OpenSSL::SSL)
