#include "tinydb/replacer.h"
#include <gtest/gtest.h>

using namespace tinydb;

TEST(Replacer, NewReplacerHasNoEvictableFrames) {
    Replacer r;
    EXPECT_EQ(r.Size(), 0u);
    frame_id_t victim;
    EXPECT_FALSE(r.Victim(&victim));
}

TEST(Replacer, RecordAccessThenUnPinMakesFrameEvictable) {
    Replacer r;
    r.RecordAccess(1);
    EXPECT_EQ(r.Size(), 0u); // recorded but not yet unpinned
    r.UnPin(1);
    EXPECT_EQ(r.Size(), 1u);
}

TEST(Replacer, PinRemovesFrameFromEvictableSet) {
    Replacer r;
    r.RecordAccess(1);
    r.UnPin(1);
    ASSERT_EQ(r.Size(), 1u);
    r.Pin(1);
    EXPECT_EQ(r.Size(), 0u);
}

TEST(Replacer, PinOnUntrackedFrameIsANoOp) {
    Replacer r;
    r.Pin(42); // never recorded/unpinned -- must not throw or corrupt state
    EXPECT_EQ(r.Size(), 0u);
}

TEST(Replacer, VictimSkipsPinnedFrameAndPicksNextEvictableOne) {
    Replacer r;
    r.RecordAccess(1);
    r.RecordAccess(2);
    r.RecordAccess(3);
    r.UnPin(1);
    r.UnPin(2);
    r.UnPin(3);
    r.Pin(1); // 1 is the LRU-most (oldest) entry, but now not evictable

    frame_id_t victim = INVALID_FRAME_ID;
    ASSERT_TRUE(r.Victim(&victim));
    EXPECT_EQ(victim, 2u);
}

TEST(Replacer, VictimReturnsFalseWhenEverythingIsPinned) {
    Replacer r;
    r.RecordAccess(1);
    r.RecordAccess(2);
    // never unpinned -- nothing evictable
    frame_id_t victim;
    EXPECT_FALSE(r.Victim(&victim));
}

TEST(Replacer, VictimStopsTrackingTheEvictedFrame) {
    Replacer r;
    r.RecordAccess(1);
    r.UnPin(1);
    ASSERT_EQ(r.Size(), 1u);

    frame_id_t victim;
    ASSERT_TRUE(r.Victim(&victim));
    EXPECT_EQ(victim, 1u);
    EXPECT_EQ(r.Size(), 0u);

    // frame_id 1 is gone from tracking; a second victim call must not return it again
    frame_id_t second;
    EXPECT_FALSE(r.Victim(&second));
}

TEST(Replacer, EvictedFrameIdCanBeReusedAsIfBrandNew) {
    Replacer r;
    r.RecordAccess(1);
    r.UnPin(1);
    frame_id_t victim;
    ASSERT_TRUE(r.Victim(&victim));
    ASSERT_EQ(victim, 1u);

    // simulate the frame_id being handed to a different page later
    r.RecordAccess(1);
    r.UnPin(1);
    EXPECT_EQ(r.Size(), 1u);
    frame_id_t victim2;
    ASSERT_TRUE(r.Victim(&victim2));
    EXPECT_EQ(victim2, 1u);
}

TEST(Replacer, SubThresholdRetouchDoesNotReorderWithinOldSegment) {
    // Default threshold (200) is never crossed here, so frame 1 stays in old_'s
    // FIFO order regardless of how many times it's re-touched -- this is the
    // deliberate scan-resistance behavior, not a bug.
    Replacer r; // default threshold
    r.RecordAccess(1);
    r.RecordAccess(2);
    r.RecordAccess(1); // re-touch, but far below the promotion threshold
    r.RecordAccess(1);
    r.UnPin(1);
    r.UnPin(2);

    frame_id_t victim;
    ASSERT_TRUE(r.Victim(&victim));
    EXPECT_EQ(victim, 1u); // still LRU-most despite repeated re-touching
}

TEST(Replacer, PromotionToYoungAfterThresholdAndRetouch) {
    Replacer r(1); // promote as soon as a single global access has elapsed
    r.RecordAccess(1); // old_entry_count_[1] = 1
    r.RecordAccess(2); // global_access_count_ = 2, delta for 1 is now 1 >= threshold
    r.RecordAccess(1); // re-touch -- should promote frame 1 out of old_ into young_
    r.UnPin(1);
    r.UnPin(2);

    // Victim always drains old_ completely before ever considering young_, so if
    // promotion worked, frame 2 (still in old_) must be picked over frame 1
    // (promoted to young_) even though 1 was touched more recently.
    frame_id_t victim;
    ASSERT_TRUE(r.Victim(&victim));
    EXPECT_EQ(victim, 2u);
}

TEST(Replacer, RemoveFromOldSegmentStopsTracking) {
    Replacer r;
    r.RecordAccess(1);
    r.UnPin(1);
    ASSERT_EQ(r.Size(), 1u);

    EXPECT_TRUE(r.Remove(1));
    EXPECT_EQ(r.Size(), 0u);
    frame_id_t victim;
    EXPECT_FALSE(r.Victim(&victim));
}

TEST(Replacer, RemoveFromYoungSegmentStopsTracking) {
    Replacer r(1);
    r.RecordAccess(5);
    r.RecordAccess(6); // bump global count past threshold
    r.RecordAccess(5); // promotes 5 into young_
    r.UnPin(5);
    ASSERT_EQ(r.Size(), 1u);

    EXPECT_TRUE(r.Remove(5));
    EXPECT_EQ(r.Size(), 0u);
}

TEST(Replacer, RemoveOnUntrackedFrameIsANoOpReturningFalse) {
    Replacer r;
    EXPECT_FALSE(r.Remove(999));
}

TEST(Replacer, RemovedFrameIdStartsFreshOnReuseNoStalePromotionState) {
    Replacer r(1);
    r.RecordAccess(1);
    r.RecordAccess(2);
    r.RecordAccess(1); // promotes 1 into young_
    r.Remove(1);

    // frame_id 1 is reused for a completely different page now -- it must be
    // treated as brand new (land in old_), not inherit its promoted status.
    r.RecordAccess(1);

    // set up a second frame that is genuinely, deliberately promoted to young_
    r.RecordAccess(9);
    r.RecordAccess(10);
    r.RecordAccess(9); // promotes 9 into young_

    r.UnPin(1);
    r.UnPin(2);
    r.UnPin(9);
    r.UnPin(10);

    // Victim always drains old_ completely before ever considering young_. If 1
    // is genuinely back in old_ (not still counted as young_), it must come out
    // before frame 9 does, regardless of the exact order within old_.
    frame_id_t victim = INVALID_FRAME_ID;
    bool saw_frame_1 = false;
    for (int i = 0; i < 3; i++) {
        ASSERT_TRUE(r.Victim(&victim));
        ASSERT_NE(victim, 9u) << "young_ frame must not be evicted before old_ is drained";
        if (victim == 1u) saw_frame_1 = true;
    }
    EXPECT_TRUE(saw_frame_1) << "frame 1 was never evicted from old_ -- it must have stayed young_";

    // old_ is now empty; the last evictable frame must be the genuinely-young one
    ASSERT_TRUE(r.Victim(&victim));
    EXPECT_EQ(victim, 9u);
}

TEST(Replacer, RemovingAMiddleEntryLeavesRestOfLruOrderIntact) {
    Replacer r;
    r.RecordAccess(10);
    r.RecordAccess(20);
    r.RecordAccess(30);
    r.UnPin(10);
    r.UnPin(20);
    r.UnPin(30);

    ASSERT_TRUE(r.Remove(20));
    EXPECT_EQ(r.Size(), 2u);

    frame_id_t victim;
    ASSERT_TRUE(r.Victim(&victim));
    EXPECT_EQ(victim, 10u); // still the LRU-most of the remaining entries
}

TEST(Replacer, MostRecentlyTouchedFrameInOldIsEvictedLast) {
    Replacer r;
    r.RecordAccess(1);
    r.RecordAccess(2);
    r.RecordAccess(3);
    r.UnPin(1);
    r.UnPin(2);
    r.UnPin(3);

    frame_id_t v1, v2, v3;
    ASSERT_TRUE(r.Victim(&v1));
    ASSERT_TRUE(r.Victim(&v2));
    ASSERT_TRUE(r.Victim(&v3));
    EXPECT_EQ(v1, 1u);
    EXPECT_EQ(v2, 2u);
    EXPECT_EQ(v3, 3u);
}
