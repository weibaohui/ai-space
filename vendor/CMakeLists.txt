# Build only the client libraries. Keep them replaceable in release packages.
set(BUILD_SHARED_LIBS ON CACHE BOOL "Build the vendored clients as shared libraries" FORCE)
set(ENABLE_TESTS OFF CACHE BOOL "" FORCE)
set(ENABLE_EXAMPLES OFF CACHE BOOL "" FORCE)
set(ENABLE_DOCUMENTATION OFF CACHE BOOL "" FORCE)
set(ENABLE_UTILS OFF CACHE BOOL "" FORCE)
set(ENABLE_MULTITHREADING OFF CACHE BOOL "" FORCE)
set(ENABLE_NFS4_2 OFF CACHE BOOL "" FORCE)
set(ENABLE_TLS OFF CACHE STRING "" FORCE)
set(ENABLE_KRB5 OFF CACHE STRING "" FORCE)
set(ENABLE_RDMA OFF CACHE BOOL "" FORCE)
set(ENABLE_LIBKRB5 OFF CACHE BOOL "" FORCE)
set(ENABLE_GSSAPI OFF CACHE BOOL "" FORCE)
set(ENABLE_LIBDCERPC OFF CACHE BOOL "" FORCE)
set(ENABLE_LIBQUIC OFF CACHE BOOL "" FORCE)
set(ENABLE_OPENSSL OFF CACHE BOOL "" FORCE)
# Linux releases target glibc 2.17. Use the library's /dev/urandom backend.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(HAVE_ARC4RANDOM_BUF OFF CACHE INTERNAL "" FORCE)
    set(HAVE_GETRANDOM OFF CACHE INTERNAL "" FORCE)
endif()
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
# windres is not a C compiler. Under GCC libnfs adds -Wall at directory level
# and CMake forwards it inside <FLAGS> to the version.rc rule, which GNU
# windres rejects ("invalid option -- W"). A resource only needs defines and
# include paths, so drop the C flags from the rule for the vendored trees.
if(MINGW)
    set(CMAKE_RC_COMPILE_OBJECT "<CMAKE_RC_COMPILER> -O coff <DEFINES> <INCLUDES> <SOURCE> <OBJECT>")
endif()
add_subdirectory(libnfs EXCLUDE_FROM_ALL)
# libnfs uses a cache variable with the same name as libsmb2.
set(CORE_LIBRARIES "")
add_subdirectory(libsmb2 EXCLUDE_FROM_ALL)
target_include_directories(nfs INTERFACE "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/libnfs/include>")
