#include "hawktracer/parser/klass_register.hpp"
#include "hawktracer/parser/event.hpp"
#include "hawktracer/parser/make_unique.hpp"

namespace HawkTracer {
namespace parser {

KlassRegister::KlassRegister()
{
    auto base_event = make_unique<EventKlass>("HT_Event", to_underlying(WellKnownKlasses::EventKlass));
    base_event->add_field(make_unique<EventKlassField>("klass_id", "uint32_t", FieldTypeId::UINT32));
    base_event->add_field(make_unique<EventKlassField>("timestamp", "uint64_t", FieldTypeId::UINT64));
    base_event->add_field(make_unique<EventKlassField>("id", "uint64_t", FieldTypeId::UINT64));
    _add_klass(std::move(base_event));

    auto endianness_event = make_unique<EventKlass>("HT_EndiannessInfoEvent", to_underlying(WellKnownKlasses::EndiannessInfoEventKlass));
    endianness_event->add_field(make_unique<EventKlassField>("endianness", "uint8_t", FieldTypeId::UINT8));
    _add_klass(std::move(endianness_event));

    auto klass_info_event = make_unique<EventKlass>("HT_EventKlassInfoEvent", to_underlying(WellKnownKlasses::EventKlassInfoEventKlass));
    klass_info_event->add_field(make_unique<EventKlassField>("info_klass_id", "uint32_t", FieldTypeId::UINT32));
    klass_info_event->add_field(make_unique<EventKlassField>("event_klass_name", "const char*", FieldTypeId::STRING));
    klass_info_event->add_field(make_unique<EventKlassField>("field_count", "uint8_t", FieldTypeId::UINT8));
    _add_klass(std::move(klass_info_event));

    auto klass_field_info_event = make_unique<EventKlass>("HT_EventKlassFieldInfoEvent", to_underlying(WellKnownKlasses::EventKlassFieldInfoEventKlass));
    klass_field_info_event->add_field(make_unique<EventKlassField>("info_klass_id", "uint32_t", FieldTypeId::UINT32));
    klass_field_info_event->add_field(make_unique<EventKlassField>("field_type", "const char*", FieldTypeId::STRING));
    klass_field_info_event->add_field(make_unique<EventKlassField>("field_name", "const char*", FieldTypeId::STRING));
    klass_field_info_event->add_field(make_unique<EventKlassField>("size", "uint64_t", FieldTypeId::UINT64));
    klass_field_info_event->add_field(make_unique<EventKlassField>("data_type", "uint8_t", FieldTypeId::UINT8));
    _add_klass(std::move(klass_field_info_event));
}

bool KlassRegister::is_well_known_klass(HT_EventKlassId klass_id)
{
    switch ((WellKnownKlasses)klass_id)
    {
    case WellKnownKlasses::EventKlass:
    case WellKnownKlasses::EventKlassInfoEventKlass:
    case WellKnownKlasses::EventKlassFieldInfoEventKlass:
        return true;
    default:
        return false;
    }
}

void KlassRegister::handle_register_events(const Event& event)
{
    switch (static_cast<WellKnownKlasses>(event.get_klass()->get_id()))
    {
    case WellKnownKlasses::EventKlassInfoEventKlass:
    {
        auto klass_id = event.get_value<HT_EventKlassId>("info_klass_id");
        _add_klass(make_unique<EventKlass>(event.get_value<char*>("event_klass_name"), klass_id));
        break;
    }
    case WellKnownKlasses::EventKlassFieldInfoEventKlass:
    {
        auto klass_id = event.get_value<HT_EventKlassId>("info_klass_id");
        if (!KlassRegister::is_well_known_klass(klass_id))
        {
            auto type = static_cast<HT_MKCREFLECT_Types_Ext>(event.get_value<uint8_t>("data_type"));
            const char* field_type = event.get_value<char*>("field_type");
            std::shared_ptr<const EventKlass> type_klass;
            if (type == HT_MKCREFLECT_TYPES_EXT_STRUCT)
            {
                type_klass = get_klass(field_type);
            }

            auto field = HawkTracer::parser::make_unique<EventKlassField>(
                        event.get_value<char*>("field_name"),
                        field_type,
                        get_type_id(event.get_value<uint64_t>("size"), type),
                        std::move(type_klass));

            _add_klass_field(klass_id, std::move(field));
        }
        break;
    }
    default:
        break;
    }
}

std::shared_ptr<const EventKlass> KlassRegister::get_klass(HT_EventKlassId klass_id) const
{
    lock_guard l(_register_mtx);
    auto klass_it = _register.find(klass_id);
    return klass_it != _register.end() ? klass_it->second : nullptr;
}

std::shared_ptr<const EventKlass> KlassRegister::get_klass(const std::string& name) const
{
    return get_klass(get_klass_id(name));
}

HT_EventKlassId KlassRegister::get_klass_id(const std::string& name) const
{
    lock_guard l(_register_mtx);
    for (const auto& klass : _register)
    {
        if (klass.second->get_name() == name)
        {
            return klass.second->get_id();
        }
    }

    return HT_INVALID_KLASS_ID;
}

void KlassRegister::_add_klass(std::unique_ptr<EventKlass> klass)
{
    lock_guard l(_register_mtx);
    if (_register.find(klass->get_id()) == _register.end())
    {
        _register.insert(std::make_pair(klass->get_id(), std::move(klass)));
    }
}

void KlassRegister::_add_klass_field(HT_EventKlassId klass_id, std::unique_ptr<EventKlassField> field)
{
    lock_guard l(_register_mtx);
    _register.at(klass_id)->add_field(std::move(field));
}

bool KlassRegister::klass_exists(HT_EventKlassId klass_id) const
{
    lock_guard l(_register_mtx);
    return _register.find(klass_id) != _register.end();
}

std::unordered_map<HT_EventKlassId, std::shared_ptr<EventKlass>> KlassRegister::get_klasses() const
{
    lock_guard l(_register_mtx);
    return _register;
}

} // namespace parser
} // namespace HawkTracer
