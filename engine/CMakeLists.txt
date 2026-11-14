if(MSVC)
    set(COMBOBITE_WARNINGS /W4 /permissive-)
else()
    set(COMBOBITE_WARNINGS -Wall -Wextra -Wpedantic)
endif()

# Shared code: the menu and target data models, the loader and the data
# checks. The optimizer will be added here later.
add_library(combobite_core STATIC
    src/dates.cpp
    src/loader.cpp
    src/checks.cpp)
target_include_directories(combobite_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(combobite_core PUBLIC nlohmann_json::nlohmann_json)
target_compile_definitions(combobite_core PUBLIC COMBOBITE_VERSION="${PROJECT_VERSION}")
target_compile_options(combobite_core PRIVATE ${COMBOBITE_WARNINGS})

# The engine program the Python front end runs.
add_executable(combobite-engine src/main.cpp)
target_link_libraries(combobite-engine PRIVATE combobite_core)
target_compile_options(combobite-engine PRIVATE ${COMBOBITE_WARNINGS})

# ------------------------------------------------------------------ tests ---

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

# Tasks 6.3 and 6.4: one bad input per loader rule and data check.
add_executable(loader_tests tests/test_loader.cpp)
target_link_libraries(loader_tests PRIVATE combobite_core)
target_compile_options(loader_tests PRIVATE ${COMBOBITE_WARNINGS})
add_test(NAME loader_and_checks
    COMMAND loader_tests ${PROJECT_SOURCE_DIR}/data/chains)
