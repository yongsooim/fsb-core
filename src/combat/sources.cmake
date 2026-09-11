# B_COMBAT semantic combat modules and their temporary ABI bridge.
set(FSB_COMBAT_SOURCES
    src/combat/status.cpp
    src/combat/pose.cpp
    src/combat/ai.cpp
    src/combat/grid.cpp
    src/combat/flow.cpp
    src/combat/object_motion.cpp
    src/combat/effects.cpp
    src/combat/actions.cpp
    src/combat/b_combat_bridge.cpp
)

# Session test target and its original-execution fixtures.
function(fsb_combat_register_tests)
    add_executable(fsb_combat_contract_tests tests/parallel/B_COMBAT/combat_contract_tests.cpp)
    target_link_libraries(fsb_combat_contract_tests PRIVATE fsb_core)
    if(EMSCRIPTEN)
        find_program(NODE_EXECUTABLE node REQUIRED)
        target_link_options(fsb_combat_contract_tests PRIVATE -sENVIRONMENT=node -sNODERAWFS=1)
        set(runner "${NODE_EXECUTABLE}" $<TARGET_FILE:fsb_combat_contract_tests>)
    else()
        set(runner fsb_combat_contract_tests)
    endif()
    foreach(fixture status-rules poses ai-scoring grid targeting flow ai-helpers ai-regions turn-setup camera placement placement-entry reach leave motion effect-objects actions refresh-edges)
        string(REPLACE "-" "_" name "${fixture}")
        add_test(NAME combat_${name}_contract COMMAND ${runner}
            "${CMAKE_CURRENT_SOURCE_DIR}/reference/battle-action-input.bin"
            "${CMAKE_CURRENT_SOURCE_DIR}/reference/parallel/B_COMBAT/combat-${fixture}-x86.bin")
    endforeach()
endfunction()
