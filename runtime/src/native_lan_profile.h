#pragma once
#include <nx/module.h>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace nx::native_lan::profile {

// Resolver/unsupported operations deliberately retain distinct return contracts.
// No operation here authorizes internet/account access or changes a game mode.
enum class Operation {
    Initialize, InitializeConfig, Finalize, IsInitialized, Socket, Bind, SendTo, RecvFrom, Poll,
    Close, GetErrno, SetErrno, SetSockOpt, GetSockOpt, Shutdown, GetSockName,
    Recv, Send, Read, Write, Fcntl, UnsupportedSocket, ResolveFailure, HostResolveFailure,
    ResolverGetOption, ResolverSetOption, InetPton, InetNtop,
    NifmInitialize, NifmSetLocalMode, NifmSubmit, NifmSubmitAndWait,
    NifmCancel, NifmAvailable, NifmOnHold, NifmRequestError, NifmIpConfig,
    NifmProxyUnavailable, PreserveOriginal, Count
};
enum class ReturnKind { Signed32, Signed64, Result32, Result22, Void, Bool, Pointer, Preserved };
struct Guard { uint64_t offset=0, hash=0; size_t bytes=32; };
struct Alias {
    std::string_view name;
    Operation operation=Operation::UnsupportedSocket;
    ReturnKind result=ReturnKind::Signed32;
    Guard sdk;
    bool main_import_required=false;
};
struct Profile {
    std::string_view main_build_id, sdk_build_id;
    std::span<const Guard> main_guards;
    std::span<const Alias> aliases;
};
const Profile& product120();
uint64_t hash_prefix(std::span<const uint8_t> bytes) noexcept;

struct ModuleView {
    const ModuleInfo* info=nullptr;
    // Module-relative bytes and loader allocation: table[offset/4].
    std::span<const uint8_t> image;
    std::span<Fn> table;
};
enum class Phase { BeforeRelocations, AfterRelocations };
struct Lookup {
    std::function<Fn(std::string_view)> hle;
    std::function<bool(std::string_view)> thunk_cached;
    std::function<Fn(Operation,ReturnKind)> handler;
};
struct Binding {
    std::string_view name;
    Operation operation;
    ReturnKind result;
    Fn replacement=nullptr, original_hle=nullptr;
    Fn* sdk_entry=nullptr;
    Fn original_sdk=nullptr;
    bool imported_by_main=false, bypassed_sdk_via_hle=false;
};
enum class Status { Disabled, Rejected, Ready };
struct Plan {
    Status status=Status::Disabled;
    std::string error;
    std::vector<Binding> bindings;
    size_t preserved_aliases=0;
};
// Pure preparation. No registry/table writes, guest calls or networking. Both
// module identity and every alias prefix/offset are checked before readiness.
// Every controlled main import must have an exact catalog entry; unsupported
// owned-descriptor operations need an explicit failure handler, never SDK fallthrough.
// Profile strings, module metadata and table allocations must outlive plan/apply.
Plan prepare(bool enabled,const Profile&,const ModuleView& main,const ModuleView& sdk,
             Phase,const Lookup&);

// One transaction for BOTH pre-link HLE aliases and SDK function-table entries.
// Implementations must allocate/validate before publication and leave everything
// unchanged on false/exception. No guest threads may execute during commit.
class Committer {
public:
    virtual ~Committer()=default;
    virtual bool commit(std::span<const Binding>)=0;
};
// Rechecks captured original entries and cached-thunk state before one atomic
// abstract commit. A plan must be applied before relocation creates HLE thunks:
// later registry writes cannot replace a thunk's already captured offline Fn.
bool apply(const Plan&,Phase,const Lookup&,Committer&);
} // namespace nx::native_lan::profile
