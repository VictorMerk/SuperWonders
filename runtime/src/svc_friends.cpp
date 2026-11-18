// Offline friends service. IServiceCreator must return a real child object: an empty successful
// reply leaves the SDK with a null interface (seen in Wonder's GetPlayHistoryRegistrationKey).
// Protocol: https://github.com/switchbrew/libnx/blob/master/nx/source/services/friends.c
#include "ipc.h"

namespace nx {
namespace {

struct OfflineFriendService : Service {
    OfflineFriendService() : Service("IFriendService (offline)") {}

    uint32_t handle(IpcCtx& c) override {
        // No fabricated keys, friend records or online credentials. Unsupported requests must
        // fail rather than report success with uninitialized output structures.
        log("friends: command %u unavailable offline", c.cmd);
        return 0xF601u;  // CMIF: command not implemented
    }
};

struct OfflineFriendCreator : Service {
    OfflineFriendCreator() : Service("friend:u") {}

    uint32_t handle(IpcCtx& c) override {
        if (c.cmd == 0) {
            c.push_object(std::make_shared<OfflineFriendService>());
            return 0u;
        }
        log("friends: creator command %u unavailable offline", c.cmd);
        return 0xF601u;
    }
};

ServiceRegistrar r_friend_u("friend:u", [] { return std::make_shared<OfflineFriendCreator>(); });

}  // namespace
}  // namespace nx
