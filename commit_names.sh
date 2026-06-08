#!/usr/bin/env bash
# Commit hash to build name mapping
# Function to look up commit name by hash

get_commit_name() {
    local hash="$1"
    case "$hash" in
        60601f7) echo "001_cpp_translation_roadmap" ;;
        3ff1cf7) echo "002_mac_cmake_math" ;;
        bd29bbe) echo "003_milestone2_complete" ;;
        c6a6eaf) echo "004_vulkan_render_pipeline" ;;
        460498f) echo "005_milestone4b_colored_cube" ;;
        d67347c) echo "006_milestone4a_world" ;;
        1e8d088) echo "007_milestone4c_lighting" ;;
        3104cc3) echo "008_milestone4_texturing" ;;
        d8fc914) echo "009_milestone5a_obj_loading" ;;
        a3189c0) echo "010_milestone5_audio_loading" ;;
        37e3540) echo "011_roadmap_cleanup" ;;
        7cd5481) echo "012_sphere_collision_wip" ;;
        0e72d4f) echo "013_sphere_collision_fixed" ;;
        b7fef41) echo "014_aabb_spring_tests" ;;
        b0b630a) echo "015_obb_interactions_milestone" ;;
        859a04e) echo "016_pgs_solver" ;;
        2e1668e) echo "017_pgs_improvements" ;;
        9dcb85a) echo "018_aabb_inertia_fix" ;;
        ffc6acf) echo "019_sap_broadphase" ;;
        f0e8720) echo "020_milestone6_threading" ;;
        0f0e524) echo "021_milestone7_audio_playback" ;;
        a8a66cb) echo "022_scripting_interface" ;;
        e1affc3) echo "023_spring_bugfix" ;;
        4878ff3) echo "024_milestone8_background_physics" ;;
        6a9500e) echo "025_milestone9_ui" ;;
        d0fa878) echo "026_milestone10_raytracing" ;;
        d010cc4) echo "027_milestone11_demo_scenes" ;;
        4250f09) echo "028_phase1_complete" ;;
        e390d92) echo "029_registry_setup" ;;
        0c8b266) echo "030_registry_components" ;;
        2d67844) echo "031_registry_integration" ;;
        1a3ff37) echo "032_ecs_scripts" ;;
        4b1cd05) echo "033_lighting_fix" ;;
        197e1db) echo "034_hull_removal" ;;
        ca37ad5) echo "035_demo_binaries" ;;
        e6809fc) echo "036_stability_fixes" ;;
        ba93545) echo "037_profiler_added" ;;
        6fb1012) echo "038_profiler_updates" ;;
        3d87631) echo "039_milestone12_ecs_complete" ;;
        5b74985) echo "040_project_binaries_included" ;;
	9dd3b5c) echo "041_editor_window_part_1" ;;
	8cd8bb5) echo "042_various_editor_bugfixes" ;;
	a67e823) echo "043_m13_complete" ;;
	*) echo "" ;;
    esac
}
