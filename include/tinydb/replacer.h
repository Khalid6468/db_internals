#pragma once

#include <cstddef>
#include <cstdint>

namespace tinydb {
    using frame_id_t = uint32_t;
    constexpr frame_id_t INVALID_FRAME_ID = static_cast<frame_id_t>(-1);
    
    class Replacer {
        public:
            virtual ~Replacer() = default;

            virtual void RecordAccess(frame_id_t frame_id) = 0;

            // Pin/UnPin are a boolean toggle, not a reference count: a frame is
            // either evictable or it isn't. If a frame can have multiple
            // concurrent pinners, the caller (e.g. a buffer pool manager) is
            // responsible for tracking its own pin count and calling Pin only on
            // the 0->1 transition and UnPin only on the 1->0 transition.
            // Calling UnPin while another pinner still considers the frame
            // pinned will make it evictable out from under that pinner.
            virtual void Pin(frame_id_t frame_id) = 0;
            virtual void UnPin(frame_id_t frame_id) = 0;

            virtual bool Victim(frame_id_t *frame_id) = 0;

            // Unconditionally stops tracking frame_id: clears it out of whatever
            // internal state the policy keeps for it, regardless of its current
            // evictable status. Unlike Victim, the caller picks which frame goes
            // away -- use this when a frame's underlying page is being retired
            // out of band (e.g. the page was deleted) rather than through normal
            // replacement. As with Pin/UnPin, this trusts the caller to only call
            // it once the frame is actually safe to forget (not pinned); it has
            // no way to verify that itself. No-op, returning false, if frame_id
            // isn't currently tracked.
            virtual bool Remove(frame_id_t frame_id) = 0;

            virtual size_t Size() const = 0;
    };
}
