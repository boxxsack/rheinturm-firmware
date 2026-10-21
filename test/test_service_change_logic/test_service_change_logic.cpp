#include <unity.h>

#include "ServiceChangeLogic.h"

void setUp(void) {}
void tearDown(void) {}

void test_differing_marker_is_pending_and_triggers(void) {
    ServiceChangeLogic logic;
    logic.begin("2.7.2", "2.8.0");
    TEST_ASSERT_TRUE(logic.pending());
    TEST_ASSERT_TRUE(logic.shouldTrigger());
}

void test_empty_stored_marker_is_pending(void) {
    ServiceChangeLogic logic;
    logic.begin("", "2.8.0");
    TEST_ASSERT_TRUE(logic.shouldTrigger());
}

void test_equal_marker_never_triggers_or_persists(void) {
    ServiceChangeLogic logic;
    logic.begin("2.8.0", "2.8.0");
    logic.markTriggered();
    logic.markPeerAuthenticated();
    TEST_ASSERT_FALSE(logic.pending());
    TEST_ASSERT_FALSE(logic.shouldTrigger());
    TEST_ASSERT_FALSE(logic.shouldPersist());
}

void test_trigger_happens_once(void) {
    ServiceChangeLogic logic;
    logic.begin("2.7.2", "2.8.0");
    logic.markTriggered();
    TEST_ASSERT_FALSE(logic.shouldTrigger());
}

void test_persist_requires_trigger_and_authenticated_peer(void) {
    ServiceChangeLogic logic;
    logic.begin("2.7.2", "2.8.0");
    TEST_ASSERT_FALSE(logic.shouldPersist());

    logic.markPeerAuthenticated();
    TEST_ASSERT_FALSE(logic.shouldPersist());

    logic.markTriggered();
    TEST_ASSERT_TRUE(logic.shouldPersist());
}

void test_trigger_alone_does_not_persist(void) {
    ServiceChangeLogic logic;
    logic.begin("2.7.2", "2.8.0");
    logic.markTriggered();
    TEST_ASSERT_FALSE(logic.shouldPersist());
}

void test_persisted_marker_is_not_retriggered(void) {
    ServiceChangeLogic logic;
    logic.begin("2.7.2", "2.8.0");
    logic.markTriggered();
    logic.markPeerAuthenticated();
    logic.markPersisted();
    TEST_ASSERT_FALSE(logic.pending());
    TEST_ASSERT_FALSE(logic.shouldTrigger());
    TEST_ASSERT_FALSE(logic.shouldPersist());
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_differing_marker_is_pending_and_triggers);
    RUN_TEST(test_empty_stored_marker_is_pending);
    RUN_TEST(test_equal_marker_never_triggers_or_persists);
    RUN_TEST(test_trigger_happens_once);
    RUN_TEST(test_persist_requires_trigger_and_authenticated_peer);
    RUN_TEST(test_trigger_alone_does_not_persist);
    RUN_TEST(test_persisted_marker_is_not_retriggered);
    return UNITY_END();
}
