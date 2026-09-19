cmake_minimum_required(VERSION 3.20)
project(OuterWorldsSpacersChoiceHeadTracking VERSION 0.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(WIN32)
    add_definitions(-DWIN32_LEAN_AND_MEAN -DNOMINMAX -D_CRT_SECURE_NO_WARNINGS -DUNICODE -D_UNICODE)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

if(MSVC)
    add_compile_options(/permissive- /Zc:__cplusplus /MP)
endif()

# MinHook (for the GetPlayerViewPoint trampoline). Fetched + built locally;
# cameraunlock-core's hook manager links against this `minhook` target.
#
# Pinned to the COMMIT, not the tag. A git tag is mutable, and this is the one
# third-party dependency compiled into the shipped .asi - so a moved tag would
# put arbitrary code in every release from that moment with no diff to catch it.
# Every GitHub Action in this repo is pinned the same way, for the same reason.
include(FetchContent)
FetchContent_Declare(
    minhook_src
    GIT_REPOSITORY https://github.com/TsudaKageyu/minhook.git
    GIT_TAG        9fbd087432700d73fc571118d6a9697a36443d88  # v1.3.3
)
FetchContent_MakeAvailable(minhook_src)
add_library(minhook STATIC
    ${minhook_src_SOURCE_DIR}/src/hook.c
    ${minhook_src_SOURCE_DIR}/src/buffer.c
    ${minhook_src_SOURCE_DIR}/src/trampoline.c
    ${minhook_src_SOURCE_DIR}/src/hde/hde64.c
    ${minhook_src_SOURCE_DIR}/src/hde/hde32.c
)
target_include_directories(minhook PUBLIC ${minhook_src_SOURCE_DIR}/include)
if(MSVC)
    target_compile_options(minhook PRIVATE /wd4267 /wd4244 /wd4127 /wd4131)
endif()

# Shared cameraunlock-core (C++ side) with the hook manager and UE runtime
# helpers enabled.
set(CAMERAUNLOCK_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(CAMERAUNLOCK_BUILD_HOOKS ON CACHE BOOL "" FORCE)
set(CAMERAUNLOCK_BUILD_UNREAL ON CACHE BOOL "" FORCE)
add_subdirectory(cameraunlock-core/cpp ${CMAKE_BINARY_DIR}/cameraunlock-core)

set(MOD_SOURCES
    src/dllmain.cpp
    src/headtracking_mod.cpp
    src/config.cpp
    src/mod_hotkeys.cpp
    src/ads.cpp
    src/ads_pose.cpp
    src/ads_state.cpp
    src/view_hook.cpp
    src/view_hook_diag.cpp
    src/camera_boundary.cpp
    src/camera_fov.cpp
    src/game_state.cpp
    src/conversation_state.cpp
    src/aim_projection.cpp
    src/aim_trace.cpp
    src/lean_trace.cpp
    src/reticle_mover.cpp
    src/widget_probe.cpp
    src/window_centering.cpp
    src/ue_reflect.cpp
    src/ue_vm.cpp
    src/builds/build_registry.cpp
    src/builds/steam_offsets.cpp
)

add_library(OuterWorldsSpacersChoiceHeadTracking SHARED ${MOD_SOURCES})

target_include_directories(OuterWorldsSpacersChoiceHeadTracking PRIVATE
    src
    cameraunlock-core/cpp/include
)

target_link_libraries(OuterWorldsSpacersChoiceHeadTracking PRIVATE
    cameraunlock
    cameraunlock_hooks
    cameraunlock_unreal
    minhook
    ws2_32
    psapi
)

# Output as .asi for Ultimate ASI Loader.
set_target_properties(OuterWorldsSpacersChoiceHeadTracking PROPERTIES
    SUFFIX ".asi"
    PREFIX ""
)

# Behaviour locks for the pure-function parts of the aim-down-sights layer: the
# verdict walk, and what the sights do to the head pose. Separate executables
# that link none of the hook or Windows machinery, so `pixi run test` runs with
# no game installed and no graphics device.
option(TOW_BUILD_TESTS "Build the behaviour tests" ON)
if(TOW_BUILD_TESTS)
    enable_testing()

    add_executable(tow_ads_gate_tests tests/ads_gate_tests.cpp)
    target_include_directories(tow_ads_gate_tests PRIVATE
        src
        tests
        cameraunlock-core/cpp/include
    )
    add_test(NAME tow_ads_gate_tests COMMAND tow_ads_gate_tests)

    add_executable(tow_ads_pose_tests
        tests/ads_pose_tests.cpp
        src/ads.cpp
        src/ads_pose.cpp
    )
    target_include_directories(tow_ads_pose_tests PRIVATE
        src
        tests
        cameraunlock-core/cpp/include
    )
    target_link_libraries(tow_ads_pose_tests PRIVATE cameraunlock)
    add_test(NAME tow_ads_pose_tests COMMAND tow_ads_pose_tests)

    # The engine boundary: the axis mapping, signs and unit scale the camera
    # is written through, and the FOV-to-tangent maths the reticle projection
    # divides by. Both are pure, so they run with no game and no window.
    add_executable(tow_camera_boundary_tests
        tests/camera_boundary_tests.cpp
        src/camera_boundary.cpp
    )
    target_include_directories(tow_camera_boundary_tests PRIVATE
        src
        tests
        cameraunlock-core/cpp/include
    )
    add_test(NAME tow_camera_boundary_tests COMMAND tow_camera_boundary_tests)

    add_executable(tow_camera_fov_tests tests/camera_fov_tests.cpp)
    target_include_directories(tow_camera_fov_tests PRIVATE
        src
        tests
        cameraunlock-core/cpp/include
    )
    add_test(NAME tow_camera_fov_tests COMMAND tow_camera_fov_tests)

    # The ini boundary: what a user-editable file is allowed to say, and what
    # happens to a value that says something else. Pure, so it needs no file.
    add_executable(tow_config_sanitize_tests tests/config_sanitize_tests.cpp)
    target_include_directories(tow_config_sanitize_tests PRIVATE
        src
        tests
        cameraunlock-core/cpp/include
    )
    add_test(NAME tow_config_sanitize_tests COMMAND tow_config_sanitize_tests)

    # The wiring above the value guards: every ini key reaching the field it
    # names, an absent key keeping its compiled default, and the ini a fresh
    # install gets reading back as the defaults it documents. Runs against a
    # temporary directory, so it needs no game install.
    add_executable(tow_config_load_tests
        tests/config_load_tests.cpp
        src/config.cpp
    )
    target_include_directories(tow_config_load_tests PRIVATE
        src
        tests
        cameraunlock-core/cpp/include
    )
    target_link_libraries(tow_config_load_tests PRIVATE cameraunlock)
    add_test(NAME tow_config_load_tests COMMAND tow_config_load_tests)

    # The width check both Kismet traces run before writing a parameter frame
    # into a stack buffer at offsets read out of the engine's reflection data.
    add_executable(tow_kismet_frame_tests tests/kismet_frame_tests.cpp)
    target_include_directories(tow_kismet_frame_tests PRIVATE
        src
        tests
        cameraunlock-core/cpp/include
    )
    add_test(NAME tow_kismet_frame_tests COMMAND tow_kismet_frame_tests)

    # Recovery from a tracker port that is busy at startup. Unlike the two above
    # this one runs live sockets and threads and times the recovery, because the
    # thing being checked is a cadence, and a cadence cannot be read off the
    # source. Still needs no game and no graphics device.
    add_executable(tow_udp_recovery_tests tests/udp_recovery_tests.cpp)
    target_include_directories(tow_udp_recovery_tests PRIVATE
        src
        tests
        cameraunlock-core/cpp/include
    )
    target_link_libraries(tow_udp_recovery_tests PRIVATE cameraunlock ws2_32)
    add_test(NAME tow_udp_recovery_tests COMMAND tow_udp_recovery_tests)
    # Five release trials, each dwelling up to a second first. RUN_SERIAL because
    # it binds a real socket and times a recovery cadence: sharing a core with
    # another test deschedules the supervisor thread and the measurement, not the
    # code, is what fails.
    set_tests_properties(tow_udp_recovery_tests PROPERTIES TIMEOUT 120 RUN_SERIAL TRUE)
endif()
