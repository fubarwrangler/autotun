cmake_minimum_required(VERSION 2.8)
project(autotun)

add_subdirectory(src)

find_path(LIBSSH_INCLUDE_DIR libssh/libssh.h)
find_library(LIBSSH_LIBRARY NAMES libssh ssh)

set(LIBSSH_LIBRARIES ${LIBSSH_LIBRARY})
set(LIBSSH_INCLUDE_DIRS ${LIBSSH_INCLUDE_DIR})

mark_as_advanced(LIBSSH_LIBRARIES LIBSSH_INCLUDE_DIRS)
