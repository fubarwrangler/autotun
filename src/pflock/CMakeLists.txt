ADD_DEFINITIONS(-std=c99 -D_XOPEN_SOURCE=700)

add_executable(pflock pflock.c ../util.c)
