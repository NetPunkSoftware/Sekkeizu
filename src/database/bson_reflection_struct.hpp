#pragma once

#include "traits/string_literal.hpp"
#include "traits/is_specialization.hpp"

#include <mongoc/mongoc.h>

#include <inttypes.h>
#include <type_traits>


constexpr uint32_t hash_of_c_string(const char* str)
{
    uint32_t hash = 2166136261;

    while (char c = *str++)
    {
        hash = hash ^ c;
        hash = hash * 16777619;
    }

    return hash;
}

template <typename T>
concept is_bson_reflection_impl = requires (T v, bson_iter_t* i)
{
    { v.visit_all(i) };
};

template <typename T, StringLiteral... MemberNames>
struct bson_reflection_struct
{
    template <typename... MemberTypes>
    struct impl
    {
        constexpr impl(std::add_lvalue_reference_t<MemberTypes>... refs) :
            _refs(refs...),
            _visitor {
                .visit_double = [](const bson_iter_t* iter, const char* key, double v_double, void* data)
                    {
                        return reinterpret_cast<T*>(data)->visit_double(iter, key, v_double, data);
                    },
                .visit_utf8 = [](const bson_iter_t* iter, const char* key, size_t v_utf8_len, const char* v_utf8, void* data)
                    {
                        return reinterpret_cast<T*>(data)->visit_utf8(iter, key, v_utf8_len, v_utf8, data);
                    },
                 .visit_document = [](const bson_iter_t* iter, const char* key, const bson_t* v_document, void* data)
                    {
                        return reinterpret_cast<T*>(data)->visit_document(iter, key, v_document, data);
                    },
                .visit_bool = [](const bson_iter_t* iter, const char* key, bool v_bool, void* data)
                    {
                        return reinterpret_cast<T*>(data)->visit_bool(iter, key, v_bool, data);
                    },
                .visit_int32 = [](const bson_iter_t* iter, const char* key, int32_t v_int32, void* data)
                    {
                        return reinterpret_cast<T*>(data)->visit_int32(iter, key, v_int32, data);
                    },
                .visit_int64 = [](const bson_iter_t* iter, const char* key, int64_t v_int64, void* data)
                    {
                        return reinterpret_cast<T*>(data)->visit_int64(iter, key, v_int64, data);
                    }
            }
        {}

        template <std::size_t I, typename V>
        constexpr inline bool check_and_store(uint32_t hash, void* data)
        {
            using type = std::decay_t<std::tuple_element_t<I, std::tuple<MemberTypes...>>>;
            if constexpr (std::is_convertible_v<type, V> || std::is_same_v<type, V>)
            {
                if (std::get<I>(hashes) == hash)
                {
                    std::get<I>(_refs) = *reinterpret_cast<V*>(data);
                    return true;
                }
            }
            else
            {
                assert(std::get<I>(hashes) != hash && "Matching key is not the same type");
            }

            if constexpr (I + 1 < sizeof...(MemberTypes))
            {
                return check_and_store<I + 1, V>(hash, data);
            }
            else
            {
                // Element not in struct, continue
                return true;
            }
        }

        template <std::size_t I>
        constexpr inline bool check_and_store_string(uint32_t hash, size_t len, const char* data)
        {
            using type = std::decay_t<std::tuple_element_t<I, std::tuple<MemberTypes...>>>;
            if constexpr (std::is_same_v<type, std::string>)
            {
                if (std::get<I>(hashes) == hash)
                {
                    std::get<I>(_refs) = std::string(data, len);
                    return true;
                }
            }
            else
            {
                assert(std::get<I>(hashes) != hash && "Matching key is not a string");
            }

            if constexpr (I + 1 < sizeof...(MemberTypes))
            {
                return check_and_store_string<I + 1>(hash, len, data);
            }
            else
            {
                // Element not in struct, continue
                return true;
            }
        }

        template <std::size_t I>
        constexpr inline bool check_and_recurse(uint32_t hash, const bson_t* doc)
        {
            using type = std::decay_t<std::tuple_element_t<I, std::tuple<MemberTypes...>>>;
            if constexpr (is_bson_reflection_impl<type>)
            {
                bson_iter_t iter;
                if (bson_iter_init(&iter, doc))
                {
                    std::get<I>(_refs).visit_all(&iter);
                }
                return true;
            }
            else
            {
                assert(std::get<I>(hashes) != hash && "Matching key is not a document");
            }

            if constexpr (I + 1 < sizeof...(MemberTypes))
            {
                return check_and_recurse<I + 1>(hash, doc);
            }
            else
            {
                // Element not in struct, continue
                return true;
            }
        }

        bool visit_double(const bson_iter_t* iter, const char* key, double v_double, void* data)
        {
            auto hash = hash_of_c_string(key);
            return !check_and_store<0, double>(hash, reinterpret_cast<void*>(&v_double));
        }

        bool visit_utf8(const bson_iter_t* iter, const char* key, size_t v_utf8_len, const char* v_utf8, void* data)
        {
            auto hash = hash_of_c_string(key);
            return !check_and_store_string<0>(hash, v_utf8_len, v_utf8);
        }

        bool visit_document(const bson_iter_t* iter, const char* key, const bson_t* v_document, void* data)
        {
            auto hash = hash_of_c_string(key);
            return !check_and_recurse<0>(hash, v_document);
        }

        bool visit_bool(const bson_iter_t* iter, const char* key, bool v_bool, void* data)
        {
            auto hash = hash_of_c_string(key);
            return !check_and_store<0, bool>(hash, reinterpret_cast<void*>(&v_bool));
        }

        bool visit_int32(const bson_iter_t* iter, const char* key, int32_t v_int32, void* data)
        {
            auto hash = hash_of_c_string(key);
            return !check_and_store<0, int32_t>(hash, reinterpret_cast<void*>(&v_int32));
        }

        bool visit_int64(const bson_iter_t* iter, const char* key, int64_t v_int64, void* data)
        {
            auto hash = hash_of_c_string(key);
            return !check_and_store<0, int64_t>(hash, reinterpret_cast<void*>(&v_int64));
        }

        inline bson_visitor_t* visitor()
        {
            return &_visitor;
        }

        inline void visit_all(bson_iter_t* iter)
        {
            bson_iter_visit_all(iter, visitor(), this);
        }

        inline bool visit_all(const bson_t* doc)
        {
            bson_iter_t iter;
            if (bson_iter_init(&iter, doc))
            {
                visit_all(&iter);
                return true;
            }
            return false;
        }

        template <std::size_t I>
        inline void fields_impl(bson_t* doc)
        {
            BSON_APPEND_INT32(doc, std::get<I>(names).value, 1);

            if constexpr (I + 1 < sizeof...(MemberTypes))
            {
                fields_impl<I + 1>(doc);
            }
        }

        inline bson_t* fields()
        {
            if (!_fields_init)
            {
                _fields_init = true;
                fields_impl<0>(&_fields);
            }
            return &_fields;
        }

        template <std::size_t I>
        inline void serialize_impl(bson_t* doc)
        {
            using type = std::decay_t<std::tuple_element<I, std::tuple<MemberTypes...>>>;
            if constexpr (std::is_same_v<type, double>)
            {
                BSON_APPEND_DOUBLE(doc, std::get<I>(names).value, std::get<I>(_refs));
            }
            else if constexpr (std::is_same_v<type, std::string>)
            {
                BSON_APPEND_UTF8(doc, std::get<I>(names).value, std::get<I>(_refs).c_str());
            }
            else if constexpr (std::is_same_v<type, bool>)
            {
                BSON_APPEND_BOOL(doc, std::get<I>(names).value, std::get<I>(_refs));
            }
            else if constexpr (std::is_same_v<type, int32_t>)
            {
                BSON_APPEND_INT32(doc, std::get<I>(names).value, std::get<I>(_refs));
            }
            else if constexpr (std::is_same_v<type, int64_t>)
            {
                BSON_APPEND_INT64(doc, std::get<I>(names).value, std::get<I>(_refs));
            }
            else
            {
                bson_t child;
                BSON_APPEND_DOCUMENT_BEGIN(doc, std::get<I>(names).value, &child);
                std::get<I>(_refs).template serialize_impl<0>(&child);
                bson_append_document_end(doc, &child);
            }

            if constexpr (I + 1 < sizeof...(MemberTypes))
            {
                serialize_impl<I + 1>(doc);
            }
        }

        inline bson_t serialize()
        {
            bson_t doc = BSON_INITIALIZER;
            serialize_impl<0>(&doc);
            return doc;
        }

        std::tuple<std::add_lvalue_reference_t<MemberTypes>...> _refs;
        bson_visitor_t _visitor;
    };

    static inline auto names = std::tuple(MemberNames...);
    static inline auto hashes = std::tuple(MemberNames.hash()...);
    static inline bson_t _fields = BSON_INITIALIZER;
    static inline bool _fields_init = false;
};
