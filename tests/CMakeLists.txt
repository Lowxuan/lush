add_executable(lush_tests
    test_main.cpp
    unit_smoke.cpp
    unit_lexer.cpp
)

target_link_libraries(lush_tests PRIVATE lush::core)
target_include_directories(lush_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

add_test(NAME lush_tests COMMAND $<TARGET_FILE:lush_tests>)

if(TARGET lush)
    add_test(NAME lush_version COMMAND $<TARGET_FILE:lush> --version)
    set_tests_properties(lush_version PROPERTIES
        PASS_REGULAR_EXPRESSION "lush ${PROJECT_VERSION}"
    )

    add_test(NAME lush_dump_tokens
        COMMAND $<TARGET_FILE:lush> --dump-tokens -e "echo 1"
    )
    set_tests_properties(lush_dump_tokens PROPERTIES
        PASS_REGULAR_EXPRESSION "EOF"
    )
endif()
