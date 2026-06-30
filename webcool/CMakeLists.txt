cmake_minimum_required(VERSION 3.16)

project(webcool LANGUAGES CXX)

option(BUILD_ACL "Build third-party ACL libraries before building webcool" OFF)
option(HAS_IO_URING "Link liburing-ffi on Linux" OFF)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(ACL_PATH "${CMAKE_CURRENT_LIST_DIR}/../third-party/acl" CACHE PATH "ACL source/library root")

file(GLOB WEBCOOL_SOURCES CONFIGURE_DEPENDS
	"${CMAKE_CURRENT_LIST_DIR}/*.cpp"
	"${CMAKE_CURRENT_LIST_DIR}/common/*.cpp"
	"${CMAKE_CURRENT_LIST_DIR}/win32/*.cpp"
	"${CMAKE_CURRENT_LIST_DIR}/template/*.cpp"
	"${CMAKE_CURRENT_LIST_DIR}/action/*.cpp"
	"${CMAKE_CURRENT_LIST_DIR}/action/admin/*.cpp"
	"${CMAKE_CURRENT_LIST_DIR}/action/convert/*.cpp"
	"${CMAKE_CURRENT_LIST_DIR}/action/file/*.cpp"
	"${CMAKE_CURRENT_LIST_DIR}/action/folder/*.cpp"
	"${CMAKE_CURRENT_LIST_DIR}/action/tag/*.cpp"
	"${CMAKE_CURRENT_LIST_DIR}/action/localdisk/*.cpp")

add_executable(webcool ${WEBCOOL_SOURCES})

target_include_directories(webcool PRIVATE
	"${CMAKE_CURRENT_LIST_DIR}"
	"${ACL_PATH}/lib_acl/include"
	"${ACL_PATH}/lib_acl_cpp/include"
	"${ACL_PATH}/lib_protocol/include"
	"${ACL_PATH}/lib_fiber/c/include"
	"${ACL_PATH}/lib_fiber/cpp/include")

target_compile_definitions(webcool PRIVATE
	_REENTRANT
	_POSIX_PTHREAD_SEMANTICS
	_USE_FAST_MACRO
	ACL_USE_CPP11)

if(APPLE)
	target_compile_definitions(webcool PRIVATE MACOSX)
	target_compile_options(webcool PRIVATE
		$<$<CXX_COMPILER_ID:AppleClang,Clang>:-Wno-invalid-source-encoding -Wno-invalid-offsetof>)
elseif(WIN32)
	target_compile_definitions(webcool PRIVATE
		WIN32
		ACL_WINDOWS
		_CRT_SECURE_NO_WARNINGS
		_WINSOCK_DEPRECATED_NO_WARNINGS
		WIN32_LEAN_AND_MEAN
		NOMINMAX)
elseif(CMAKE_SYSTEM_NAME STREQUAL "FreeBSD")
	target_compile_definitions(webcool PRIVATE FREEBSD _REENTRANT)
endif()

if(MSVC)
	target_compile_options(webcool PRIVATE /W3 /MP /utf-8 /Zc:__cplusplus)
else()
	target_compile_options(webcool PRIVATE
		-g -W -Wall -Wcast-qual -Wcast-align -Wno-long-long
		-Wpointer-arith -Wshadow -O2)
endif()

set(ACL_LIBRARY_DIRS
	"${ACL_PATH}/lib_fiber/lib"
	"${ACL_PATH}/lib_acl_cpp/lib"
	"${ACL_PATH}/lib_protocol/lib"
	"${ACL_PATH}/lib_acl/lib")

if(WIN32)
	list(APPEND ACL_LIBRARY_DIRS "${CMAKE_CURRENT_LIST_DIR}/x64/Release")
	set(ACL_PREBUILT_LIBRARIES
		libfiber_cpp
		lib_acl_cpp
		lib_protocol
		lib_acl
		libfiber)
else()
	set(ACL_PREBUILT_LIBRARIES
		fiber_cpp
		acl_cpp
		protocol
		acl
		fiber)
endif()

if(BUILD_ACL)
	add_subdirectory("${ACL_PATH}" "${CMAKE_BINARY_DIR}/acl-build" EXCLUDE_FROM_ALL)
	target_link_libraries(webcool PRIVATE
		fiber_cpp_static
		acl_cpp_static
		protocol_static
		acl_static
		fiber_static)
else()
	target_link_directories(webcool PRIVATE ${ACL_LIBRARY_DIRS})
	target_link_libraries(webcool PRIVATE ${ACL_PREBUILT_LIBRARIES})
endif()

if(WIN32)
	target_link_libraries(webcool PRIVATE
		ws2_32
		iphlpapi
		advapi32
		user32
		shell32
		comdlg32
		ole32
		crypt32)
elseif(APPLE)
	target_link_libraries(webcool PRIVATE
		pthread
		z
		iconv
		dl)
elseif(UNIX)
	target_link_libraries(webcool PRIVATE
		dl
		pthread
		z
		crypt
		rt)
	if(HAS_IO_URING)
		target_link_libraries(webcool PRIVATE uring-ffi)
	endif()
endif()

if(UNIX AND NOT WIN32)
	set_target_properties(webcool PROPERTIES
		BUILD_RPATH "${ACL_LIBRARY_DIRS}"
		INSTALL_RPATH "${ACL_LIBRARY_DIRS}")
endif()
