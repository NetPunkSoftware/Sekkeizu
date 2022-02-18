find_package (Threads REQUIRED)

set(CORE_SOURCES 
    core/coreloop.hpp
    core/coreloop_network_plugin.hpp
    core/coreloop_palanteer_tick_time.hpp
    core/coreloop_scheduled_tick.hpp
    core/coreloop_user_tick_plugin.hpp
    core/fixed_string.hpp
    database/bson_reflection_struct.hpp
    database/database.hpp
    database/transaction.hpp
    memory/per_thread_pool.hpp)

add_library(sekkeizu OBJECT ${CORE_SOURCES})

target_link_libraries(sekkeizu PUBLIC Threads::Threads)
target_compile_features(sekkeizu PUBLIC cxx_std_20)
target_include_directories(sekkeizu PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    # target_compile_options(sekkeizu PUBLIC -stdlib=libc++)
    # target_link_options(sekkeizu INTERFACE -stdlib=libc++ -lc++abi -lc++)
elseif (CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    target_compile_definitions(sekkeizu PUBLIC _UNICODE UNICODE NOMINMAX)
    target_compile_options(sekkeizu PUBLIC /MP /Zc:__cplusplus)
endif()

if (WIN32)
    target_link_libraries(sekkeizu PUBLIC Winmm.lib)
endif()

# EXECUTABLE
add_executable(sekkeizu_test main.cpp)
target_compile_features(sekkeizu_test PUBLIC cxx_std_20)
target_link_libraries(sekkeizu_test PRIVATE sekkeizu)
