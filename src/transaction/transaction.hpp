#pragma once

#include "core/database.hpp"

#include <boost/circular_buffer.hpp>
#include <function2/function2.hpp>
#include <mongoc/mongoc.h>
#include <tao/tuple/tuple.hpp>
#include <inplace_function.h>

#include <atomic>
#include <optional>
#include <set>


enum class op_type
{
    insert,
    delete_one,
    delete_many,
    update_one,
    update_many,
    upsert_one,
    upsert_many
};

template <uint32_t callable_size>
class transaction
{
    using callable_t = stdext::inplace_function<void(mongoc_collection_t*), callable_size>;

    struct transaction_info
    {
        struct dependency
        {
            uint64_t owner;
            uint64_t id;
        };

        std::optional<struct dependency> dependency;
        op_type type;
        bson_t operation_op_1;
        bson_t operation_op_2;
        std::optional<callable_t> callable;
        bool pending;
        bool done;
    };

    struct collection_info
    {
        collection_info();

        uint64_t first_id;
        std::atomic<uint64_t> current_id;
        std::unordered_map<uint64_t, transaction_info> transactions;
    };

public:
    using store_t = store<transaction, entity<transaction>>;

public:
    transaction() noexcept;

    transaction(transaction&& other) noexcept
        _collections(std::move(other._collections)),
        _execute_every(other._execute_every),
        _since_last_execution(other._since_last_execution),
        _pending_callables(static_cast<uint8_t>(other._pending_callables)),
        _flagged(other._flagged),
        _scheduled(other._scheduled)
    {}

    transaction& operator=(transaction&& other) noexcept
    {
        _collections = std::move(other._collections);
        _execute_every = other._execute_every;
        _since_last_execution = other._since_last_execution;
        _pending_callables = static_cast<uint8_t>(other._pending_callables);
        _flagged = other._flagged;
        _scheduled = other._scheduled;

        return *this;
    }

    void construct(uint64_t execute_every);
    bool update(uint64_t diff, store_t* store, database* database);

    uint64_t push_operation(uint8_t collection, op_type type, bson_t& operation);
    uint64_t push_operation(uint8_t collection, op_type type, bson_t& operation_1, bson_t& operation_2);
    uint64_t push_callable(uint8_t collection, callable_t&& callable);
    void push_dependency(uint8_t collection, uint64_t owner, uint64_t id);

    inline void flag_deletion();
    inline void unflag_deletion();

private:
    std::vector<transaction_info*> get_pending_operations(uint8_t collection, store_t* store, bool& has_non_callable_transactions);
    transaction_info* get_transaction(uint8_t collection, uint64_t id);

private:
    std::unordered_map<uint8_t, collection_info*> _collections;
    uint64_t _execute_every;
    uint64_t _since_last_execution;
    std::atomic<uint8_t> _pending_callables;
    bool _flagged;
    bool _scheduled;
};


template <uint32_t callable_size>
bool transaction<callable_size>::update(uint64_t diff, store_t* store, database* database)
{
    if (_flagged)
    {
        if (_scheduled)
        {
            // Delete already
            return false;
        }

        bool can_delete = true;

        // We will only delete if all transactions are done
        for (auto& [collection, info] : _collections)
        {
            if (info->first_id != info->current_id)
            {
                can_delete = false;
                break;
            }

            if (auto it = info->transactions.find(info->current_id - 1); it != info->transactions.end() && !it->second.done)
            {
                can_delete = false;
                break;
            }
        }

        if (can_delete)
        {
            // Return false to be deleted
            _scheduled = true;
            return false;
        }
    }

    // We have to execute if
    //  a) Too much time has elapsed
    //  b) There are pending callables
    _since_last_execution += diff;
    if (_pending_callables == 0 && _since_last_execution < _execute_every)
    {
        // Nothing to do here
        return true;
    }
    _since_last_execution = 0;

    // Transactions are pending when ids don't match
    for (auto& [collection, info] : _collections)
    {
        if (info->first_id == info->current_id)
        {
            continue;
        }
    
        // TODO(gpascualg): Pending operations should decrease _pending_callables where appropiate
        //  _pending_callables -= count
        bool has_non_callable_transactions;
        std::vector<transaction_info*> transactions = get_pending_operations(collection, store, has_non_callable_transactions);
        auto final_id = info->first_id;
        
        if (transactions.empty())
        {
            break;
        }

        // Everything is write ops
        if (has_non_callable_transactions)
        {
            database->execute([
                collection = collection,
                transactions = std::move(transactions)
            ](auto mongo_database)
            {
                auto col = database->get_collection(mongo_database, collection);
                auto bulk = mongoc_collection_create_bulk_operation_with_opts(col, NULL);

                for (auto t : transactions)
                {
                    switch (t->type)
                    {
                        case op_type::insert:
                            mongoc_bulk_operation_insert(bulk, &t->operation_op_1);
                            break;

                        case op_type::update_one:
                            mongoc_bulk_operation_update_one_with_opts(bulk, &t->operation_op_1, &t->operation_op_2, nullptr, nullptr);
                            break;

                        case op_type::update_many:
                            mongoc_bulk_operation_update_many_with_opts(bulk, &t->operation_op_1, &t->operation_op_2, nullptr, nullptr);
                            break;

                        case op_type::upsert_one:
                        case op_type::upsert_many:
                            // TODO(gpascualg): Use mongoc_bulk_operation_update_many_with_opts specifying opts
                            mongoc_bulk_operation_update(bulk, &t->operation_op_1, &t->operation_op_2, true);
                            break;

                        case op_type::delete_one:
                        case op_type::delete_many:
                            mongoc_bulk_operation_delete(bulk, &t->operation_op_1);
                            break;

                        default:
                            break;
                    }

                    // Destroy data, it's already in the bulk
                    bson_destroy(&t->operation_op_1);
                    bson_destroy(&t->operation_op_2);
                }

                // Send transactions
                bson_error_t error;
                bson_t reply;
                bool ret = mongoc_bulk_operation_execute(bulk, &reply, &error);
                // TODO(gpascualg): Handle bulk error
                bson_destroy(&reply);
                (void)ret;

                // Once we get here, they are all executed, so flag them and destroy bulk
                mongoc_bulk_operation_destroy(bulk);
                for (auto t : transactions)
                {
                    // Flag
                    t->done = true;
                    t->pending = false;
                }
            });
        }
        // Everything is callable ops
        else
        {
            database->execute([
                collection = collection,
                transactions = std::move(transactions)
            ]()
            {
                auto col = database::instance->get_collection(collection);

                for (auto t : transactions)
                {
                    (*t->callable)(col);
                    t->done = true;
                    t->pending = false;
                }
            });
        }
    }
}

template <uint32_t callable_size>
inline void transaction<callable_size>::flag_deletion()
{
    _flagged = true;
}

template <uint32_t callable_size>
inline void transaction<callable_size>::unflag_deletion()
{
    _flagged = false;
    _scheduled = false;
}
