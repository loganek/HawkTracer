#ifndef HAWKTRACER_PARSER_EVENT_KLASS_H
#define HAWKTRACER_PARSER_EVENT_KLASS_H

#include <memory>
#include <string>
#include <vector>
#include <type_traits>

#include <hawktracer/mkcreflect.h>

namespace HawkTracer {
namespace parser {

template <typename T>
constexpr typename std::underlying_type<T>::type to_underlying(T v) noexcept
{
    return static_cast<typename std::underlying_type<T>::type>(v);
}

class Event;

typedef union {
    uint8_t f_UINT8; int8_t f_INT8;
    uint16_t f_UINT16; int16_t f_INT16;
    uint32_t f_UINT32; int32_t f_INT32;
    uint64_t f_UINT64; int64_t f_INT64;
    void* f_POINTER;
    char* f_STRING;
    Event* f_EVENT;
} FieldType;

enum class FieldTypeId
{
    UINT8 = 0, INT8,
    UINT16, INT16,
    UINT32, INT32,
    UINT64, INT64,
    POINTER,
    STRING,
    STRUCT
};

FieldTypeId get_type_id(size_t type_size, MKCREFLECT_Types data_type);

class EventKlassField
{
public:
    EventKlassField(std::string name, std::string type_name, FieldTypeId type_id);

    std::string get_name() const { return _name; }
    std::string get_type_name() const { return _type_name; }
    FieldTypeId get_type_id() const { return _type_id; }
    size_t get_sizeof() const;
    bool is_numeric() const
    {
        return to_underlying(_type_id) <= to_underlying(FieldTypeId::INT64);
    }

public:
    std::string _name;
    std::string _type_name;
    FieldTypeId _type_id;
};

class EventKlass
{
public:
    EventKlass(const std::string& name, uint32_t id);

    std::string get_name() const { return _name; }
    uint32_t get_id() const { return _id; }
    const std::vector<std::unique_ptr<EventKlassField>>& get_fields() const { return _fields; }
    void add_field(std::unique_ptr<EventKlassField> field);

private:
    std::vector<std::unique_ptr<EventKlassField>> _fields;
    std::string _name;
    uint32_t _id;
};

} // namespace parser
} // namespace HawkTracer

#endif // HAWKTRACER_PARSER_EVENT_KLASS_H
