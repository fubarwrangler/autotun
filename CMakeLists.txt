CMAKE_MINIMUM_REQUIRED(VERSION 2.8)
PROJECT(autotun)

INCLUDE_DIRECTORIES(include)
INCLUDE_DIRECTORIES(iniread/include)

FIND_PATH(
	LIBSSH_INCLUDE_DIR libssh/libssh.h
	PATHS /usr/local/ /usr/
)
FIND_LIBRARY(
	LIBSSH_LIBRARY NAMES libssh ssh
	PATHS /usr/local/ /usr/
)

SET(LIBSSH_LIBRARIES ${LIBSSH_LIBRARY})
SET(LIBSSH_INCLUDE_DIRS ${LIBSSH_INCLUDE_DIR})

MARK_AS_ADVANCED(LIBSSH_LIBRARIES LIBSSH_INCLUDE_DIRS)

ADD_SUBDIRECTORY(src)

SET(WITH_STATIC True)

IF( NOT EXISTS "${CMAKE_SOURCE_DIR}/iniread/.git")
    MESSAGE( WARNING "Not found, updating submodules")
    EXECUTE_PROCESS(COMMAND git submodule update --init
                    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
                   )
ENDIF()

ADD_CUSTOM_COMMAND( TARGET  iniread
                    PRE_BUILD
                    COMMAND git submodule init
                    COMMAND git submodule update
                  )

ADD_SUBDIRECTORY(iniread)
