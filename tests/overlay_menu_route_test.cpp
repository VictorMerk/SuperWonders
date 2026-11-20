#include "overlay/overlay_internal.h"
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <latch>
#include <thread>

static void check(bool ok, const char* message) {
    if (!ok) { std::fprintf(stderr, "%s\n", message); std::exit(1); }
}
int main() {
    nx::overlay::MenuRouteRequests requests;
    check(requests.take().tab == -1, "no initial route");
    requests.controls(2);
    // No render consumption until the menu's next open frame.
    const auto players = requests.take();
    check(players.tab == 2 && players.controls_section == 2, "paired Players route survives pending frame");
    check(requests.take().tab == -1, "one-shot route");
    requests.controls(2); requests.tab(0);
    auto route = requests.take();
    check(route.tab == 0 && route.controls_section == -1, "generic navigation clears stale subsection");
    requests.controls(2); requests.tab(2);
    route = requests.take();
    check(route.tab == 2 && route.controls_section == 0, "generic Controls opens keyboard intentionally");
    requests.controls(1); requests.clear();
    check(requests.take().tab == -1, "close clears pending subsection");
    requests.controls(0); const auto earlier = requests.take(); requests.controls(2);
    route = requests.take();
    check(earlier.controls_section == 0 && route.controls_section == 2, "later request not erased by prior frame");
    // Deterministic interleaving: an old producer paused before publication,
    // while a new open already selected Players & devices.
    nx::overlay::MenuRouteRequests generations;
    generations.controls(2, 12);
    generations.tab(0, 11);
    generations.controls(1, 11);
    generations.clear(11);
    check(generations.take(11).tab == -1, "stale consumer cannot take newer route");
    route = generations.take(12);
    check(route.tab == 2 && route.controls_section == 2,
          "new open survives old producer, close and render consumer");
    generations.tab(4, 11);
    check(generations.take(12).tab == -1, "high-water survives consumption");
    generations.controls(1, 13);
    check(generations.take(14).tab == -1, "new menu without route discards old pending");
    generations.controls(2, 13);
    check(generations.take(14).tab == -1, "new empty generation rejects delayed old producer");
    generations.controls(2, 15);
    generations.clear(16);
    generations.controls(1, 15);
    check(generations.take(16).tab == -1, "new close rejects delayed old producer");
    generations.controls(2, 17);
    generations.clear(16);
    check(generations.take(16).tab == -1, "stale render preserves future pending");
    route = generations.take(17);
    check(route.tab == 2 && route.controls_section == 2, "future route not consumed or cleared early");
    generations.tab(2, 18);
    route = generations.take(18);
    check(route.tab == 2 && route.controls_section == 0, "tagged generic Controls deliberately opens keyboard");
    nx::overlay::MenuRouteRequests concurrent_generations;
    std::latch old_paused{1}, new_opened{1};
    std::thread delayed_producer([&] {
        old_paused.count_down();
        new_opened.wait();
        concurrent_generations.tab(4, 20);
        concurrent_generations.controls(1, 20);
        concurrent_generations.clear(20);
        check(concurrent_generations.take(20).tab == -1, "paused old render cannot consume new open");
    });
    old_paused.wait();
    concurrent_generations.controls(2, 21);
    new_opened.count_down();
    delayed_producer.join();
    route = concurrent_generations.take(21);
    check(route.tab == 2 && route.controls_section == 2, "new open survives resumed old thread");
    std::atomic<bool> done{false};
    std::thread writer([&] {
        for (int i = 0; i < 100000; ++i) {
            requests.controls(i % 3);
            requests.tab(4);
        }
        done = true;
    });
    do {
        route = requests.take();
        check((route.tab == -1 && route.controls_section == -1) ||
              (route.tab == 2 && route.controls_section >= 0 && route.controls_section < 3) ||
              (route.tab == 4 && route.controls_section == -1), "atomic tab/subsection publication");
    } while (!done.load());
    writer.join();
    std::puts("Overlay menu route requests passed");
}
