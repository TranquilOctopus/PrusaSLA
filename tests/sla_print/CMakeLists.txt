get_filename_component(_TEST_NAME ${CMAKE_CURRENT_LIST_DIR} NAME)
add_executable(${_TEST_NAME}_tests ${_TEST_NAME}_tests_main.cpp
    sla_print_tests.cpp
    sla_test_utils.hpp sla_test_utils.cpp
    sla_supptgen_tests.cpp
    sla_raycast_tests.cpp
    sla_parabola_tests.cpp
    sla_voronoi_graph_tests.cpp
    sla_vectorUtils_tests.cpp
    sla_lineUtils_tests.cpp
    sla_supptreeutils_tests.cpp
    sla_zcorrection_tests.cpp
    sla_island_detection_tests.cpp
    sla_cavity_detection_tests.cpp
    sla_layer_stats_tests.cpp
    sla_raft_preset_tests.cpp
    sla_raft_type_tests.cpp
    sla_raft_edge_taper_tests.cpp
    sla_raft_infill_tests.cpp
    sla_raft_floor_thickness_tests.cpp
    sla_invalidation_tests.cpp
    sla_rotfinder_tests.cpp
    sla_layer_image_tests.cpp
    sla_raster_memory_tests.cpp
    sla_layers_to_mesh_tests.cpp
    sla_island_coverage_tests.cpp
    sla_local_island_coverage_tests.cpp
    sla_support_tool_tests.cpp
    sla_support_preset_tests.cpp
    sla_support_base_shape_tests.cpp
    sla_branching_volume_tests.cpp
    sla_support_placement_tests.cpp
    sla_support_point_geometry_tests.cpp
    sla_support_on_model_tests.cpp
    sla_benchmark_tests.cpp
    sla_layer_hash_tests.cpp
    sla_support_brace_tests.cpp
    sla_hollowing_tests.cpp
    sla_robustness_tests.cpp
    sla_support_regression_tests.cpp
    sla_pad_robustness_tests.cpp)

# mold linker for successful linking needs also to link TBB library and link it before libslic3r.
target_link_libraries(${_TEST_NAME}_tests test_common TBB::tbb TBB::tbbmalloc libslic3r slic3r-shared)
# TODO This temporarily allows using private header files from slic3r-shared in all tests, the
# same hack tests/CMakeLists.txt does for libslic3r. sla_bench_manifest.hpp reads the model
# manifest through the Biz Yaml wrapper, which lives in slic3r-shared's private sources.
target_include_directories(${_TEST_NAME}_tests PRIVATE "$<TARGET_PROPERTY:slic3r-shared,SOURCE_DIR>/src")
set_property(TARGET ${_TEST_NAME}_tests PROPERTY FOLDER "tests")

if (WIN32)
    # sla_benchmark_tests.cpp reads the peak working set with GetProcessMemoryInfo.
    target_link_libraries(${_TEST_NAME}_tests psapi)
    prusaslicer_copy_dlls(${_TEST_NAME}_tests)
endif()

# catch_discover_tests(${_TEST_NAME}_tests TEST_PREFIX "${_TEST_NAME}: ")
add_test(${_TEST_NAME}_tests ${_TEST_NAME}_tests ${CATCH_EXTRA_ARGS})

if (WIN32)
    # Adds a post-build copy of libgmp-10.dll
    add_custom_command(TARGET ${_TEST_NAME}_tests POST_BUILD
          COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${CMAKE_PREFIX_PATH}/bin/libgmp-10.dll"
          $<TARGET_FILE_DIR:${_TEST_NAME}_tests>)
endif()
