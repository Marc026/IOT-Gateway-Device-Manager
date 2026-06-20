cmake_minimum_required(VERSION 3.16)
project(iot_gateway_devicemgr C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

option(ENABLE_SANITIZERS "Build with AddressSanitizer + UndefinedBehaviorSanitizer" OFF)

add_compile_options(-Wall -Wextra -Werror -Wshadow)

if(ENABLE_SANITIZERS)
  add_compile_options(-fsanitize=address,undefined -g -O0)
  add_link_options(-fsanitize=address,undefined)
endif()

include_directories(include)

add_library(gateway_core
  src/state_machine.c
  src/nvs_store.c
  src/watchdog.c
  src/device_registry.c
  src/gateway_protocol.c
)

add_executable(gateway_main src/main.c)
target_link_libraries(gateway_main gateway_core)

enable_testing()
add_subdirectory(tests)
