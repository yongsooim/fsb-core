# A_ACTORS contract test. Include from CMakeLists.txt after fsb_core exists and
# after EMSCRIPTEN/NODE_EXECUTABLE have been resolved:
#   include(tests/parallel/A_ACTORS/tests.cmake)
add_executable(fsb_actor_core_tests ${CMAKE_CURRENT_LIST_DIR}/actor_core_tests.cpp)
target_link_libraries(fsb_actor_core_tests PRIVATE fsb_core)
if(EMSCRIPTEN)
    target_link_options(fsb_actor_core_tests PRIVATE -sENVIRONMENT=node -sNODERAWFS=1)
    add_test(NAME actor_core_contract COMMAND "${NODE_EXECUTABLE}" $<TARGET_FILE:fsb_actor_core_tests>
             "${CMAKE_CURRENT_SOURCE_DIR}/reference/battle-action-input.bin"
             "${CMAKE_CURRENT_SOURCE_DIR}/reference/parallel/A_ACTORS/actor-core-x86.bin")
else()
    add_test(NAME actor_core_contract COMMAND fsb_actor_core_tests
             "${CMAKE_CURRENT_SOURCE_DIR}/reference/battle-action-input.bin"
             "${CMAKE_CURRENT_SOURCE_DIR}/reference/parallel/A_ACTORS/actor-core-x86.bin")
endif()
