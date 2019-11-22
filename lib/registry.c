#include "hawktracer/alloc.h"
#include "hawktracer/core_events.h"
#include "internal/bag.h"
#include "internal/hash.h"
#include "internal/registry.h"
#include "internal/mutex.h"
#include "internal/timeline_listener_container.h"
#include "internal/event_utils.h"

#include <assert.h>
#include <string.h>

static HT_BagVoidPtr event_klass_register;
static HT_BagVoidPtr listeners_register;

static HT_Mutex* listeners_register_mutex;
static HT_Mutex* event_klass_registry_register_mutex;

#define HT_CREATE_MUTEX_(mutex_var, error_code_var, label_if_fails) \
    do { \
        mutex_var = ht_mutex_create(); \
        if (!mutex_var) \
        { \
            error_code_var = HT_ERR_OUT_OF_MEMORY; \
            goto label_if_fails; \
        } \
    } while(0)

HT_ErrorCode
ht_registry_init(void)
{
    HT_ErrorCode error_code;

    error_code = ht_bag_void_ptr_init(&event_klass_register, 8);
    if (error_code != HT_ERR_OK)
    {
        goto done;
    }

    error_code = ht_bag_void_ptr_init(&listeners_register, 8);
    if (error_code != HT_ERR_OK)
    {
        goto error_listeners_register;
    }

    HT_CREATE_MUTEX_(listeners_register_mutex, error_code, error_listeners_mutex);
    HT_CREATE_MUTEX_(event_klass_registry_register_mutex, error_code, error_event_klass_registry_mutex);

    goto done;

error_event_klass_registry_mutex:
    ht_mutex_destroy(listeners_register_mutex);
error_listeners_mutex:
    ht_bag_void_ptr_deinit(&listeners_register);
error_listeners_register:
    ht_bag_void_ptr_deinit(&event_klass_register);
done:
    return error_code;
}

HT_EventKlassId
ht_registry_register_event_klass(HT_EventKlass* event_klass)
{
    if (event_klass->klass_id == HT_INVALID_KLASS_ID)
    {
        ht_mutex_lock(event_klass_registry_register_mutex);

        if (ht_bag_void_ptr_add(&event_klass_register, event_klass) == HT_ERR_OK)
        {
            event_klass->klass_id = (HT_EventKlassId)event_klass_register.size - 1;
        }

        ht_mutex_unlock(event_klass_registry_register_mutex);
    }

    return event_klass->klass_id;
}

HT_EventKlass**
ht_registry_get_event_klasses(size_t* out_klass_count)
{
    assert(out_klass_count != NULL);

    *out_klass_count = event_klass_register.size;
    return (HT_EventKlass**)event_klass_register.data;
}

void
ht_registry_deinit(void)
{
    size_t i;
    for (i = 0; i < listeners_register.size; i++)
    {
        ht_timeline_listener_container_unref((HT_TimelineListenerContainer*)listeners_register.data[i]);
    }

    ht_mutex_destroy(event_klass_registry_register_mutex);
    ht_mutex_destroy(listeners_register_mutex);
    ht_bag_void_ptr_deinit(&listeners_register);
    ht_bag_void_ptr_deinit(&event_klass_register);
}

HT_TimelineListenerContainer*
ht_registry_find_listener_container(const char* name)
{
    size_t i;
    uint32_t id = djb2_hash(name);

    ht_mutex_lock(listeners_register_mutex);
    for (i = 0; i < listeners_register.size; i++)
    {
        if (ht_timeline_listener_container_get_id((HT_TimelineListenerContainer*)listeners_register.data[i]) == id)
        {
            HT_TimelineListenerContainer* container = (HT_TimelineListenerContainer*)listeners_register.data[i];
            ht_mutex_unlock(listeners_register_mutex);
            return container;
        }
    }
    ht_mutex_unlock(listeners_register_mutex);

    return NULL;
}

HT_ErrorCode
ht_registry_register_listener_container(const char* name, HT_TimelineListenerContainer* container)
{
    HT_ErrorCode error_code;

    if (ht_registry_find_listener_container(name) != NULL)
    {
        return HT_ERR_LISTENER_CONTAINER_ALREADY_REGISTERED;
    }

    ht_mutex_lock(listeners_register_mutex);
    ht_timeline_listener_container_set_id(container, djb2_hash(name));
    ht_timeline_listener_container_ref(container);

    error_code = ht_bag_void_ptr_add(&listeners_register, container);
    if (error_code != HT_ERR_OK)
    {
        ht_mutex_unlock(listeners_register_mutex);
        return error_code;
    }

    ht_mutex_unlock(listeners_register_mutex);

    return HT_ERR_OK;
}

#define REGISTRY_LISETNER_BUFF_SIZE 4096

static void
_ht_registry_init_event_klass_info_event(HT_EventKlass* klass, HT_EventKlassInfoEvent* event)
{
    event->base.id = ht_event_id_provider_next(ht_event_id_provider_get_default());
    event->base.timestamp = ht_monotonic_clock_get_timestamp();

    event->event_klass_name = klass->type_info->name;
    event->info_klass_id = klass->klass_id;
    event->field_count = (uint8_t) klass->type_info->fields_count;
}

static void
_ht_registry_init_event_klass_field_info_event(HT_EventKlass* klass, size_t field_id, HT_EventKlassFieldInfoEvent* event)
{
    MKCREFLECT_FieldInfo* info = &klass->type_info->fields[field_id];
    event->base.id = ht_event_id_provider_next(ht_event_id_provider_get_default());
    event->base.timestamp = ht_monotonic_clock_get_timestamp();

    if (info->data_type == MKCREFLECT_TYPES_INTEGER && !info->is_signed)
    {
        event->data_type = HT_MKCREFLECT_TYPES_EXT_UNSIGNED_INTEGER;
    }
    else
    {
        event->data_type = (uint8_t)info->data_type;
    }

    event->info_klass_id = klass->klass_id;
    event->field_name = info->field_name;
    event->field_type = info->field_type;
    event->size = info->size;
}

static size_t
_ht_registry_push_class_to_listener(HT_EventKlass* klass, HT_Byte* data, size_t* data_pos, HT_TimelineListenerCallback callback, void* listener, HT_Boolean serialize)
{
    size_t j;
    size_t total_size = 0;
    HT_DECL_EVENT(HT_EventKlassInfoEvent, event);
    _ht_registry_init_event_klass_info_event(klass, &event);

    if (ht_HT_EventKlassInfoEvent_get_size(HT_EVENT(&event)) > REGISTRY_LISETNER_BUFF_SIZE - *data_pos)
    {
        callback(data, *data_pos, serialize, listener);
        total_size += *data_pos;
        *data_pos = 0;
    }

    *data_pos += ht_event_utils_serialize_event_to_buffer(HT_EVENT(&event), data + *data_pos, serialize);

    for (j = 0; j < klass->type_info->fields_count; j++)
    {
        HT_DECL_EVENT(HT_EventKlassFieldInfoEvent, field_event);
        _ht_registry_init_event_klass_field_info_event(klass, j, &field_event);

        if (ht_HT_EventKlassFieldInfoEvent_get_size(HT_EVENT(&field_event)) > REGISTRY_LISETNER_BUFF_SIZE - *data_pos)
        {
            callback(data, *data_pos, serialize, listener);
            total_size += *data_pos;
            *data_pos = 0;
        }

        *data_pos += ht_event_utils_serialize_event_to_buffer(HT_EVENT(&field_event), data + *data_pos, serialize);
    }

    return total_size;
}

size_t
ht_registry_push_registry_klasses_to_listener(HT_TimelineListenerCallback callback, void* listener, HT_Boolean serialize)
{
    size_t i;
    size_t total_size = 0;

    HT_Byte data[REGISTRY_LISETNER_BUFF_SIZE];
    size_t data_pos = 0;

    ht_mutex_lock(event_klass_registry_register_mutex);

    for (i = 0; i < event_klass_register.size; i++)
    {
        total_size += _ht_registry_push_class_to_listener((HT_EventKlass*)event_klass_register.data[i], data, &data_pos, callback, listener, serialize);
    }

    ht_mutex_unlock(event_klass_registry_register_mutex);

    if (data_pos > 0)
    {
        callback(data, data_pos, serialize, listener);
        total_size += data_pos;
    }

    return total_size;
}
