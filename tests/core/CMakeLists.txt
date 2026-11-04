# Standalone build of the engine-free core, for testing outside Unreal.
#   cmake -S tests/core -B build/core -DSANITIZE=thread && cmake --build build/core && ctest --test-dir build/core
cmake_minimum_required(VERSION 3.16)
project(LiveMapCoreTests C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(MOD ${CMAKE_CURRENT_SOURCE_DIR}/../../Mod/LiveMap/Source)
set(CORE ${MOD}/LiveMap/Private/Core)

set(SANITIZE "" CACHE STRING "address, thread, or empty")

add_library(livemap_core STATIC
  ${CORE}/LiveMapCore.cpp
  ${CORE}/LiveMapServer.cpp
  ${CORE}/WebAssets.cpp
  ${CORE}/Civetweb.c)
target_include_directories(livemap_core PUBLIC ${CORE} ${MOD}/ThirdParty/civetweb/include)
# Match Unreal: no exceptions, no RTTI in our code.
target_compile_options(livemap_core PRIVATE
  $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions -fno-rtti -Wall -Wextra -Werror>)
find_package(Threads REQUIRED)
target_link_libraries(livemap_core PUBLIC Threads::Threads)
if(WIN32)
  target_link_libraries(livemap_core PUBLIC ws2_32)
else()
  target_link_libraries(livemap_core PUBLIC dl)
endif()

if(SANITIZE)
  add_compile_options(-fsanitize=${SANITIZE} -g -O1)
  add_link_options(-fsanitize=${SANITIZE})
  target_compile_options(livemap_core PRIVATE -fsanitize=${SANITIZE} -g -O1)
  target_link_options(livemap_core PUBLIC -fsanitize=${SANITIZE})
endif()

add_executable(test_core test_core.cpp)
target_link_libraries(test_core livemap_core)
add_executable(test_server test_server.cpp)
target_link_libraries(test_server livemap_core)
add_executable(livemap_sim sim.cpp)
target_link_libraries(livemap_sim livemap_core)

enable_testing()
add_test(NAME core COMMAND test_core)
add_test(NAME server COMMAND test_server)
