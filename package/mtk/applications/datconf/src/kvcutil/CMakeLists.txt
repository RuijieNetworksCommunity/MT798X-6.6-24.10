cmake_minimum_required(VERSION 2.6)

project(kvcutil)

add_definitions(-Os -Wall -Werror -g3)

add_library(kvcutil SHARED libkvcutil.c)
set_target_properties(kvcutil PROPERTIES COMPILE_FLAGS "-O2")

SET_TARGET_PROPERTIES(kvcutil PROPERTIES VERSION ${DATCONF_VERSION_MAJOR}.${DATCONF_VERSION_MINOR}.${DATCONF_VERSION_PATCH}.0 SOVERSION ${DATCONF_VERSION_MAJOR})

add_executable(kvcedit kvcedit.c)
target_link_libraries(kvcedit kvcutil)

install(FILES libkvcutil.h
	DESTINATION include
)

install(TARGETS kvcutil kvcedit
	ARCHIVE DESTINATION lib
	LIBRARY DESTINATION lib
	RUNTIME DESTINATION bin
)

