#include <hawktracer/timeline.h>
#include <hawktracer/events.h>

#include "test_test_events.h"
#include "test_common.h"

#include <gtest/gtest.h>

#include <thread>

class TestTimeline : public ::testing::Test
{
protected:
    void SetUp() override
    {
        _timeline = ht_timeline_create(sizeof(HT_Event) * 3, HT_FALSE, HT_FALSE, nullptr, nullptr);
    }

    void TearDown() override
    {
        ht_timeline_unregister_all_listeners(_timeline);
        ht_timeline_destroy(_timeline);
    }

    HT_Timeline* _timeline;
};

// TODO test different types of events

TEST_F(TestTimeline, PublishEventsShouldNotifyListener)
{
    // Arrange
    NotifyInfo<HT_Event> info;

    ht_timeline_register_listener(_timeline, test_listener<HT_Event>, &info);

    // Act
    for (int i = 0; i < 10; i++)
    {
        HT_DECL_EVENT(HT_Event, event);
        ht_timeline_push_event(_timeline, &event);
    }

    // Assert
    ASSERT_EQ(9 * sizeof(HT_Event), info.notified_events); // last event not sent, because buffer is not full
    ASSERT_EQ(3, info.notify_count);
}

TEST_F(TestTimeline, FlushEventsShouldNotifyListener)
{
    // Arrange
    NotifyInfo<HT_Event> info;

    ht_timeline_register_listener(_timeline, test_listener<HT_Event>, &info);

    HT_DECL_EVENT(HT_Event, event);
    ht_timeline_push_event(_timeline, &event);

    // Act
    ht_timeline_flush(_timeline);

    // Assert
    ASSERT_EQ(1 * sizeof(HT_Event), info.notified_events);
    ASSERT_EQ(1, info.notify_count);
}

TEST_F(TestTimeline, TimelineShouldBeFlushedBeforeUninitialized)
{
    // Arrange
    HT_Timeline* timeline = ht_timeline_create(sizeof(HT_Event) * 3, HT_FALSE, HT_FALSE, nullptr, nullptr);
    NotifyInfo<HT_Event> info;

    ht_timeline_register_listener(timeline, test_listener<HT_Event>, &info);

    HT_DECL_EVENT(HT_Event, event);
    ht_timeline_push_event(timeline, &event);

    // Act
    ht_timeline_destroy(timeline);

    // Assert
    ASSERT_EQ(1 * sizeof(HT_Event), info.notified_events);
    ASSERT_EQ(1, info.notify_count);
}

TEST_F(TestTimeline, InitEventTwiceShouldIncreaseId)
{
    // Arrange
    HT_DECL_EVENT(HT_Event, event);

    ht_timeline_init_event(_timeline, &event);
    HT_EventId id = event.id;

    // Act
    ht_timeline_init_event(_timeline, &event);

    // Assert
    ASSERT_EQ(id, event.id - 1);
}

TEST_F(TestTimeline, InitEventShouldSetMonotonicTimestamp)
{
    // Arrange
    HT_DECL_EVENT(HT_Event, event);

    ht_timeline_init_event(_timeline, &event);
    HT_TimestampNs ts = event.timestamp;

    // Act
    ht_timeline_init_event(_timeline, &event);

    // Assert
    ASSERT_LE(ts, event.timestamp);
}

TEST_F(TestTimeline, ThreadSafeMessageShouldWorkWithMultipleThreads)
{
    // Arrange
    const size_t event_count = 20000;
    HT_Timeline* timeline = ht_timeline_create(sizeof(HT_Event) * 3, HT_TRUE, HT_FALSE, NULL, NULL);

    NotifyInfo<HT_Event> info;
    ht_timeline_register_listener(timeline, test_listener<HT_Event>, &info);

    // Act
    std::thread th = std::thread([timeline, &event_count] {
        for (size_t i = event_count / 2; i < event_count; i++)
        {
            HT_DECL_EVENT(HT_Event, event);
            event.timestamp = i;
            ht_timeline_push_event(timeline, &event);
        }
    });

    for (size_t i = 0; i < event_count / 2; i++)
    {
        HT_DECL_EVENT(HT_Event, event);
        event.timestamp = i;
        ht_timeline_push_event(timeline, &event);
    }

    th.join();

    ht_timeline_flush(timeline);

    // Assert
    std::vector<HT_TimestampNs> all_values(event_count, 1);
    HT_TimestampNs sum = 0;
    for (const auto& event : info.values)
    {
        ASSERT_GT(event_count, event.timestamp);
        sum += all_values[(size_t)event.timestamp];
        all_values[(size_t)event.timestamp] = 0;
    }

    ASSERT_EQ(event_count, sum);

    ht_timeline_destroy(timeline);
}

TEST_F(TestTimeline, SharedListener)
{
    // Arrange
    HT_Timeline* timeline1 = ht_timeline_create(sizeof(HT_Event) * 3, HT_TRUE, HT_FALSE, "listener", nullptr);
    NotifyInfo<HT_Event> info;
    ht_timeline_register_listener(timeline1, test_listener<HT_Event>, &info);

    HT_DECL_EVENT(HT_Event, event);
    ht_timeline_init_event(_timeline, &event);

    // Act
    HT_Timeline* timeline2 = ht_timeline_create(sizeof(HT_Event) * 3, HT_TRUE, HT_FALSE, "listener", nullptr);

    ht_timeline_push_event(timeline2, &event);
    ht_timeline_flush(timeline2);

    // Assert
    ASSERT_EQ(1u, info.values.size());
    ASSERT_EQ(event.timestamp, info.values.front().timestamp);

    ht_timeline_destroy(timeline1);
    ht_timeline_destroy(timeline2);
}

TEST_F(TestTimeline, TooLargeEventShouldGoStraightToListeners_DisableSerialization)
{
    // Arrange
    HT_Timeline* timeline = ht_timeline_create(1, HT_TRUE, HT_FALSE, nullptr, nullptr);
    NotifyInfo<DoubleTestEvent> info;
    ht_timeline_register_listener(timeline, test_listener<DoubleTestEvent>, &info);

    // Act
    HT_REGISTER_EVENT_KLASS(DoubleTestEvent);
    HT_DECL_EVENT(DoubleTestEvent, event);
    event.field = 31337;
    ht_timeline_push_event(timeline, ((HT_Event*)(&event)));

    // Assert
    ASSERT_EQ(1, info.notify_count);
    ASSERT_EQ(sizeof(DoubleTestEvent), info.notified_events);
    ASSERT_EQ(event.field, info.values.front().field);

    ht_timeline_unregister_all_listeners(timeline);
    ht_timeline_destroy(timeline);
}

TEST_F(TestTimeline, TooLargeEventShouldGoStraightToListeners_EnableSerialization)
{
    // Arrange
    HT_Timeline* timeline = ht_timeline_create(1, HT_TRUE, HT_TRUE, nullptr, nullptr);
    HT_Byte buffer[64];
    ht_timeline_register_listener(timeline, [] (TEventPtr events, size_t event_count, HT_Boolean /* is_serialized */, void* user_data) {
        HT_Byte* data = (HT_Byte*)user_data;
        memcpy(data, events, event_count);
    }, buffer);

    // Act
    HT_REGISTER_EVENT_KLASS(RegistryTestEvent);
    HT_DECL_EVENT(RegistryTestEvent, event);
    event.field = 30;
    ht_timeline_push_event(timeline, ((HT_Event*)(&event)));

    // Assert
    HT_Event tmp_event;
    int read_value = *(int*)(buffer + ht_HT_Event_get_size(&tmp_event));
    ASSERT_EQ(event.field, read_value);

    ht_timeline_unregister_all_listeners(timeline);
    ht_timeline_destroy(timeline);
}

TEST_F(TestTimeline, PushingLargeEventShouldNotCrashApplication)
{
    // Arrange
    HT_REGISTER_EVENT_KLASS(LargeTestEvent);
    HT_REGISTER_EVENT_KLASS(SuperLargeTestEvent);
    HT_Timeline* timeline = ht_timeline_create(1, HT_TRUE, HT_TRUE, nullptr, nullptr);
    bool called = false;
    ht_timeline_register_listener(timeline, [] (TEventPtr /* events */, size_t /* event_count */, HT_Boolean /* is_serialized */, void* user_data) {
        *static_cast<bool*>(user_data) = true;
    }, &called);

    // Act
    HT_DECL_EVENT(SuperLargeTestEvent, event);
    ht_timeline_push_event(timeline, HT_EVENT(&event));

    // Assert
    ASSERT_TRUE(called);

    ht_timeline_unregister_all_listeners(timeline);
    ht_timeline_destroy(timeline);
}

struct DummyFeature
{
    bool is_destroyed = false;
};

static void destroy_dummy_feature(void* f)
{
    auto feature = static_cast<DummyFeature*>(f);
    feature->is_destroyed = true;
}

TEST_F(TestTimeline, SettingFeatureWithUsedIDShouldFail)
{
    // Arrange
    DummyFeature feature1, feature2;
    ht_timeline_set_feature(_timeline, 10, &feature1, destroy_dummy_feature);

    // Act
    HT_ErrorCode err = ht_timeline_set_feature(_timeline, 10, &feature2, destroy_dummy_feature);

    // Assert
    ASSERT_EQ(HT_ERR_FEATURE_ID_ALREADY_USED, err);
    ASSERT_TRUE(feature2.is_destroyed);
}

TEST_F(TestTimeline, SettingFeatureWithInvalidIDShouldFail)
{
    // Arrange
    DummyFeature feature;

    // Act
    HT_ErrorCode err = ht_timeline_set_feature(_timeline, HT_TIMELINE_MAX_FEATURES, &feature, destroy_dummy_feature);

    // Assert
    ASSERT_EQ(HT_ERR_INVALID_ARGUMENT, err);
    ASSERT_TRUE(feature.is_destroyed);
}

TEST_F(TestTimeline, GetFeatureShouldReturnValidFeatureIfTheFeatureIsSet)
{
    // Arrange
    DummyFeature feature;
    ht_timeline_set_feature(_timeline, 10, &feature, destroy_dummy_feature);

    // Act
    void* f = ht_timeline_get_feature(_timeline, 10);

    // Assert
    ASSERT_EQ(f, &feature);
}

TEST_F(TestTimeline, GetFeatureShouldReturnNullIfTheFeatureIsNotSet)
{
    // Arrange
    // Act
    void* f = ht_timeline_get_feature(_timeline, 10);

    // Assert
    ASSERT_EQ(f, nullptr);
}
