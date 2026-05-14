// SPDX-License-Identifier: GPL-2.0-or-later
#include "input_event_buffer.hpp"

namespace nomos::rt {

input_event_buffer::input_event_buffer() noexcept {
    vtable_.ctx  = this;
    vtable_.size = size_impl;
    vtable_.get  = get_impl;
}

void input_event_buffer::drain(input_event_queue& q) noexcept {
    while (count_ < max_events) {
        auto ev = q.pop();
        if (!ev)
            break;
        events_[count_++] = *ev;
    }
}

uint32_t input_event_buffer::size_impl(const clap_input_events_t* list) noexcept {
    const auto* self = static_cast<const input_event_buffer*>(list->ctx);
    return static_cast<uint32_t>(self->count_);
}

const clap_event_header_t* input_event_buffer::get_impl(const clap_input_events_t* list,
                                                        uint32_t                   index) noexcept {
    const auto* self = static_cast<const input_event_buffer*>(list->ctx);
    if (index >= self->count_)
        return nullptr;
    return &self->events_[index].header;
}

} // namespace nomos::rt
