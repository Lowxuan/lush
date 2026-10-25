add_library(lush_core STATIC
    lush/core/version.cpp
    lush/core/utf8.cpp
    lush/core/source.cpp
    lush/core/diagnostic.cpp
    lush/core/lexer.cpp
    lush/core/ast.cpp
    lush/core/parser.cpp
    lush/core/value.cpp
    lush/core/format.cpp
    lush/core/builtins.cpp
    lush/core/validate.cpp
    lush/core/eval.cpp
)

target_include_directories(lush_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(lush_core PUBLIC cxx_std_20)
target_compile_definitions(lush_core PUBLIC LUSH_VERSION="${PROJECT_VERSION}")

add_library(lush::core ALIAS lush_core)

add_library(lush_command STATIC
    lush/command/registry.cpp
    lush/command/session.cpp
)
target_link_libraries(lush_command PUBLIC lush::core)
target_compile_features(lush_command PUBLIC cxx_std_20)
add_library(lush::command ALIAS lush_command)

add_library(lush_driver STATIC
    lush/driver/source_table.cpp
    lush/driver/driver.cpp
)
target_link_libraries(lush_driver PUBLIC lush::command)
target_compile_features(lush_driver PUBLIC cxx_std_20)
add_library(lush::driver ALIAS lush_driver)

add_library(lush_reference STATIC
    reference/reference_host.cpp
)
target_link_libraries(lush_reference PUBLIC lush::command)
target_compile_features(lush_reference PUBLIC cxx_std_20)
add_library(lush::reference ALIAS lush_reference)

if(LUSH_BUILD_CLI)
    add_executable(lush
        cli/main.cpp
        cli/repl.cpp
        cli/console_sink.cpp
    )
    target_link_libraries(lush PRIVATE lush::driver lush::reference)
endif()
