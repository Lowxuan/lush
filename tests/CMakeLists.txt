add_executable(lush_tests
    test_main.cpp
    unit_smoke.cpp
    unit_lexer.cpp
    unit_parser.cpp
    unit_value.cpp
    unit_ref.cpp
    unit_eval.cpp
    unit_command.cpp
    golden_spec.cpp
)

target_link_libraries(lush_tests PRIVATE lush::reference)
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

    add_test(NAME lush_dump_ast
        COMMAND $<TARGET_FILE:lush> --dump-ast -e "echo 1"
    )
    set_tests_properties(lush_dump_ast PROPERTIES
        PASS_REGULAR_EXPRESSION "Program"
    )
endif()
