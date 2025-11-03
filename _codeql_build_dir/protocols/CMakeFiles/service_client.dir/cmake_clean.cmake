file(REMOVE_RECURSE
  "CMakeFiles/service_client"
  "chef_cvd_service.h"
  "chef_cvd_service_client.c"
  "chef_cvd_service_client.h"
  "chef_cvd_service_server.c"
  "chef_cvd_service_server.h"
  "chef_served_service.h"
  "chef_served_service_client.c"
  "chef_served_service_client.h"
  "chef_served_service_server.c"
  "chef_served_service_server.h"
  "chef_waiterd_service.h"
  "chef_waiterd_service_client.c"
  "chef_waiterd_service_client.h"
  "chef_waiterd_service_server.c"
  "chef_waiterd_service_server.h"
)

# Per-language clean rules from dependency scanning.
foreach(lang )
  include(CMakeFiles/service_client.dir/cmake_clean_${lang}.cmake OPTIONAL)
endforeach()
