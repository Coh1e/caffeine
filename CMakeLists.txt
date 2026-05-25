cmake_minimum_required(VERSION 3.20)
project(caffeine LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# WIN32 → /SUBSYSTEM:WINDOWS, wWinMain entry, no console window.
# caffeine.rc embeds the coffee-cup icon (caffeine.ico) and the manifest.
add_executable(caffeine WIN32
    main.cpp
    caffeine.rc
)

target_link_libraries(caffeine PRIVATE user32 shell32)

if(MSVC)
    target_compile_options(caffeine PRIVATE /W4 /permissive- /utf-8)
    # caffeine.rc already embeds the application manifest (caffeine.manifest);
    # stop the linker from generating a second one (CVT1100 duplicate MANIFEST).
    target_link_options(caffeine PRIVATE /MANIFEST:NO)
endif()
