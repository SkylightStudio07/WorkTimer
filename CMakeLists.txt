cmake_minimum_required(VERSION 3.16)
project(WorkTimer VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- wxWidgets path (edit to match your installation) ---
set(wxWidgets_ROOT_DIR "C:/wxWidgets-3.2.4" CACHE PATH "wxWidgets root directory")

# Release=mswu / Debug=mswud
set(wxWidgets_CONFIGURATION mswu)

find_package(wxWidgets REQUIRED COMPONENTS core base adv qa)
include(${wxWidgets_USE_FILE})

set(SOURCES src/main.cpp)

if(WIN32)
    list(APPEND SOURCES resources/app.rc)
endif()

add_executable(WorkTimer WIN32 ${SOURCES})

target_link_libraries(WorkTimer PRIVATE
    ${wxWidgets_LIBRARIES}
    psapi
    comctl32
)

if(MSVC)
    target_compile_options(WorkTimer PRIVATE /W3 /utf-8 /MP)
    target_compile_definitions(WorkTimer PRIVATE
        _CRT_SECURE_NO_WARNINGS
        _SILENCE_ALL_CXX17_DEPRECATION_WARNINGS
    )
endif()

set_target_properties(WorkTimer PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

install(TARGETS WorkTimer DESTINATION ".")
