# Semantic actor reconstructions owned by the A_ACTORS session.
# Include from CMakeLists.txt after the fsb_core target exists:
#   include(src/actor_core/sources.cmake)
#   target_sources(fsb_core PRIVATE ${FSB_ACTOR_CORE_SOURCES})
set(FSB_ACTOR_CORE_SOURCES
    ${CMAKE_CURRENT_LIST_DIR}/actor_slots.cpp
    ${CMAKE_CURRENT_LIST_DIR}/actor_geometry.cpp
    ${CMAKE_CURRENT_LIST_DIR}/actor_runtime.cpp
    ${CMAKE_CURRENT_LIST_DIR}/actor_lifecycle.cpp
    ${CMAKE_CURRENT_LIST_DIR}/ifc_stage.cpp
    ${CMAKE_CURRENT_LIST_DIR}/gatewarp.cpp
    ${CMAKE_CURRENT_LIST_DIR}/party_status.cpp
    ${CMAKE_CURRENT_LIST_DIR}/save_slot.cpp
    ${CMAKE_CURRENT_LIST_DIR}/session_state.cpp
    ${CMAKE_CURRENT_LIST_DIR}/item_menu.cpp
    ${CMAKE_CURRENT_LIST_DIR}/motion_callbacks.cpp
    ${CMAKE_CURRENT_LIST_DIR}/cim_blob.cpp
    ${CMAKE_CURRENT_LIST_DIR}/player_input.cpp
    ${CMAKE_CURRENT_LIST_DIR}/field_interaction.cpp
    ${CMAKE_CURRENT_LIST_DIR}/jump_motion.cpp
    ${CMAKE_CURRENT_LIST_DIR}/map_transition.cpp
    ${CMAKE_CURRENT_LIST_DIR}/a_actors_bridge.cpp
)
