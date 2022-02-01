find_package (Threads REQUIRED)

set(CORE_SOURCES 
    common/result_of.hpp
    common/tao.hpp
    common/types.hpp
    concepts/constructable.hpp
    concepts/destroyable.hpp
    concepts/entity_destroyable.hpp
    concepts/has_scheme_created.hpp
    concepts/has_scheme_information.hpp
    entity/components_map.hpp
    entity/component.hpp
    entity/scheme.hpp
    ids/generator.hpp
    io/memmap.hpp
    io/memmap.cpp
    pools/plain_pool.hpp
    pools/singleton_pool.hpp
    pools/thread_local_pool.hpp
    storage/growable_storage.hpp
    storage/partitioned_growable_storage.hpp
    storage/partitioned_static_storage.hpp
    storage/pool_item.hpp
    storage/static_growable_storage.hpp
    storage/static_storage.hpp
    storage/storage.hpp
    storage/ticket.hpp
    traits/base_dic.hpp
    traits/contains.hpp
    traits/ctti.hpp
    traits/has_type.hpp
    traits/shared_function.hpp
    traits/tuple.hpp
    traits/without_duplicates.hpp
    updater/core.hpp
    updater/tasks_manager.hpp
    updater/variant_tasks_manager.hpp
    view/scheme_view.hpp
    view/partial_scheme_view.hpp)

add_library(sekkeizu STATIC ${CORE_SOURCES})

target_link_libraries(sekkeizu PUBLIC Threads::Threads)
target_compile_features(sekkeizu PUBLIC cxx_std_20)
target_include_directories(sekkeizu PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    target_compile_options(sekkeizu PUBLIC -stdlib=libc++)
    target_link_options(sekkeizu INTERFACE -stdlib=libc++ -lc++abi -lc++)
elseif (CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    target_compile_definitions(sekkeizu PUBLIC _UNICODE UNICODE)
    target_compile_options(sekkeizu PUBLIC /MP /Zc:__cplusplus)
endif()

# EXECUTABLE
add_executable(sekkeizu_test main.cpp)
target_compile_features(sekkeizu_test PUBLIC cxx_std_20)
target_link_libraries(sekkeizu_test PRIVATE sekkeizu)
