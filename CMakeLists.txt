cmake_minimum_required(VERSION 3.20)
project(amped LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Generate full.ico / empty.ico via the PowerShell helper before the .rc
# is compiled. The custom command is idempotent — it overwrites files,
# but only runs when make_icons.ps1 changes.
set(_AMPED_ICOS
    "${CMAKE_CURRENT_SOURCE_DIR}/full.ico"
    "${CMAKE_CURRENT_SOURCE_DIR}/empty.ico")

add_custom_command(
    OUTPUT  ${_AMPED_ICOS}
    COMMAND powershell -ExecutionPolicy Bypass -File
            "${CMAKE_CURRENT_SOURCE_DIR}/make_icons.ps1"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/make_icons.ps1"
    COMMENT "Generating full.ico / empty.ico"
    VERBATIM)

add_custom_target(amped_icons DEPENDS ${_AMPED_ICOS})

# WIN32 → /SUBSYSTEM:WINDOWS, wWinMain entry, no console window.
add_executable(amped WIN32
    main.cpp
    amped.rc
)
add_dependencies(amped amped_icons)

target_link_libraries(amped PRIVATE user32 shell32)

if(MSVC)
    target_compile_options(amped PRIVATE /W4 /permissive- /utf-8)
endif()
