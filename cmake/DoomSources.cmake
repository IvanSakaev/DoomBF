# Source inventory only. The original Doom implementation files are intentionally
# kept untouched; platform wrappers and build scripts consume this list.

if(NOT DEFINED DOOM_SOURCE_DIR)
    get_filename_component(DOOM_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../doom" ABSOLUTE)
endif()

set(DOOM_BACKEND_BASENAMES
    am_map d_items d_main d_net doomdef doomstat dstrings f_finale f_wipe
    g_game hu_lib hu_stuff i_net info i_sound i_system i_video m_argv m_bbox
    m_cheat m_fixed m_menu m_misc m_random m_swap p_ceilng p_doors p_enemy
    p_floor p_inter p_lights p_map p_maputl p_mobj p_plats p_pspr p_saveg
    p_setup p_sight p_spec p_switch p_telept p_tick p_user r_bsp r_data
    r_draw r_main r_plane r_segs r_sky r_things sounds s_sound st_lib
    st_stuff tables v_video wi_stuff w_wad z_zone DOOM
)

set(DOOM_BACKEND_SOURCES)
foreach(name IN LISTS DOOM_BACKEND_BASENAMES)
    list(APPEND DOOM_BACKEND_SOURCES "${DOOM_SOURCE_DIR}/backend/${name}.c")
endforeach()

set(DOOM_CRT_SOURCES
    "${DOOM_SOURCE_DIR}/crt/doom_crt.c"
    "${DOOM_SOURCE_DIR}/crt/heap.c"
    "${DOOM_SOURCE_DIR}/crt/printf.c"
)
