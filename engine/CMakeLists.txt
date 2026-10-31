# Shared code: the menu data model. The optimizer and loader will be added here later.
add_library(combobite_core INTERFACE)
target_include_directories(combobite_core INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(combobite_core INTERFACE nlohmann_json::nlohmann_json)
target_compile_definitions(combobite_core INTERFACE
    COMBOBITE_VERSION="${PROJECT_VERSION}")

if(MSVC)
    set(COMBOBITE_WARNINGS /W4 /permissive-)
else()
    set(COMBOBITE_WARNINGS -Wall -Wextra -Wpedantic)
endif()

# The engine program the Python front end runs.
add_executable(combobite-engine src/main.cpp)
target_link_libraries(combobite-engine PRIVATE combobite_core)
target_compile_options(combobite-engine PRIVATE ${COMBOBITE_WARNINGS})

# Tests
add_executable(engine_tests tests/test_menu_format.cpp)
target_link_libraries(engine_tests PRIVATE combobite_core)
target_compile_options(engine_tests PRIVATE ${COMBOBITE_WARNINGS})
add_test(NAME menu_format_sample
    COMMAND engine_tests ${PROJECT_SOURCE_DIR}/data/sample/sample_chain.json)
foreach(chain mcdonalds wendys burger_king taco_bell)
    add_test(NAME menu_format_${chain}
        COMMAND engine_tests ${PROJECT_SOURCE_DIR}/data/chains/${chain}.json)
endforeach()

add_executable(target_tests tests/test_target_format.cpp)
target_link_libraries(target_tests PRIVATE combobite_core)
target_compile_options(target_tests PRIVATE ${COMBOBITE_WARNINGS})
add_test(NAME target_format
    COMMAND target_tests ${PROJECT_SOURCE_DIR}/tests/target_examples.json)
