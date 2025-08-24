cmake_minimum_required(VERSION 3.16)
project(SwitchMonitor LANGUAGES CXX)

# Use C++17 or newer
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find libssh
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBSSH REQUIRED libssh)

# Configure BSThreadPool
add_compile_definitions(BS_THREAD_POOL_ENABLE_PAUSE=1)
add_compile_definitions(BS_THREAD_POOL_ENABLE_PRIORITY=1)
add_compile_definitions(BS_THREAD_POOL_ENABLE_WAIT_DEADLOCK_CHECK=1)

## Find RoveComm
set(__ROVECOMM_LIBRARY_MODE__ 0 CACHE INTERNAL "Force RoveComm to build in standalone mode")
include_directories(external/RoveComm_CPP/src)
add_subdirectory(external/RoveComm_CPP)

# Add executable
add_executable(Switch_Monitor src/main.cpp)

# Include libssh headers
target_include_directories(Switch_Monitor PRIVATE ${LIBSSH_INCLUDE_DIRS})

# Link libssh
target_link_libraries(Switch_Monitor PRIVATE ${LIBSSH_LIBRARIES})

# Fix for some systems requiring rpath
target_link_options(Switch_Monitor PRIVATE ${LIBSSH_LDFLAGS_OTHER})
