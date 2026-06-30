cmake_minimum_required(VERSION 3.16)

project(iosync LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(IOSYNC_COMMON
    src/main.cpp
    src/media_item.cpp)

if(APPLE)
    enable_language(OBJCXX)
    set(IOSYNC_PLATFORM_SOURCES src/macos/image_capture.mm)
    add_executable(iosync ${IOSYNC_COMMON} ${IOSYNC_PLATFORM_SOURCES})
    target_link_libraries(iosync PRIVATE "-framework Foundation" "-framework ImageCaptureCore")
    target_compile_options(iosync PRIVATE -fobjc-arc)
elseif(WIN32)
    set(IOSYNC_PLATFORM_SOURCES src/windows/wpd.cpp)
    add_executable(iosync ${IOSYNC_COMMON} ${IOSYNC_PLATFORM_SOURCES})
    target_compile_definitions(iosync PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(iosync PRIVATE PortableDeviceGuids ole32 oleaut32 propsys uuid)
else()
    set(IOSYNC_PLATFORM_SOURCES src/unsupported/unsupported.cpp)
    add_executable(iosync ${IOSYNC_COMMON} ${IOSYNC_PLATFORM_SOURCES})
endif()

target_include_directories(iosync PRIVATE include)

if(MSVC)
    target_compile_options(iosync PRIVATE /W4 /permissive- /EHsc /utf-8)
else()
    target_compile_options(iosync PRIVATE -Wall -Wextra -Wpedantic)
endif()
