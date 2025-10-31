cmake_minimum_required(VERSION 2.6)

project(datconf-lua)

IF(NOT LUA_CFLAGS)
	FIND_PROGRAM(PKG_CONFIG pkg-config)
	IF(PKG_CONFIG)
		EXECUTE_PROCESS(
			COMMAND pkg-config --silence-errors --cflags lua5.1
			OUTPUT_VARIABLE LUA_CFLAGS
			OUTPUT_STRIP_TRAILING_WHITESPACE
		)
	ENDIF()
ENDIF()

IF(NOT LUAPATH)
	EXECUTE_PROCESS(
		COMMAND  lua -e "for k in string.gmatch(package.cpath .. \";\", \"([^;]+)/..so;\") do if k:sub(1,1) == \"/\" then print(k) break end end"
		OUTPUT_VARIABLE LUAPATH
		RESULT_VARIABLE LUA_CHECK_RES
		OUTPUT_STRIP_TRAILING_WHITESPACE
	)

	IF(BUILD_LUA)
		IF(NOT ${LUA_CHECK_RES} EQUAL 0 OR "${LUAPATH}" EQUAL "")
			MESSAGE(SEND_ERROR "Lua was not found on your system")
		ENDIF()
	ENDIF()
ENDIF()

add_definitions(-Os -Wall -Werror -g3 ${LUA_CFLAGS})

include_directories(${kvcutil_SOURCE_DIR})
include_directories(${datconf_SOURCE_DIR})

IF(BUILD_LUA)
	add_library(datconf-lua SHARED datconf.c)
	target_link_libraries(datconf-lua datconf kvcutil)
	set_target_properties(datconf-lua PROPERTIES OUTPUT_NAME datconf PREFIX "")
	set_target_properties(datconf-lua PROPERTIES COMPILE_FLAGS "-ffunction-sections -fdata-sections")
	set_target_properties(datconf-lua PROPERTIES LINK_FLAGS "-Wl,--gc-sections")

	install(TARGETS datconf-lua
		LIBRARY DESTINATION ${LUAPATH}
	)
ENDIF()
