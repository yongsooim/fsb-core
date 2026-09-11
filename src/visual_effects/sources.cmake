# D_EFFECTS semantic modules. The integration session includes this from
# CMakeLists.txt after the recovered sources:
#     include(src/visual_effects/sources.cmake)
#     target_sources(fsb_core PRIVATE ${FSB_VISUAL_EFFECTS_SOURCES})
set(FSB_VISUAL_EFFECTS_SOURCES
    src/visual_effects/directional_particles.cpp
    src/visual_effects/effect_lifetimes.cpp
    src/visual_effects/effect_objects.cpp
    src/visual_effects/projectiles.cpp
    src/visual_effects/sequences.cpp
    src/visual_effects/effect_objects_bridge.cpp
    src/visual_effects/skill_callbacks.cpp
    src/visual_effects/d_effects_bridge.cpp
)
