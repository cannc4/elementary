#pragma once

#ifdef USE_YYJSON

#include "yyjson.h"
#include "Value.h"
#include <stdexcept>
#include <string>
#include <sstream>

namespace elem
{
namespace js
{

    namespace detail
    {
        // Forward declare stream serializer for compatibility
        template <typename Stream>
        static void serialize(Stream& output, Value const& v);

        // Sanitize UTF-8 string by replacing invalid sequences with U+FFFD (replacement character)
        static std::string sanitizeUTF8(const char* str)
        {
            if (!str) return "";

            std::string result;
            result.reserve(strlen(str));

            const unsigned char* p = reinterpret_cast<const unsigned char*>(str);
            while (*p)
            {
                // ASCII character (0x00-0x7F)
                if (*p < 0x80)
                {
                    result += static_cast<char>(*p);
                    p++;
                }
                // 2-byte UTF-8 (0xC0-0xDF)
                else if ((*p & 0xE0) == 0xC0)
                {
                    if ((p[1] & 0xC0) == 0x80)
                    {
                        result += static_cast<char>(*p);
                        result += static_cast<char>(p[1]);
                        p += 2;
                    }
                    else
                    {
                        result += "\xEF\xBF\xBD"; // U+FFFD
                        p++;
                    }
                }
                // 3-byte UTF-8 (0xE0-0xEF)
                else if ((*p & 0xF0) == 0xE0)
                {
                    if ((p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80)
                    {
                        result += static_cast<char>(*p);
                        result += static_cast<char>(p[1]);
                        result += static_cast<char>(p[2]);
                        p += 3;
                    }
                    else
                    {
                        result += "\xEF\xBF\xBD"; // U+FFFD
                        p++;
                    }
                }
                // 4-byte UTF-8 (0xF0-0xF7)
                else if ((*p & 0xF8) == 0xF0)
                {
                    if ((p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80 && (p[3] & 0xC0) == 0x80)
                    {
                        result += static_cast<char>(*p);
                        result += static_cast<char>(p[1]);
                        result += static_cast<char>(p[2]);
                        result += static_cast<char>(p[3]);
                        p += 4;
                    }
                    else
                    {
                        result += "\xEF\xBF\xBD"; // U+FFFD
                        p++;
                    }
                }
                // Invalid UTF-8 start byte
                else
                {
                    result += "\xEF\xBF\xBD"; // U+FFFD
                    p++;
                }
            }

            return result;
        }

        // Convert yyjson_val to elem::js::Value recursively
        static Value convertFromYYJSON(yyjson_val* val)
        {
            if (!val)
                return Value();

            if (yyjson_is_null(val))
                return Null();

            if (yyjson_is_bool(val))
                return Value(yyjson_get_bool(val));

            if (yyjson_is_num(val))
                return Value(yyjson_get_num(val));

            if (yyjson_is_str(val))
                return Value(String(yyjson_get_str(val)));

            if (yyjson_is_arr(val))
            {
                Array arr;
                size_t idx, max;
                yyjson_val* item;
                yyjson_arr_foreach(val, idx, max, item) {
                    arr.push_back(convertFromYYJSON(item));
                }
                return Value(arr);
            }

            if (yyjson_is_obj(val))
            {
                Object obj;
                size_t idx, max;
                yyjson_val* key, *item;
                yyjson_obj_foreach(val, idx, max, key, item) {
                    obj[yyjson_get_str(key)] = convertFromYYJSON(item);
                }
                return Value(obj);
            }

            return Value();
        }

        // Build yyjson_mut_val from elem::js::Value recursively
        static yyjson_mut_val* convertToYYJSON(yyjson_mut_doc* doc, Value const& v)
        {
            if (v.isUndefined() || v.isNull())
                return yyjson_mut_null(doc);

            if (v.isBool())
                return yyjson_mut_bool(doc, (Boolean) v);

            if (v.isNumber())
                return yyjson_mut_real(doc, (Number) v);

            if (v.isString())
            {
                std::string sanitized = sanitizeUTF8(((String) v).c_str());
                return yyjson_mut_strcpy(doc, sanitized.c_str());
            }

            if (v.isArray())
            {
                yyjson_mut_val* arr = yyjson_mut_arr(doc);
                auto const& elemArr = v.getArray();
                for (auto const& item : elemArr)
                {
                    yyjson_mut_val* child = convertToYYJSON(doc, item);
                    yyjson_mut_arr_append(arr, child);
                }
                return arr;
            }

            if (v.isFloat32Array())
            {
                yyjson_mut_val* arr = yyjson_mut_arr(doc);
                auto const& elemArr = v.getFloat32Array();
                for (auto const& item : elemArr)
                {
                    yyjson_mut_val* child = yyjson_mut_real(doc, (double) item);
                    yyjson_mut_arr_append(arr, child);
                }
                return arr;
            }

            if (v.isObject())
            {
                yyjson_mut_val* obj = yyjson_mut_obj(doc);
                auto const& elemObj = v.getObject();
                for (auto const& [key, val] : elemObj)
                {
                    std::string sanitizedKey = sanitizeUTF8(key.c_str());
                    yyjson_mut_val* k = yyjson_mut_strcpy(doc, sanitizedKey.c_str());
                    yyjson_mut_val* v = convertToYYJSON(doc, val);
                    yyjson_mut_obj_add(obj, k, v);
                }
                return obj;
            }

            throw std::runtime_error("Failed to convert Value: unsupported type.");
        }

        // Stream-based serializer for compatibility with existing code
        // This is a simple wrapper that uses yyjson and outputs to stream
        template <typename Stream>
        static void serialize(Stream& output, Value const& v)
        {
            // For stream-based serialization, we use the string-based serialize
            // and output it to the stream. This maintains API compatibility.
            yyjson_mut_doc* doc = yyjson_mut_doc_new(NULL);
            if (!doc)
                throw std::runtime_error("Failed to create yyjson mutable document.");

            yyjson_mut_val* root = convertToYYJSON(doc, v);
            yyjson_mut_doc_set_root(doc, root);

            yyjson_write_err err;
            size_t len;
            // Strings are sanitized in convertToYYJSON, so we can use default flags
            char* json = yyjson_mut_write_opts(doc, YYJSON_WRITE_NOFLAG, NULL, &len, &err);

            if (!json)
            {
                yyjson_mut_doc_free(doc);
                std::string errorMsg = "Serialization error: ";
                if (err.msg)
                    errorMsg += err.msg;
                else
                    errorMsg += "unknown error";
                throw std::runtime_error(errorMsg);
            }

            output.write(json, len);
            free(json);
            yyjson_mut_doc_free(doc);
        }
    }

    // Deserialize a JSON string into a Value using yyjson
    static Value parseJSON(std::string const& str)
    {
        // Read the JSON string
        yyjson_read_err err;
        yyjson_doc* doc = yyjson_read_opts((char*)str.c_str(), str.length(), YYJSON_READ_ALLOW_INVALID_UNICODE, NULL, &err);

        if (!doc)
        {
            if (err.code != YYJSON_READ_SUCCESS)
            {
                std::string errorMsg = "Parse error: ";
                if (err.msg)
                    errorMsg += err.msg;
                else
                    errorMsg += "unknown error";
                throw std::runtime_error(errorMsg);
            }
            throw std::runtime_error("Failed to parse json string.");
        }

        // Get the root value
        yyjson_val* root = yyjson_doc_get_root(doc);
        if (!root)
        {
            yyjson_doc_free(doc);
            throw std::runtime_error("JSON document has no root value.");
        }

        // Enforce top-level array or object requirement (matching nlohmann behavior)
        if (!yyjson_is_arr(root) && !yyjson_is_obj(root))
        {
            yyjson_doc_free(doc);
            throw std::runtime_error("Top-level JSON value must be an array or object.");
        }

        // Convert to elem::js::Value
        Value result = detail::convertFromYYJSON(root);

        // Free the yyjson document
        yyjson_doc_free(doc);

        return result;
    }

    // Serialize a Value object to a JSON string using yyjson
    static std::string serialize(Value const& v)
    {
        // Create a mutable document
        yyjson_mut_doc* doc = yyjson_mut_doc_new(NULL);
        if (!doc)
            throw std::runtime_error("Failed to create yyjson mutable document.");

        // Convert elem::js::Value to yyjson_mut_val
        yyjson_mut_val* root = detail::convertToYYJSON(doc, v);
        yyjson_mut_doc_set_root(doc, root);

        // Write to JSON string (minified, no extra whitespace)
        // Strings are sanitized in convertToYYJSON, so we can use default flags
        yyjson_write_err err;
        size_t len;
        char* json = yyjson_mut_write_opts(doc, YYJSON_WRITE_NOFLAG, NULL, &len, &err);

        if (!json)
        {
            yyjson_mut_doc_free(doc);
            std::string errorMsg = "Serialization error: ";
            if (err.msg)
                errorMsg += err.msg;
            else
                errorMsg += "unknown error";
            throw std::runtime_error(errorMsg);
        }

        // Copy to std::string and free
        std::string result(json, len);
        free(json);
        yyjson_mut_doc_free(doc);

        return result;
    }

} // namespace js
} // namespace elem

#else

// Fallback to existing nlohmann/json implementation
#include "./deps/json.hpp"
#include "Value.h"


namespace elem
{
namespace js
{

    // Deserialize a JSON string into a Value
    //
    // This uses the nlohmann/json library for parsing the json string, with the
    // SAX event consumer for building up our Value in response to the given
    // parse events.
    static Value parseJSON (std::string const& str)
    {
        using json = nlohmann::json;

        struct sax_event_consumer : public json::json_sax_t
        {
            Value top;
            std::vector<Value*> stack;
            std::string pendingKey;

            bool push(Value && v)
            {
                bool const descend = v.isArray() || v.isObject();

                // First value that we push will become our top-level result
                if (top.isUndefined())
                {
                    // If the top level value is neither an object nor an array,
                    // we have a parsing problem
                    if (!descend)
                        return false;

                    top = std::move(v);
                    stack.push_back(&top);
                    return true;
                }

                auto* current = stack.back();

                if (current->isArray())
                {
                    current->getArray().push_back(std::move(v));

                    if (descend)
                    {
                        auto& a = current->getArray();
                        stack.push_back(&(a.back()));
                    }

                    return true;
                }

                if (current->isObject())
                {
                    // If we're pushing a value onto an object and don't have a corresponding
                    // key to write it with, we have a problem
                    if (pendingKey.empty())
                        return false;

                    current->getObject().insert({pendingKey, std::move(v)});

                    if (descend)
                    {
                        auto& o = current->getObject();
                        stack.push_back(&(o.at(pendingKey)));
                    }

                    pendingKey.clear();
                    return true;
                }

                return false;
            }

            bool null() override
            {
                return push(Null());
            }

            bool boolean(bool val) override
            {
                return push(Value(val));
            }

            bool number_integer(number_integer_t val) override
            {
                return push(Value((double) val));
            }

            bool number_unsigned(number_unsigned_t val) override
            {
                return push(Value((double) val));
            }

            bool number_float(number_float_t val, const string_t& /* s */) override
            {
                return push(Value((double) val));
            }

            bool string(string_t& val) override
            {
                return push(Value(val));
            }

            bool start_object(std::size_t /* elements */) override
            {
                return push(Object());
            }

            bool end_object() override
            {
                stack.pop_back();
                return true;
            }

            bool start_array(std::size_t /* elements */) override
            {
                return push(Array());
            }

            bool end_array() override
            {
                stack.pop_back();
                return true;
            }

            bool key(string_t& val) override
            {
                pendingKey = val;
                return true;
            }

            bool binary(json::binary_t& /* val */) override
            {
                throw std::runtime_error("Deserializing binary is not supported.");
            }

            bool parse_error(std::size_t /* position */, const std::string& /* last_token */, const json::exception& ex) override
            {
                throw std::runtime_error("Parse error:" + std::string(ex.what()));
            }
        };

        sax_event_consumer sec;

        if (!json::sax_parse(str, &sec))
            throw std::runtime_error("Failed to parse json string.");

        return sec.top;
    }

    namespace detail
    {

        template <typename Stream>
        static void serialize (Stream& output, Value const& v);

        template <typename Stream>
        static void serializeNull (Stream& output) {
            output << "null";
        }

        template <typename Stream>
        static void serialize (Stream& output, Boolean const& v) {
            output << (v ? "true" : "false");
        }

        template <typename Stream>
        static void serialize (Stream& output, Number const& v) {
            // Using nlohmann::json's serializer to ensure that our float representation
            // matches the json spec
            output << nlohmann::json(v).dump();
        }

        template <typename Stream>
        static void serialize (Stream& output, String const& v) {
            // Using nlohmann::json's serializer to ensure that our strings are properly
            // escaped and handle unicode characters
            output << nlohmann::json(v).dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
        }

        template <typename Stream>
        static void serialize (Stream& output, Array const& v) {
            output << '[';

            for (size_t i = 0; i < v.size(); ++i) {
                if (i != 0) output << ", ";
                serialize(output, v[i]);
            }

            output << ']';
        }

        template <typename Stream>
        static void serialize (Stream& output, Float32Array const& v) {
            output << '[';

            for (size_t i = 0; i < v.size(); ++i) {
                if (i != 0) output << ", ";
                serialize(output, v[i]);
            }

            output << ']';
        }

        template <typename Stream>
        static void serialize (Stream& output, Object const& v) {
            size_t i = 0;
            output << '{';

            for (const auto& [key, val] : v) {
                if (i++ != 0) output << ", ";

                // Using nlohmann::json's serializer to ensure that our key strings
                // are properly escaped and handle unicode characters
                output << nlohmann::json(key).dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
                output << ": ";
                serialize(output, val);
            }

            output << '}';
        }

        template <typename Stream>
        static void serialize (Stream& output, Value const& v) {
            if (v.isUndefined())    return (void) detail::serializeNull(output);
            if (v.isNull())         return (void) detail::serializeNull(output);
            if (v.isBool())         return (void) detail::serialize(output, (Boolean) v);
            if (v.isNumber())       return (void) detail::serialize(output, (Number) v);
            if (v.isString())       return (void) detail::serialize(output, (String) v);
            if (v.isArray())        return (void) detail::serialize(output, v.getArray());
            if (v.isFloat32Array()) return (void) detail::serialize(output, v.getFloat32Array());
            if (v.isObject())       return (void) detail::serialize(output, v.getObject());

            throw std::runtime_error("Failed to serialize Value: unsupported type.");
        }

    }

    // Serialize a Value object to a JSON string
    static std::string serialize (Value const& v)
    {
        std::ostringstream o;
        detail::serialize(o, v);
        return o.str();
    }

} // namespace js
} // namespace elem

#endif
