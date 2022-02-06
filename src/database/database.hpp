#pragma once

#include "core/fixed_string.hpp"

#include <pool/fiber_pool.hpp>
#include <synchronization/mutex.hpp>

#include <mongoc/mongoc.h>

#include <chacha.h>
#include <algparam.h>
#include <argnames.h>
#include <osrng.h>

#include <atomic>
#include <optional>
#include <set>
#include <unordered_map>



template <typename pool_traits>
class database
{
public:
    database() noexcept = default;
    database(np::fiber_pool<pool_traits>* fiber_pool) noexcept;

    void set_fiber_pool(np::fiber_pool<pool_traits>* fiber_pool) noexcept;
    void init(const char* uri, const std::string& database) noexcept;
    void add_collection(uint8_t key, const std::string& collection) noexcept;

    template <typename F>
    void execute(F&& function) noexcept;

    template <fixed_string collection>
    inline void ensure_creation(bson_t* document) noexcept;

    template <fixed_string collection>
    inline void ensure_creation(bson_t& document) noexcept;

    template <fixed_string collection, typename C>
    inline void ensure_creation(bson_t* document, C&& callback) noexcept;

    template <fixed_string collection, typename C>
    inline void ensure_creation(bson_t& document, C&& callback) noexcept;

    template <typename C>
    inline void ensure_creation(uint8_t collection, bson_t* document, C&& callback) noexcept;

    template <typename C>
    inline void ensure_creation(uint8_t collection, bson_t& document, C&& callback) noexcept;

    inline int64_t ensure_creation_unsafe(mongoc_collection_t* collection, bson_t* document) noexcept;

    mongoc_collection_t* get_collection(mongoc_database_t* database, uint8_t collection) noexcept;
    inline const std::unordered_map<uint8_t, std::string>& get_all_collections() const noexcept;

protected:
    template <typename C>
    void ensure_creation_impl(mongoc_collection_t* collection, bson_t* document, C&& callback) noexcept;

    int64_t ensure_creation_impl(mongoc_collection_t* collection, bson_t* document) noexcept;

    int64_t get_potentially_unique_id() noexcept;

private:
    // Execution pool
    np::fiber_pool<pool_traits>* _fiber_pool;
    
    // Database parameters
    mongoc_uri_t* _uri;
    mongoc_client_pool_t* _pool;
    std::string _database;
    bool _is_connected;
    std::unordered_map<uint8_t, std::string> _collections_map;

    // Unique ID generator
    np::mutex _mutex;
    CryptoPP::SecByteBlock _key;
    CryptoPP::SecByteBlock _iv;
    CryptoPP::ChaCha::Encryption _enc;
    std::atomic<uint64_t> _counter;
};


template <typename pool_traits>
database<pool_traits>::database(np::fiber_pool<pool_traits>* fiber_pool) noexcept :
    _fiber_pool(fiber_pool),
    _uri(nullptr),
    _pool(nullptr),
    _database(),
    _is_connected(false),
    _collections_map(),
    _mutex(),
    _key(32),
    _iv(8),
    _enc(),
    _counter()
{}

template <typename pool_traits>
void database<pool_traits>::set_fiber_pool(np::fiber_pool<pool_traits>* fiber_pool) noexcept
{
    _fiber_pool = fiber_pool;
}

template <typename pool_traits>
void database<pool_traits>::init(const char* uri, const std::string& database) noexcept
{
    // Initialize unique id generator
    CryptoPP::AutoSeededRandomPool prng;
    prng.GenerateBlock(_key, _key.size());
    prng.GenerateBlock(_iv, _iv.size());

    const CryptoPP::AlgorithmParameters params = CryptoPP::MakeParameters(CryptoPP::Name::Rounds(), 8)
        (CryptoPP::Name::IV(), CryptoPP::ConstByteArrayParameter(_iv, 8, false));

    _enc.SetKey(_key, _key.size(), params);

    // Init db
    mongoc_init();

    // Check uri
    bson_error_t error;
    _uri = mongoc_uri_new_with_error(uri, &error);
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
void database<pool_traits>::add_collection(uint8_t key, const std::string& collection) noexcept
{
    _collections_map.emplace(key, collection);
}

template <typename pool_traits>
template <typename F>
void database<pool_traits>::execute(F&& function) noexcept
{
    _fiber_pool->push([this, function = std::forward<F>(function)]() mutable {
        auto client = mongoc_client_pool_pop(_pool);
        auto database = mongoc_client_get_database(client, _database.c_str());
        function(database);
        mongoc_client_pool_push(_pool, client);
    });
}

template <typename pool_traits>
template <fixed_string collection>
inline void database<pool_traits>::ensure_creation(bson_t* document) noexcept
{
    ensure_creation<collection>(document, [](int64_t id) {});
}

template <typename pool_traits>
template <fixed_string collection>
inline void database<pool_traits>::ensure_creation(bson_t& document) noexcept
{
    ensure_creation<collection>(document, [](int64_t id) {});
}

template <typename pool_traits>
template <fixed_string collection, typename C>
inline void database<pool_traits>::ensure_creation(bson_t* document, C&& callback) noexcept
{
    execute([this, document, callback = std::forward<C>(callback)](auto database) {
        auto col = mongoc_database_get_collection(database, collection);
        ensure_creation_impl(col, document, std::forward<C>(callback));
    });
}

template <typename pool_traits>
template <fixed_string collection, typename C>
inline void database<pool_traits>::ensure_creation(bson_t& document, C&& callback) noexcept
{
    execute([this, document = bson_copy(&document), callback = std::forward<C>(callback)](auto database) {
        auto col = mongoc_database_get_collection(database, collection);
        ensure_creation_impl(col, document, std::forward<C>(callback));
    });

    bson_destroy(&document);
}

template <typename pool_traits>
template <typename C>
inline void database<pool_traits>::ensure_creation(uint8_t collection, bson_t* document, C&& callback) noexcept
{
    execute([this, collection, document, callback = std::forward<C>(callback)](auto database) {
        auto col = get_collection(database, collection);
        ensure_creation_impl(col, document, std::forward<C>(callback));
    });
}

template <typename pool_traits>
template <typename C>
inline void database<pool_traits>::ensure_creation(uint8_t collection, bson_t& document, C&& callback) noexcept
{
    execute([this, collection, document = bson_copy(&document), callback = std::forward<C>(callback)](auto database) {
        auto col = get_collection(database, collection);
        ensure_creation_impl(col, document, std::forward<C>(callback));
    });

    bson_destroy(&document);
}

template <typename pool_traits>
inline int64_t database<pool_traits>::ensure_creation_unsafe(mongoc_collection_t* collection, bson_t* document) noexcept
{
    return ensure_creation_impl(collection, document);
}

template <typename pool_traits>
template <typename C>
void database<pool_traits>::ensure_creation_impl(mongoc_collection_t* collection, bson_t* document, C&& callback) noexcept
{
    callback(ensure_creation_impl(collection, document));
}

template <typename pool_traits>
int64_t database<pool_traits>::ensure_creation_impl(mongoc_collection_t* collection, bson_t* document) noexcept
{
    bson_error_t error;

    while (true)
    {
        int64_t id = get_potentially_unique_id();
        
        // TODO(gpascualg): Explore if this could be moved outside the loop, and then overwrite _id inside
        bson_t with_id = BSON_INITIALIZER;
        BSON_APPEND_INT64(&with_id, "_id", id);
        bson_concat(&with_id, document);

        if (mongoc_collection_insert_one(collection, &with_id, NULL, NULL, &error))
        {
            bson_destroy(&with_id);
            bson_destroy(document);
            return id;
        }
        
        bson_destroy(&with_id);
    }
}

template <typename pool_traits>
mongoc_collection_t* database<pool_traits>::get_collection(mongoc_database_t* database, uint8_t collection) noexcept
{
    auto it = _collections_map.find(collection);
    assert(it != _collections_map.end());

    return mongoc_database_get_collection(database, it->second.c_str());
}

template <typename pool_traits>
inline const std::unordered_map<uint8_t, std::string>& database<pool_traits>::get_all_collections() const noexcept
{
    return _collections_map;
}

template <typename pool_traits>
int64_t database<pool_traits>::get_potentially_unique_id() noexcept
{
    CryptoPP::byte data[8];
    uint64_t counter = _counter++ + std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    
    // Processing should be done sync
    _mutex.lock();
    _enc.ProcessData(data, (CryptoPP::byte*)&counter, 8);
    _mutex.unlock();

    // Get as int64_t
    return *(int64_t*)data;
}
