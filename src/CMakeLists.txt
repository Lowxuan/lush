add_library(lush_core STATIC
    lush/core/version.cpp
    lush/core/utf8.cpp
    lush/core/source.cpp
    lush/core/diagnostic.cpp
    lush/core/lexer.cpp
)

target_include_directories(lush_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(lush_core PUBLIC cxx_std_20)
target_compile_definitions(lush_core PUBLIC LUSH_VERSION="${PROJECT_VERSION}")

add_library(lush::core ALIAS lush_core)

if(LUSH_BUILD_CLI)
    add_executable(lush
        cli/main.cpp
    )
    target_link_libraries(lush PRIVATE lush::core)
endif()
