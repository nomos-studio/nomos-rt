# SPDX-License-Identifier: LGPL-2.1-or-later
include(Catch)

add_executable(nomos-rt-tests
    test_modulator_engine.cpp
    test_slope_modulator.cpp
    test_segment_modulator.cpp
    test_spsc_queue.cpp
    test_time_identity.cpp
)

target_link_libraries(nomos-rt-tests
    PRIVATE
        nomos::rt
        nomos::rt-exe-headers
        Catch2::Catch2WithMain
)

set_target_properties(nomos-rt-tests PROPERTIES
    CXX_STANDARD          20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS        OFF
)

catch_discover_tests(nomos-rt-tests)
