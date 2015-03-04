// Copyright 2015 Stellar Development Foundation and contributors. Licensed
// under the ISC License. See the COPYING file at the top-level directory of
// this distribution or at http://opensource.org/licenses/ISC

#include "util/Timer.h"
#include "autocheck/autocheck.hpp"
#include "main/Application.h"
#include "main/Config.h"
#include "main/test.h"
#include "lib/catch.hpp"
#include "util/Logging.h"

using namespace stellar;

TEST_CASE("VirtualClock::pointToISOString", "[timer]")
{
    VirtualClock clock;

    VirtualClock::time_point now = clock.now();
    CHECK(VirtualClock::pointToISOString(now) ==
          std::string("1970-01-01T00:00:00Z"));

    now += std::chrono::hours(36);
    CHECK(VirtualClock::pointToISOString(now) ==
          std::string("1970-01-02T12:00:00Z"));

    now += std::chrono::minutes(10);
    CHECK(VirtualClock::pointToISOString(now) ==
          std::string("1970-01-02T12:10:00Z"));

    now += std::chrono::seconds(3618);
    CHECK(VirtualClock::pointToISOString(now) ==
          std::string("1970-01-02T13:10:18Z"));
}

TEST_CASE("virtual event dispatch order and times", "[timer]")
{
    Config cfg(getTestConfig());
    VirtualClock clock;
    Application::pointer appPtr = Application::create(clock, cfg);
    Application &app = *appPtr;

    VirtualTimer timer1(app);
    VirtualTimer timer20(app);
    VirtualTimer timer21(app);
    VirtualTimer timer200(app);

    size_t eventsDispatched = 0;

    timer1.expires_from_now(std::chrono::nanoseconds(1));
    timer1.async_wait(
        [&](asio::error_code const& e)
        {
            CHECK(clock.now().time_since_epoch().count() == 1);
            CHECK(eventsDispatched++ == 0);
        });

    timer20.expires_from_now(std::chrono::nanoseconds(20));
    timer20.async_wait(
        [&](asio::error_code const& e)
        {
            CHECK(clock.now().time_since_epoch().count() == 20);
            CHECK(eventsDispatched++ == 1);
        });

    timer21.expires_from_now(std::chrono::nanoseconds(21));
    timer21.async_wait(
        [&](asio::error_code const& e)
        {
            CHECK(clock.now().time_since_epoch().count() == 21);
            CHECK(eventsDispatched++ == 2);
        });

    timer200.expires_from_now(std::chrono::nanoseconds(200));
    timer200.async_wait(
        [&](asio::error_code const& e)
        {
            CHECK(clock.now().time_since_epoch().count() == 200);
            CHECK(eventsDispatched++ == 3);
        });

    while(app.crank(false) > 0);
    CHECK(eventsDispatched == 4);
}


TEST_CASE("shared virtual time advances only when all apps idle", "[timer][sharedtimer]")
{
    Config cfg(getTestConfig());
    VirtualClock clock;
    Application::pointer app1 = Application::create(clock, cfg);
    Application::pointer app2 = Application::create(clock, cfg);

    size_t app1Event = 0;
    size_t app2Event = 0;
    size_t timerFired = 0;

    // Fire one event on the app's queue
    app1->getMainIOService().post([&]() { ++app1Event; });
    app1->crank(false);
    CHECK(app1Event == 1);

    // Fire one timer
    VirtualTimer timer(*app1);
    timer.expires_from_now(std::chrono::seconds(1));
    timer.async_wait( [&](asio::error_code const& e) { ++timerFired; });
    app1->crank(false);
    CHECK(timerFired == 1);

    // Queue 2 new events and 1 new timer
    app1->getMainIOService().post([&]() { ++app1Event; });
    app2->getMainIOService().post([&]() { ++app2Event; });
    timer.expires_from_now(std::chrono::seconds(1));
    timer.async_wait( [&](asio::error_code const& e) { ++timerFired; });

    // Check app2's crank fires app2's event but doesn't advance timer
    app2->crank(false);
    CHECK(app2Event == 1);
    CHECK(app1Event == 1);
    CHECK(timerFired == 1);

    // Check app2's final (idle) crank doesn't advance timer
    app2->crank(false);
    CHECK(app2Event == 1);
    CHECK(app1Event == 1);
    CHECK(timerFired == 1);

    // Check app1's crank fires app1's event, not app2 and not timer
    app1->crank(false);
    CHECK(app2Event == 1);
    CHECK(app1Event == 2);
    CHECK(timerFired == 1);

    // Check app1's final (idle) crank fires timer
    app1->crank(false);
    CHECK(app2Event == 1);
    CHECK(app1Event == 2);
    CHECK(timerFired == 2);
}
