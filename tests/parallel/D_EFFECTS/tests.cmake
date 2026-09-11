# D_EFFECTS contract tests. The integration session includes this from
# CMakeLists.txt inside the block that already defines the other contract tests:
#     include(tests/parallel/D_EFFECTS/tests.cmake)
# Each test replays a fixture recorded from the original binary by the matching
# tools/parallel/D_EFFECTS/prepare_*.py script.
add_executable(fsb_d_effects_skill_callback_tests tests/parallel/D_EFFECTS/skill_callback_tests.cpp)
target_link_libraries(fsb_d_effects_skill_callback_tests PRIVATE fsb_core)
if(EMSCRIPTEN)
    find_program(NODE_EXECUTABLE node REQUIRED)
    target_link_options(fsb_d_effects_skill_callback_tests PRIVATE -sENVIRONMENT=node -sNODERAWFS=1)
    set(FSB_D_EFFECTS_SKILL_CALLBACK_RUNNER "${NODE_EXECUTABLE}" $<TARGET_FILE:fsb_d_effects_skill_callback_tests>)
else()
    set(FSB_D_EFFECTS_SKILL_CALLBACK_RUNNER fsb_d_effects_skill_callback_tests)
endif()
add_test(NAME d_effects_skill_callback_contract
    COMMAND ${FSB_D_EFFECTS_SKILL_CALLBACK_RUNNER}
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/FLYINGSB.EXE"
    "${CMAKE_CURRENT_SOURCE_DIR}/reference/parallel/D_EFFECTS/skill-callbacks-x86.bin")

add_executable(fsb_d_effects_effect_object_tests tests/parallel/D_EFFECTS/effect_object_tests.cpp)
target_link_libraries(fsb_d_effects_effect_object_tests PRIVATE fsb_core)
if(EMSCRIPTEN)
    target_link_options(fsb_d_effects_effect_object_tests PRIVATE -sENVIRONMENT=node -sNODERAWFS=1)
    set(FSB_D_EFFECTS_EFFECT_OBJECT_RUNNER "${NODE_EXECUTABLE}" $<TARGET_FILE:fsb_d_effects_effect_object_tests>)
else()
    set(FSB_D_EFFECTS_EFFECT_OBJECT_RUNNER fsb_d_effects_effect_object_tests)
endif()
add_test(NAME d_effects_effect_object_contract
    COMMAND ${FSB_D_EFFECTS_EFFECT_OBJECT_RUNNER}
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/FLYINGSB.EXE"
    "${CMAKE_CURRENT_SOURCE_DIR}/reference/parallel/D_EFFECTS/effect-objects-x86.bin")
