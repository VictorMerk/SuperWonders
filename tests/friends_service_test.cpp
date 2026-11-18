// Exercise the real IPC service registry and friends handlers without a game dump.
#include "ipc.h"
#include <cstdio>
#include <cstdlib>
#include <unordered_map>

namespace nx {
bool g_trace = false;
void log(const char*, ...) {}
static std::unordered_map<uint32_t, std::shared_ptr<KObject>> handles;
uint32_t handle_add(std::shared_ptr<KObject> object) {
    static uint32_t next = 1;
    handles.emplace(next, std::move(object));
    return next++;
}
std::shared_ptr<KObject> handle_get(uint32_t handle) {
    auto it = handles.find(handle);
    return it == handles.end() ? nullptr : it->second;
}
void handle_close(uint32_t handle) { handles.erase(handle); }
uint64_t kernel_alloc(uint64_t, uint64_t) { std::abort(); }
bool commit(uint64_t, uint64_t) { std::abort(); }
void vm_set(uint64_t, uint64_t, uint32_t, uint32_t) { std::abort(); }
}

static void check(bool ok, const char* message) {
    if (!ok) { std::fprintf(stderr, "%s\n", message); std::exit(1); }
}

int main() {
    auto service = nx::make_service("friend:u");
    nx::IpcCtx create;
    create.cmd = 0;
    check(service->handle(create) == 0, "friends creation failed");
    check(create.out_objects.size() == 1 && create.out_objects[0], "success must return a child interface");
    nx::IpcCtx unsupported;
    unsupported.cmd = 0xffffffffu;
    check(create.out_objects[0]->handle(unsupported) != 0, "unsupported commands must fail offline");
    check(unsupported.out.empty() && unsupported.out_objects.empty(), "no fabricated online output");
    nx::IpcCtx notification;
    notification.cmd = 1;
    check(service->handle(notification) != 0 && notification.out_objects.empty(),
          "unsupported creator methods must not return empty success");

    // The SDK uses a domain session. Verify that object creation survives the actual CMIF
    // parser/response builder and returns a routable child ID, not just a direct C++ object.
    uint64_t session = 0;
    check(nx::ipc_connect("friend:u", &session) == 0, "connect friends");
    nx::CPU cpu{};
    alignas(16) uint32_t words[64]{};
    auto request = [&] {
        return nx::ipc_request(cpu, uint32_t(session), reinterpret_cast<uint64_t>(words), sizeof words);
    };
    words[0] = 5; words[1] = 8; words[4] = 0x49434653;  // ConvertCurrentObjectToDomain
    check(request() == 0 && words[6] == 0 && words[8] != 0, "convert friends domain");
    const uint32_t root = words[8];
    std::memset(words, 0, sizeof words);
    words[0] = 4; words[1] = 12; words[4] = 0x00100001; words[5] = root;
    words[8] = 0x49434653;  // IServiceCreator::CreateFriendService, command 0
    check(request() == 0 && words[10] == 0 && words[4] == 1, "domain creation returns one object");
    const uint32_t child = words[12];
    check(child != 0 && child != root, "valid child domain ID");
    std::memset(words, 0, sizeof words);
    words[0] = 4; words[1] = 12; words[4] = 0x00100001; words[5] = child;
    words[8] = 0x49434653; words[10] = 0xffffffffu;
    check(request() == 0 && words[8] == 0x4f434653 && words[10] == 0xF601u,
          "child requests return a protocol error without crashing");
    std::puts("friends service tests passed");
}
