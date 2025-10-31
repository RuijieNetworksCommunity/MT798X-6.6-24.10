cmake_minimum_required(VERSION 2.6)

project(datconf)

OPTION(BUILD_LUA "build Lua binding" ON)
SET(L1_PROFILE_PATH "/etc/wireless/l1profile.dat" CACHE STRING "l1profile path")

add_subdirectory(lua)

add_definitions(-Os -Wall -Werror -g3)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../kvcutil)

add_library(datconf libdatconf.c)
set_target_properties(datconf PROPERTIES COMPILE_FLAGS "-fPIC -ffunction-sections -fdata-sections -DL1PROFILE_PATH=\"\\\"${L1_PROFILE_PATH}\\\"\"")

add_executable(dconf datconf.c)
target_link_libraries(dconf datconf kvcutil)
set_target_properties(dconf PROPERTIES OUTPUT_NAME datconf)
set_target_properties(dconf PROPERTIES COMPILE_FLAGS "-fPIC -ffunction-sections -fdata-sections")
set_target_properties(dconf PROPERTIES LINK_FLAGS "-Wl,--gc-sections")

install(FILES libdatconf.h
	DESTINATION include
)

install(TARGETS datconf dconf
	ARCHIVE DESTINATION lib
	LIBRARY DESTINATION lib
	RUNTIME DESTINATION bin
)

