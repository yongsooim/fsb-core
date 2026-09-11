# C_MAPS map/worldmap/interaction reconstructions and their ABI bridge.
target_sources(fsb_core PRIVATE
    src/map_logic/event_flags.cpp
    src/map_logic/map_objects.cpp
    src/map_logic/map_object_actors.cpp
    src/map_logic/map_patch_handlers.cpp
    src/map_logic/map_spawns.cpp
    src/map_logic/worldmap.cpp
    src/map_logic/map_setup.cpp
    src/map_logic/c_maps_bridge.cpp
    src/map_logic/c_maps_installer_bridge.cpp)
