cmake_minimum_required(VERSION 3.16)
project(WTCleaner VERSION 2.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Część przenośna (JSON, tłumaczenia, ustawienia) – używana też przez testy.
add_library(wtcore STATIC
    src/Json.cpp
    src/I18n.cpp
    src/Settings.cpp
)
target_include_directories(wtcore PUBLIC src)

if(WIN32)
    enable_language(RC)
    add_executable(WTCleaner WIN32
        src/main.cpp
        src/WinSystem.cpp
        res/app.rc
    )
    set_target_properties(WTCleaner PROPERTIES OUTPUT_NAME "WT Cleaner")
    target_compile_definitions(WTCleaner PRIVATE UNICODE _UNICODE NOMINMAX)
    target_link_libraries(WTCleaner PRIVATE wtcore comctl32 shell32 ole32 uuid advapi32)

    if(MSVC)
        target_compile_options(wtcore PRIVATE /utf-8 /W4)
        target_compile_options(WTCleaner PRIVATE /utf-8 /W4)
        # Manifest jest w app.rc – wyłącz automatyczny, żeby się nie dublował.
        target_link_options(WTCleaner PRIVATE /MANIFEST:NO)
        # Statyczny runtime: jeden .exe bez wymagania VC++ Redistributable.
        set_property(TARGET wtcore WTCleaner PROPERTY
            MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    elseif(MINGW)
        target_compile_options(WTCleaner PRIVATE -municode -Wall)
        target_link_options(WTCleaner PRIVATE -municode -static -static-libgcc -static-libstdc++)
    endif()

    # Skopiuj pliki tłumaczeń obok .exe.
    add_custom_command(TARGET WTCleaner POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                ${CMAKE_CURRENT_SOURCE_DIR}/lang $<TARGET_FILE_DIR:WTCleaner>/lang)
endif()

# Testy części przenośnej (działają na każdym systemie).
option(WTCLEANER_TESTS "Build unit tests" ON)
if(WTCLEANER_TESTS)
    enable_testing()
    add_executable(wtcore_tests tests/core_tests.cpp)
    target_link_libraries(wtcore_tests PRIVATE wtcore)
    target_compile_definitions(wtcore_tests PRIVATE
        LANG_DIR="${CMAKE_CURRENT_SOURCE_DIR}/lang")
    if(MSVC)
        target_compile_options(wtcore_tests PRIVATE /utf-8)
        set_property(TARGET wtcore_tests PROPERTY
            MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    endif()
    add_test(NAME wtcore_tests COMMAND wtcore_tests)
endif()
