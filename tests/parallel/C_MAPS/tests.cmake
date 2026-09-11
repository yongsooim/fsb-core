# C_MAPS original-execution contracts. Include after the other native contracts.
add_executable(fsb_c_maps_tests tests/parallel/C_MAPS/map_object_tests.cpp)
target_link_libraries(fsb_c_maps_tests PRIVATE fsb_core)
if(EMSCRIPTEN)
    target_link_options(fsb_c_maps_tests PRIVATE -sENVIRONMENT=node -sNODERAWFS=1 -sINITIAL_MEMORY=268435456)
    set(FSB_C_MAPS_RUNNER "${NODE_EXECUTABLE}" $<TARGET_FILE:fsb_c_maps_tests>)
else()
    set(FSB_C_MAPS_RUNNER fsb_c_maps_tests)
endif()
add_test(NAME c_maps_object_contract COMMAND ${FSB_C_MAPS_RUNNER}
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/FLYINGSB.EXE"
    "${CMAKE_CURRENT_SOURCE_DIR}/reference/parallel/C_MAPS/map-objects-x86.bin")
