#pragma once

#include <pool/fiber_pool.hpp>

#include <mongoc/mongoc.h>
#include <mongocxx/bulk_write.hpp>
#include <mongocxx/pool.hpp>
#include <mongocxx/client.hpp>
#include <mongocxx/instance.hpp>
#include <mongocxx/uri.hpp>
#include <bsoncxx/builder/stream/document.hpp>
#include <bsoncxx/json.hpp>
#include <bsoncxx/oid.hpp>

#include <set>
#include <optional>
#include <unordered_map>


template <typename pool_traits>
class database
{
public:
    database(np::fiber_pool<pool_traits>* fiber_pool) noexcept;

    void init(const mongocxx::uri& uri, std::string database) noexcept;

    template <typename F>
    void query(F&& function) noexcept;
    
    mongoc_database_t* database::get_database() noexcept;
    mongoc_collection_t* database::get_collection(uint8_t collection) noexcept;

private:
    // Execution pool
    np::fiber_pool<pool_traits>* _fiber_pool;
    
    // Database parameters
    mongoc_uri_t* _uri;
    mongoc_client_pool_t* _pool;
    std::string _database;
    bool _is_connected;
    std::unordered_map<uint8_t, std::string> _collections_map;
};

template <typename pool_traits>
database<pool_traits>::database(np::fiber_pool<pool_traits>* fiber_pool) noexcept
{}


template <typename pool_traits>
void database<pool_traits>::init(const mongocxx::uri& uri, std::string database) noexcept
{
    // Init db
    mongoc_init();

    // Check uri
    bson_error_t error;
    _uri = mongoc_uri_new_with_error(uri.to_string().c_str(), &error);
    if (!_uri) 
    {
        _is_connected = false;
        return;
    }

    // Setup pool
    _pool = mongoc_client_pool_new(_uri);
    mongoc_client_pool_set_error_api(_pool, 2);

    // Check connection
    auto client = mongoc_client_pool_pop(_pool);

    bson_t command = BSON_INITIALIZER;
    BSON_APPEND_INT32(&command, "ping", 1);

    bson_t reply;
    _is_connected = mongoc_client_command_simple(client, "admin", &command, NULL, &reply, &error);
    bson_destroy(&reply);
    mongoc_client_pool_push(_pool, client);

    // Done
    _database = database;
}

template <typename pool_traits>
template <typename F>
void database<pool_traits>::query(F&& function) noexcept
{
    _fiber_pool->push([this, function = std::forward<F>(function)]() {

    });
}

template <typename pool_traits>
mongoc_database_t* database<pool_traits>::get_database()
{
    // TODO(gpascualg): We re never pushing this back, do we care?
    thread_local auto client = mongoc_client_pool_pop(_pool);
    thread_local auto database = mongoc_client_get_database(client, _database.c_str());
    return database;
}

template <typename pool_traits>
mongoc_collection_t* database<pool_traits>::get_collection(uint8_t collection)
{
    auto database = get_database();
    return mongoc_database_get_collection(database, _collections_map[collection].c_str());
}