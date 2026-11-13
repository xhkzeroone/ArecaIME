cmake_minimum_required(VERSION 3.20)

project(fcitx5-areca VERSION 8.3.13 LANGUAGES C CXX)

include(GNUInstallDirs)
include(CTest)

find_package(Fcitx5Core REQUIRED)
find_package(Fcitx5Config REQUIRED)
find_package(Fcitx5Utils REQUIRED)
find_program(GO_EXECUTABLE go REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(DBUS REQUIRED dbus-1)
pkg_check_modules(LIBINPUT REQUIRED IMPORTED_TARGET libinput)
pkg_check_modules(UDEV REQUIRED IMPORTED_TARGET libudev)
pkg_check_modules(LIBEI QUIET IMPORTED_TARGET libei-1.0>=1.0)
pkg_check_modules(LIBPORTAL QUIET IMPORTED_TARGET libportal>=0.7.1)
# Backend phím ảo Wayland: thiếu thì backend báo không khả dụng.
pkg_check_modules(WAYLAND_CLIENT QUIET IMPORTED_TARGET wayland-client)
find_program(WAYLAND_SCANNER wayland-scanner)

add_executable(areca-mouse-monitor tools/mouse_monitor.cpp)
target_compile_features(areca-mouse-monitor PRIVATE cxx_std_20)
target_compile_options(areca-mouse-monitor PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(areca-mouse-monitor PRIVATE PkgConfig::LIBINPUT PkgConfig::UDEV)
install(TARGETS areca-mouse-monitor DESTINATION "${CMAKE_INSTALL_LIBEXECDIR}")
install(FILES data/70-areca-pointer.rules DESTINATION "lib/udev/rules.d")

option(ARECA_BUILD_LEGACY_SETTINGS "Build the legacy ImGui Areca settings application" OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(BAMBOO_BRIDGE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/bamboo-bridge")
set(BAMBOO_ARCHIVE "${CMAKE_CURRENT_BINARY_DIR}/libareca-bamboo.a")
file(GLOB BAMBOO_CORE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/bamboo/bamboo-core/*.go")

add_custom_command(
    OUTPUT "${BAMBOO_ARCHIVE}" "${CMAKE_CURRENT_BINARY_DIR}/libareca-bamboo.h"
    COMMAND "${CMAKE_COMMAND}" -E env
            "GOCACHE=${CMAKE_CURRENT_BINARY_DIR}/go-build-cache"
            "${GO_EXECUTABLE}" build -buildvcs=false -buildmode=c-archive
            -o "${BAMBOO_ARCHIVE}" .
    WORKING_DIRECTORY "${BAMBOO_BRIDGE_DIR}"
    DEPENDS
        "${BAMBOO_BRIDGE_DIR}/go.mod"
        "${BAMBOO_BRIDGE_DIR}/main.go"
        ${BAMBOO_CORE_SOURCES}
    VERBATIM)
add_custom_target(areca-bamboo-bridge DEPENDS "${BAMBOO_ARCHIVE}")

add_library(areca-bamboo STATIC IMPORTED GLOBAL)
set_target_properties(areca-bamboo PROPERTIES IMPORTED_LOCATION "${BAMBOO_ARCHIVE}")
add_dependencies(areca-bamboo areca-bamboo-bridge)

add_library(areca MODULE
    src/adaptive_wait_monitor.cpp
    src/areca_engine.cpp
    src/areca_engine_actions.cpp
    src/areca_engine_backend.cpp
    src/areca_engine_backend_menu.cpp
    src/areca_engine_config.cpp
    src/bamboo_engine_adapter.cpp
    src/browser_autocomplete.cpp
    src/forward_backspace_backend.cpp
    src/notification/dynamic_island_announcement.cpp
    src/notification/input_method_announcement.cpp
    src/input_scheduler.cpp
    src/input_type_detector.cpp
    src/preedit_mode.cpp
    src/preedit_logic.cpp
    src/program_compatibility.cpp
    src/redirect_mode.cpp
    src/rewrite_mode.cpp
    src/sentence_capitalizer.cpp
    src/surrounding_text_backend.cpp
    src/surrounding_text_restore.cpp
    src/surrounding_text_v2_backend.cpp
    src/surrounding_text_cache.cpp
    src/uinput_backspace_backend.cpp
    src/uinput_device.cpp
    src/uinput_shift_select_backend.cpp
    src/virtual_keyboard_backspace_backend.cpp
    src/virtual_keyboard_device.cpp
    src/virtual_keyboard_keymap.cpp
    src/atspi/atspi_connection.cpp
    src/atspi/atspi_accessible.cpp
    src/window_focus_tracker.cpp
    src/mouse_click_tracker.cpp
    src/native_device.cpp
    src/word_segment.cpp
    src/native_backspace_backend.cpp
    src/native_shift_select_backend.cpp)
target_compile_definitions(areca PRIVATE
    ARECA_MOUSE_HELPER_PATH="${CMAKE_INSTALL_FULL_LIBEXECDIR}/areca-mouse-monitor"
    ARECA_SETTINGS_PATH="${CMAKE_INSTALL_FULL_BINDIR}/areca-settings")
if(Fcitx5Core_VERSION VERSION_GREATER_EQUAL "5.1.11")
    target_compile_definitions(areca PRIVATE ARECA_HAVE_SHOW_CUSTOM_INFO=1)
endif()
if(LIBEI_FOUND AND LIBPORTAL_FOUND)
    message(STATUS "Native Wayland backend enabled (libei ${LIBEI_VERSION}, libportal ${LIBPORTAL_VERSION})")
    target_compile_definitions(areca PRIVATE ARECA_HAVE_LIBEI=1)
    if(LIBPORTAL_VERSION VERSION_GREATER_EQUAL 0.8)
        target_compile_definitions(areca PRIVATE ARECA_LIBPORTAL_PERSIST=1)
    else()
        message(WARNING "libportal < 0.8: Native Wayland permission cannot be restored across sessions")
    endif()
    target_link_libraries(areca PRIVATE PkgConfig::LIBEI PkgConfig::LIBPORTAL)
else()
    message(WARNING "Native Wayland backend disabled (libei or libportal missing)")
endif()
if(WAYLAND_CLIENT_FOUND AND WAYLAND_SCANNER)
    set(ARECA_VK_XML "${CMAKE_CURRENT_SOURCE_DIR}/protocols/virtual-keyboard-unstable-v1.xml")
    set(ARECA_VK_HEADER "${CMAKE_CURRENT_BINARY_DIR}/virtual-keyboard-unstable-v1-client-protocol.h")
    set(ARECA_VK_SOURCE "${CMAKE_CURRENT_BINARY_DIR}/virtual-keyboard-unstable-v1-protocol.c")
    add_custom_command(
        OUTPUT "${ARECA_VK_HEADER}"
        COMMAND "${WAYLAND_SCANNER}" client-header "${ARECA_VK_XML}" "${ARECA_VK_HEADER}"
        DEPENDS "${ARECA_VK_XML}" VERBATIM)
    add_custom_command(
        OUTPUT "${ARECA_VK_SOURCE}"
        COMMAND "${WAYLAND_SCANNER}" private-code "${ARECA_VK_XML}" "${ARECA_VK_SOURCE}"
        DEPENDS "${ARECA_VK_XML}" VERBATIM)
    target_sources(areca PRIVATE "${ARECA_VK_HEADER}" "${ARECA_VK_SOURCE}")
    target_include_directories(areca PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
    target_compile_definitions(areca PRIVATE ARECA_HAVE_WAYLAND_VK=1)
    target_link_libraries(areca PRIVATE PkgConfig::WAYLAND_CLIENT)
    message(STATUS "Virtual keyboard backend enabled")
else()
    message(WARNING "Virtual keyboard backend disabled (wayland-client or wayland-scanner missing)")
endif()
add_dependencies(areca areca-mouse-monitor)
set_target_properties(areca PROPERTIES PREFIX "" OUTPUT_NAME "areca")
target_compile_features(areca PRIVATE cxx_std_20)
target_compile_options(areca PRIVATE -Wall -Wextra -Wpedantic)
target_include_directories(areca PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src" ${DBUS_INCLUDE_DIRS})
# Addon thiếu symbol chỉ lộ ra lúc fcitx5 nạp; bắt linker báo ngay khi build.
target_link_options(areca PRIVATE "LINKER:--no-undefined")
target_link_libraries(areca PRIVATE
    areca-bamboo
    Fcitx5::Core
    Fcitx5::Config
    Fcitx5::Utils
    ${DBUS_LIBRARIES}
    pthread dl m)

if(ARECA_BUILD_LEGACY_SETTINGS)
    find_package(SDL3 QUIET CONFIG)
    if(NOT SDL3_FOUND)
        pkg_check_modules(SDL3 QUIET IMPORTED_TARGET sdl3)
        if(TARGET PkgConfig::SDL3)
            add_library(SDL3::SDL3 ALIAS PkgConfig::SDL3)
            set(SDL3_FOUND TRUE)
        endif()
    endif()

    if(NOT SDL3_FOUND)
        message(STATUS "SDL3 not found on system; downloading and building SDL3 via FetchContent...")
        include(FetchContent)
        FetchContent_Declare(
            SDL3
            GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
            GIT_TAG release-3.2.8
            GIT_SHALLOW TRUE
        )
        set(SDL_SHARED OFF CACHE BOOL "" FORCE)
        set(SDL_STATIC ON CACHE BOOL "" FORCE)
        set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
        set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
        FetchContent_MakeAvailable(SDL3)
        if(NOT TARGET SDL3::SDL3)
            if(TARGET SDL3-static)
                add_library(SDL3::SDL3 ALIAS SDL3-static)
            elseif(TARGET SDL3-shared)
                add_library(SDL3::SDL3 ALIAS SDL3-shared)
            endif()
        endif()
    endif()

    pkg_check_modules(FONTCONFIG REQUIRED fontconfig)

    set(ARECA_IMGUI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/settings/imgui")
    if(NOT EXISTS "${ARECA_IMGUI_DIR}/imgui.h")
        message(FATAL_ERROR "Dear ImGui source was not found at settings/imgui.")
    endif()

    add_executable(areca-settings-legacy
        settings/main.cpp
        settings/app_config.cpp
        settings/config_store.cpp
        settings/font_loader.cpp
        settings/fcitx_dbus.cpp
        settings/key_mapper.cpp
        settings/ui_theme.cpp
        settings/ui_views.cpp
        src/bamboo_engine_adapter.cpp
        "${ARECA_IMGUI_DIR}/imgui.cpp"
        "${ARECA_IMGUI_DIR}/imgui_draw.cpp"
        "${ARECA_IMGUI_DIR}/imgui_tables.cpp"
        "${ARECA_IMGUI_DIR}/imgui_widgets.cpp"
        "${ARECA_IMGUI_DIR}/backends/imgui_impl_sdl3.cpp"
        "${ARECA_IMGUI_DIR}/backends/imgui_impl_sdlrenderer3.cpp")
    target_compile_features(areca-settings-legacy PRIVATE cxx_std_20)
    target_compile_options(areca-settings-legacy PRIVATE -Wall -Wextra -Wpedantic)
    target_include_directories(areca-settings-legacy PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${ARECA_IMGUI_DIR}"
        "${ARECA_IMGUI_DIR}/backends"
        ${DBUS_INCLUDE_DIRS})
    target_link_libraries(areca-settings-legacy PRIVATE
        areca-bamboo
        Fcitx5::Config
        Fcitx5::Utils
        SDL3::SDL3
        ${DBUS_LIBRARIES}
        ${FONTCONFIG_LIBRARIES}
        pthread dl m)
    target_include_directories(areca-settings-legacy PRIVATE ${FONTCONFIG_INCLUDE_DIRS})
endif()

configure_file(data/areca-addon.conf.in areca-addon.conf @ONLY)
configure_file(data/areca.conf.in areca-inputmethod.conf @ONLY)

install(TARGETS areca DESTINATION "${CMAKE_INSTALL_LIBDIR}/fcitx5")
install(FILES gtk-settings/areca_settings.py gtk-settings/icon_library.py
        DESTINATION "${CMAKE_INSTALL_DATADIR}/areca/gtk-settings")
install(DIRECTORY gtk-settings/assets
        DESTINATION "${CMAKE_INSTALL_DATADIR}/areca/gtk-settings")
install(FILES "icons/hicolor/scalable/apps/areca-settings.svg"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/areca/icons/hicolor/scalable/apps")
configure_file(data/areca-settings-launcher.in areca-settings-launcher @ONLY)
install(PROGRAMS "${CMAKE_CURRENT_BINARY_DIR}/areca-settings-launcher"
        DESTINATION "${CMAKE_INSTALL_BINDIR}" RENAME areca-settings)
install(FILES "data/areca-settings.desktop"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/applications"
        RENAME "io.github.areca.Settings.desktop")
install(FILES "data/areca-settings-legacy.desktop"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/applications"
        RENAME "areca-settings.desktop")
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/areca-addon.conf"
        RENAME areca.conf DESTINATION "${CMAKE_INSTALL_DATADIR}/fcitx5/addon")
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/areca-inputmethod.conf"
        RENAME areca.conf DESTINATION "${CMAKE_INSTALL_DATADIR}/fcitx5/inputmethod")

set(ARECA_SCALABLE_ICON
    "${CMAKE_CURRENT_SOURCE_DIR}/icons/hicolor/scalable/apps/fcitx-areca.svg")
foreach(ARECA_ICON_NAME
        fcitx-areca
        areca
        org.fcitx.Fcitx5.fcitx-areca)
    install(FILES "${ARECA_SCALABLE_ICON}"
            DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps"
            RENAME "${ARECA_ICON_NAME}.svg")
endforeach()

# Icon riêng cho areca-settings — DE dùng tên này theo desktop file.
set(ARECA_SETTINGS_ICON
    "${CMAKE_CURRENT_SOURCE_DIR}/icons/hicolor/scalable/apps/areca-settings.svg")
install(FILES "${ARECA_SETTINGS_ICON}"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps")
# /usr/share/pixmaps: fallback cuối cùng, mọi DE (GTK, Qt, XFCE, LXDE...) đều kiểm tra.
install(FILES "${ARECA_SETTINGS_ICON}"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/pixmaps")

set(ARECA_SYMBOLIC_ICON
    "${CMAKE_CURRENT_SOURCE_DIR}/icons/hicolor/symbolic/apps/fcitx-areca-symbolic.svg")
foreach(ARECA_ICON_NAME
        fcitx-areca-symbolic
        areca-symbolic
        org.fcitx.Fcitx5.fcitx-areca-symbolic)
    install(FILES "${ARECA_SYMBOLIC_ICON}"
            DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/symbolic/apps"
            RENAME "${ARECA_ICON_NAME}.svg")
endforeach()

install(FILES "data/99-uinput-areca.rules"
        DESTINATION "lib/udev/rules.d")

if(BUILD_TESTING)
    add_executable(areca-adaptive-wait-test
        tests/adaptive_wait_test.cpp)
    target_compile_features(areca-adaptive-wait-test PRIVATE cxx_std_20)
    target_compile_options(areca-adaptive-wait-test PRIVATE
        -Wall -Wextra -Wpedantic -UNDEBUG)
    target_include_directories(areca-adaptive-wait-test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src")
    add_test(NAME adaptive-wait COMMAND areca-adaptive-wait-test)

    add_executable(areca-event-loop-post-test
        tests/event_loop_post_test.cpp)
    target_compile_features(areca-event-loop-post-test PRIVATE cxx_std_20)
    target_compile_options(areca-event-loop-post-test PRIVATE
        -Wall -Wextra -Wpedantic -UNDEBUG)
    target_include_directories(areca-event-loop-post-test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(areca-event-loop-post-test PRIVATE Fcitx5::Utils)
    add_test(NAME event-loop-post COMMAND areca-event-loop-post-test)

    add_executable(areca-bamboo-test
        tests/bamboo_engine_adapter_test.cpp
        src/bamboo_engine_adapter.cpp)
    target_compile_features(areca-bamboo-test PRIVATE cxx_std_20)
    target_compile_options(areca-bamboo-test PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(areca-bamboo-test PRIVATE -UNDEBUG)
    target_include_directories(areca-bamboo-test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(areca-bamboo-test PRIVATE
        areca-bamboo Fcitx5::Utils pthread dl m)
    add_test(NAME bamboo-adapter COMMAND areca-bamboo-test)

    add_executable(areca-word-segment-test
        tests/word_segment_test.cpp
        src/word_segment.cpp)
    target_compile_features(areca-word-segment-test PRIVATE cxx_std_20)
    target_compile_options(areca-word-segment-test PRIVATE
        -Wall -Wextra -Wpedantic -UNDEBUG)
    target_include_directories(areca-word-segment-test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(areca-word-segment-test PRIVATE Fcitx5::Utils)
    add_test(NAME word-segment COMMAND areca-word-segment-test)

    add_executable(areca-browser-autocomplete-test
        tests/browser_autocomplete_test.cpp
        src/browser_autocomplete.cpp
        src/program_compatibility.cpp)
    target_compile_features(areca-browser-autocomplete-test PRIVATE cxx_std_20)
    target_compile_options(areca-browser-autocomplete-test PRIVATE
        -Wall -Wextra -Wpedantic -UNDEBUG)
    target_include_directories(areca-browser-autocomplete-test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(areca-browser-autocomplete-test PRIVATE Fcitx5::Utils)
    add_test(NAME browser-autocomplete COMMAND areca-browser-autocomplete-test)

    add_executable(areca-sentence-capitalizer-test
        tests/sentence_capitalizer_test.cpp
        src/sentence_capitalizer.cpp)
    target_compile_features(areca-sentence-capitalizer-test PRIVATE cxx_std_20)
    target_compile_options(areca-sentence-capitalizer-test PRIVATE
        -Wall -Wextra -Wpedantic -UNDEBUG)
    target_include_directories(areca-sentence-capitalizer-test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(areca-sentence-capitalizer-test PRIVATE Fcitx5::Utils)
    add_test(NAME sentence-capitalizer COMMAND areca-sentence-capitalizer-test)

    add_executable(areca-preedit-logic-test
        tests/preedit_logic_test.cpp
        src/preedit_logic.cpp)
    target_compile_features(areca-preedit-logic-test PRIVATE cxx_std_20)
    target_compile_options(areca-preedit-logic-test PRIVATE
        -Wall -Wextra -Wpedantic -UNDEBUG)
    target_include_directories(areca-preedit-logic-test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(areca-preedit-logic-test PRIVATE Fcitx5::Utils)
    add_test(NAME preedit-logic COMMAND areca-preedit-logic-test)

    add_executable(areca-program-compatibility-test
        tests/program_compatibility_test.cpp
        src/program_compatibility.cpp)
    target_compile_features(areca-program-compatibility-test PRIVATE cxx_std_20)
    target_compile_options(areca-program-compatibility-test PRIVATE
        -Wall -Wextra -Wpedantic -UNDEBUG)
    target_include_directories(areca-program-compatibility-test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src")
    add_test(NAME program-compatibility COMMAND areca-program-compatibility-test)

    add_executable(areca-app-mode-routing-test
        tests/app_mode_routing_test.cpp)
    target_compile_features(areca-app-mode-routing-test PRIVATE cxx_std_20)
    target_compile_options(areca-app-mode-routing-test PRIVATE
        -Wall -Wextra -Wpedantic -UNDEBUG)
    target_include_directories(areca-app-mode-routing-test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(areca-app-mode-routing-test PRIVATE
        Fcitx5::Config Fcitx5::Utils)
    add_test(NAME app-mode-routing COMMAND areca-app-mode-routing-test)

    add_executable(areca-forward-backspace-probe-test
        tests/forward_backspace_probe_test.cpp
        src/forward_backspace_backend.cpp)
    target_compile_features(areca-forward-backspace-probe-test PRIVATE cxx_std_20)
    target_compile_options(areca-forward-backspace-probe-test PRIVATE
        -Wall -Wextra -Wpedantic -UNDEBUG)
    target_include_directories(areca-forward-backspace-probe-test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(areca-forward-backspace-probe-test PRIVATE
        Fcitx5::Core Fcitx5::Utils)
    add_test(NAME forward-backspace-probe COMMAND areca-forward-backspace-probe-test)

    add_executable(areca-uinput-backspace-test
        tests/uinput_backspace_backend_test.cpp
        src/uinput_backspace_backend.cpp
        src/uinput_device.cpp
        src/program_compatibility.cpp)
    target_compile_features(areca-uinput-backspace-test PRIVATE cxx_std_20)
    target_compile_options(areca-uinput-backspace-test PRIVATE
        -Wall -Wextra -Wpedantic -UNDEBUG)
    target_include_directories(areca-uinput-backspace-test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(areca-uinput-backspace-test PRIVATE Fcitx5::Core Fcitx5::Utils)
    add_test(NAME uinput-backspace COMMAND areca-uinput-backspace-test)

    add_executable(areca-uinput-shift-select-test
        tests/uinput_shift_select_backend_test.cpp
        src/uinput_shift_select_backend.cpp
        src/uinput_device.cpp
        src/program_compatibility.cpp)
    target_compile_features(areca-uinput-shift-select-test PRIVATE cxx_std_20)
    target_compile_options(areca-uinput-shift-select-test PRIVATE
        -Wall -Wextra -Wpedantic -UNDEBUG)
    target_include_directories(areca-uinput-shift-select-test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(areca-uinput-shift-select-test PRIVATE Fcitx5::Core Fcitx5::Utils)
    add_test(NAME uinput-shift-select COMMAND areca-uinput-shift-select-test)

    add_executable(areca-native-backspace-test
        tests/native_backspace_backend_test.cpp
        src/native_backspace_backend.cpp)
    target_compile_features(areca-native-backspace-test PRIVATE cxx_std_20)
    target_compile_options(areca-native-backspace-test PRIVATE
        -Wall -Wextra -Wpedantic -UNDEBUG)
    target_include_directories(areca-native-backspace-test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(areca-native-backspace-test PRIVATE
        Fcitx5::Core Fcitx5::Utils)
    add_test(NAME native-backspace COMMAND areca-native-backspace-test)

    add_executable(areca-virtual-keyboard-backspace-test
        tests/virtual_keyboard_backspace_backend_test.cpp
        src/virtual_keyboard_backspace_backend.cpp
        src/virtual_keyboard_device.cpp
        src/virtual_keyboard_keymap.cpp)
    target_compile_features(areca-virtual-keyboard-backspace-test PRIVATE cxx_std_20)
    target_compile_options(areca-virtual-keyboard-backspace-test PRIVATE
        -Wall -Wextra -Wpedantic -UNDEBUG)
    target_include_directories(areca-virtual-keyboard-backspace-test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(areca-virtual-keyboard-backspace-test PRIVATE
        Fcitx5::Core Fcitx5::Utils)
    add_test(NAME virtual-keyboard-backspace COMMAND areca-virtual-keyboard-backspace-test)

    add_executable(areca-native-shift-select-test
        tests/native_shift_select_backend_test.cpp
        src/native_shift_select_backend.cpp)
    target_compile_features(areca-native-shift-select-test PRIVATE cxx_std_20)
    target_compile_options(areca-native-shift-select-test PRIVATE
        -Wall -Wextra -Wpedantic -UNDEBUG)
    target_include_directories(areca-native-shift-select-test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(areca-native-shift-select-test PRIVATE
        Fcitx5::Core Fcitx5::Utils)
    add_test(NAME native-shift-select COMMAND areca-native-shift-select-test)

    add_executable(areca-surrounding-text-v2-test
        tests/surrounding_text_v2_backend_test.cpp
        src/surrounding_text_v2_backend.cpp
        src/surrounding_text_cache.cpp)
    target_compile_features(areca-surrounding-text-v2-test PRIVATE cxx_std_20)
    target_compile_options(areca-surrounding-text-v2-test PRIVATE
        -Wall -Wextra -Wpedantic -UNDEBUG)
    target_include_directories(areca-surrounding-text-v2-test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(areca-surrounding-text-v2-test PRIVATE Fcitx5::Core Fcitx5::Utils)
    add_test(NAME surrounding-text-v2 COMMAND areca-surrounding-text-v2-test)

    add_executable(areca-surrounding-text-probe-test
        tests/surrounding_text_probe_test.cpp
        src/surrounding_text_backend.cpp
        src/surrounding_text_cache.cpp)
    target_compile_features(areca-surrounding-text-probe-test PRIVATE cxx_std_20)
    target_compile_options(areca-surrounding-text-probe-test PRIVATE
        -Wall -Wextra -Wpedantic -UNDEBUG)
    target_include_directories(areca-surrounding-text-probe-test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(areca-surrounding-text-probe-test PRIVATE Fcitx5::Core Fcitx5::Utils)
    add_test(NAME surrounding-text-probe COMMAND areca-surrounding-text-probe-test)

    add_executable(areca-surrounding-text-restore-test
        tests/surrounding_text_restore_test.cpp
        src/surrounding_text_restore.cpp)
    target_compile_features(areca-surrounding-text-restore-test PRIVATE cxx_std_20)
    target_compile_options(areca-surrounding-text-restore-test PRIVATE
        -Wall -Wextra -Wpedantic -UNDEBUG)
    target_include_directories(areca-surrounding-text-restore-test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(areca-surrounding-text-restore-test PRIVATE
        Fcitx5::Config Fcitx5::Utils)
    add_test(NAME surrounding-text-restore
        COMMAND areca-surrounding-text-restore-test)

    add_executable(areca-input-type-detector-test
        tests/input_type_detector_test.cpp
        src/browser_autocomplete.cpp
        src/input_type_detector.cpp
        src/program_compatibility.cpp
        src/atspi/atspi_connection.cpp
        src/atspi/atspi_accessible.cpp
        src/window_focus_tracker.cpp)
    target_compile_features(areca-input-type-detector-test PRIVATE cxx_std_20)
    target_compile_options(areca-input-type-detector-test PRIVATE
        -Wall -Wextra -Wpedantic -UNDEBUG)
    target_include_directories(areca-input-type-detector-test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src" ${DBUS_INCLUDE_DIRS})
    target_link_libraries(areca-input-type-detector-test PRIVATE
        Fcitx5::Core Fcitx5::Utils ${DBUS_LIBRARIES} pthread dl m)
    add_test(NAME input-type-detector COMMAND areca-input-type-detector-test)
endif()

if(BUILD_TESTING)
    add_executable(areca-mouse-test-helper tests/mouse_click_tracker_helper.cpp)
    add_executable(areca-mouse-click-tracker-test
        tests/mouse_click_tracker_test.cpp src/mouse_click_tracker.cpp)
    target_compile_features(areca-mouse-click-tracker-test PRIVATE cxx_std_20)
    target_compile_options(areca-mouse-click-tracker-test PRIVATE
        -Wall -Wextra -Wpedantic -UNDEBUG)
    target_include_directories(areca-mouse-click-tracker-test PRIVATE src)
    target_compile_definitions(areca-mouse-click-tracker-test PRIVATE
        ARECA_MOUSE_HELPER_PATH="$<TARGET_FILE:areca-mouse-test-helper>")
    target_link_libraries(areca-mouse-click-tracker-test PRIVATE Fcitx5::Utils)
    add_dependencies(areca-mouse-click-tracker-test areca-mouse-test-helper)
    add_test(NAME mouse-click-tracker COMMAND areca-mouse-click-tracker-test)
endif()

if(BUILD_TESTING)
    add_executable(areca-idle-key-forward-test
        tests/idle_key_forward_test.cpp src/input_scheduler.cpp
        src/surrounding_text_cache.cpp)
    target_compile_features(areca-idle-key-forward-test PRIVATE cxx_std_20)
    target_compile_options(areca-idle-key-forward-test PRIVATE
        -Wall -Wextra -Wpedantic -UNDEBUG)
    target_include_directories(areca-idle-key-forward-test PRIVATE src)
    target_link_libraries(areca-idle-key-forward-test PRIVATE Fcitx5::Core Fcitx5::Utils)
    add_test(NAME idle-key-forward COMMAND areca-idle-key-forward-test)
endif()

if(BUILD_TESTING)
    add_executable(areca-preedit-surrounding-restore-test
        tests/preedit_surrounding_restore_test.cpp src/preedit_mode.cpp
        src/preedit_logic.cpp src/bamboo_engine_adapter.cpp
        src/sentence_capitalizer.cpp src/surrounding_text_cache.cpp
        src/surrounding_text_restore.cpp)
    target_compile_features(areca-preedit-surrounding-restore-test PRIVATE cxx_std_20)
    target_compile_options(areca-preedit-surrounding-restore-test PRIVATE
        -Wall -Wextra -Wpedantic -UNDEBUG)
    target_include_directories(areca-preedit-surrounding-restore-test PRIVATE src)
    target_link_libraries(areca-preedit-surrounding-restore-test PRIVATE
        areca-bamboo Fcitx5::Core Fcitx5::Utils pthread dl m)
    add_test(NAME preedit-surrounding-restore
        COMMAND areca-preedit-surrounding-restore-test)

    add_executable(areca-rewrite-feature-toggle-test
        tests/rewrite_feature_toggle_test.cpp src/rewrite_mode.cpp
        src/input_scheduler.cpp src/surrounding_text_cache.cpp
        src/bamboo_engine_adapter.cpp src/sentence_capitalizer.cpp
        src/surrounding_text_restore.cpp)
    target_compile_features(areca-rewrite-feature-toggle-test PRIVATE cxx_std_20)
    target_compile_options(areca-rewrite-feature-toggle-test PRIVATE
        -Wall -Wextra -Wpedantic -UNDEBUG)
    target_include_directories(areca-rewrite-feature-toggle-test PRIVATE src)
    target_link_libraries(areca-rewrite-feature-toggle-test PRIVATE
        areca-bamboo Fcitx5::Core Fcitx5::Utils pthread dl m)
    add_test(NAME rewrite-feature-toggle COMMAND areca-rewrite-feature-toggle-test)
endif()
