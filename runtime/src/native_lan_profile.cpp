// Product120 original NSO prefix identity guards. No game code bytes are embedded.
#include "native_lan_profile.h"
#include <algorithm>
#include <limits>

namespace nx::native_lan::profile {
namespace {
constexpr Guard main_guards[]={
    {0x418634,0xc23229310081f88full,32},
    {0x54fe54,0x207d9a1b879801cfull,32},
    {0x1db7bec,0xd58385cbeedbc991ull,32},
};
constexpr Alias aliases[]={
    {"_ZN2nn4nifm10InitializeEv",Operation::NifmInitialize,ReturnKind::Result32,{0x38d150,0xb687d8e37b6cf6a0ull,32},true},
    {"_ZN2nn4nifm18IsNetworkAvailableEv",Operation::NifmAvailable,ReturnKind::Bool,{0x38e210,0x28b0891b7c57179bull,32},true},
    {"_ZN2nn4nifm19SetLocalNetworkModeEb",Operation::NifmSetLocalMode,ReturnKind::Void,{0x38e290,0xc9df2023a5491efbull,32},true},
    {"_ZN2nn4nifm20CancelNetworkRequestEv",Operation::NifmCancel,ReturnKind::Void,{0x38e190,0x28b6d32afc45c44dull,32},true},
    {"_ZN2nn4nifm20SubmitNetworkRequestEv",Operation::NifmSubmit,ReturnKind::Void,{0x38e0f0,0xa4101eb68f4e4565ull,32},false},
    {"_ZN2nn4nifm22GetCurrentIpConfigInfoEP7in_addrS2_S2_S2_S2_",Operation::NifmIpConfig,ReturnKind::Result32,{0x38dff0,0xbf9f96eaa4644d45ull,32},true},
    {"_ZN2nn4nifm22GetCurrentIpConfigInfoEPNS_6socket6InAddrES3_S3_S3_S3_",Operation::NifmIpConfig,ReturnKind::Result32,{0x38def0,0xbf9f96eaa4644d45ull,32},false},
    {"_ZN2nn4nifm22GetCurrentProxySettingEPNS0_12ProxySettingE",Operation::NifmProxyUnavailable,ReturnKind::Result32,{0x38e9e0,0x7d31f3e8bd57d004ull,32},true},
    {"_ZN2nn4nifm22IsNetworkRequestOnHoldEv",Operation::NifmOnHold,ReturnKind::Bool,{0x38e1d0,0x573fb98a391966abull,32},false},
    {"_ZN2nn4nifm27SubmitNetworkRequestAndWaitEv",Operation::NifmSubmitAndWait,ReturnKind::Void,{0x38e140,0x77ad40860c0e7249ull,32},true},
    {"_ZN2nn4nifm31HandleNetworkRequestErrorResultEv",Operation::NifmRequestError,ReturnKind::Result32,{0x38e250,0xca36db8409aaee8bull,32},true},
    {"_ZN2nn6socket10GetSockOptEiNS0_5LevelENS0_6OptionEPvPj",Operation::GetSockOpt,ReturnKind::Signed32,{0x147660,0x177ee3c99bd3d701ull,32},false},
    {"_ZN2nn6socket10GetSockOptEiiiPvPj",Operation::GetSockOpt,ReturnKind::Signed32,{0x147670,0xd94c5a188feb19a5ull,32},false},
    {"_ZN2nn6socket10InetHtonllEm",Operation::PreserveOriginal,ReturnKind::Preserved,{0x147a20,0x5e2fa47c72ac5f85ull,32},false},
    {"_ZN2nn6socket10InetNtohllEm",Operation::PreserveOriginal,ReturnKind::Preserved,{0x147a50,0xb2cb1d0891b3d459ull,32},false},
    {"_ZN2nn6socket10InitializeEPvmmi",Operation::Initialize,ReturnKind::Result32,{0x147b70,0x9ca3c8f485024d25ull,32},true},
    {"_ZN2nn6socket10InitializeERKNS0_6ConfigE",Operation::InitializeConfig,ReturnKind::Result32,{0x147b80,0x0e62402acf90c0e1ull,32},false},
    {"_ZN2nn6socket10SetSockOptEiNS0_5LevelENS0_6OptionEPKvj",Operation::SetSockOpt,ReturnKind::Signed32,{0x147690,0xb4c6803b466907a9ull,32},true},
    {"_ZN2nn6socket10SetSockOptEiiiPKvj",Operation::SetSockOpt,ReturnKind::Signed32,{0x1476a0,0x0ab759bf394d7985ull,32},false},
    {"_ZN2nn6socket11EventFdReadEiPm",Operation::UnsupportedSocket,ReturnKind::Signed32,{0x1478f0,0xb9e6327379fad941ull,32},true},
    {"_ZN2nn6socket11GAIStrErrorENS0_7AiErrnoE",Operation::PreserveOriginal,ReturnKind::Preserved,{0x147d40,0x2162dda9e7cb0fc1ull,32},true},
    {"_ZN2nn6socket11GAIStrErrorEi",Operation::PreserveOriginal,ReturnKind::Preserved,{0x147d50,0x801a240d428e0fa9ull,32},false},
    {"_ZN2nn6socket11GetAddrInfoEPKcS2_PK8addrinfoPPS3_",Operation::ResolveFailure,ReturnKind::Signed32,{0x147bb0,0x27245d138fb7f87eull,32},true},
    {"_ZN2nn6socket11GetAddrInfoEPKcS2_PK8addrinfoPPS3_PKNS0_14ResolverOptionEm",Operation::ResolveFailure,ReturnKind::Signed32,{0x147bd0,0x93d0ec8810af0cd9ull,32},false},
    {"_ZN2nn6socket11GetAddrInfoEPKcS2_PKNS0_8AddrInfoEPPS3_",Operation::ResolveFailure,ReturnKind::Signed32,{0x147ba0,0x5b326149b10d9a19ull,32},true},
    {"_ZN2nn6socket11GetAddrInfoEPKcS2_PKNS0_8AddrInfoEPPS3_PKNS0_14ResolverOptionEm",Operation::ResolveFailure,ReturnKind::Signed32,{0x147bc0,0x7fb001f72c6187c5ull,32},false},
    {"_ZN2nn6socket11GetPeerNameEiP8sockaddrPj",Operation::UnsupportedSocket,ReturnKind::Signed32,{0x147630,0xafc09809dab8df25ull,32},false},
    {"_ZN2nn6socket11GetPeerNameEiPNS0_8SockAddrEPj",Operation::UnsupportedSocket,ReturnKind::Signed32,{0x147620,0x75436543a2809621ull,32},false},
    {"_ZN2nn6socket11GetSockNameEiP8sockaddrPj",Operation::GetSockName,ReturnKind::Signed32,{0x147650,0xa5c06c9351456345ull,32},false},
    {"_ZN2nn6socket11GetSockNameEiPNS0_8SockAddrEPj",Operation::GetSockName,ReturnKind::Signed32,{0x147640,0xe41ec9985c635c19ull,32},false},
    {"_ZN2nn6socket12EventFdWriteEim",Operation::UnsupportedSocket,ReturnKind::Signed32,{0x147950,0x79e77df4efbbe45full,32},true},
    {"_ZN2nn6socket12FreeAddrInfoEP8addrinfo",Operation::PreserveOriginal,ReturnKind::Preserved,{0x147bf0,0x6d9b687afcf3eccaull,32},true},
    {"_ZN2nn6socket12FreeAddrInfoEPNS0_8AddrInfoE",Operation::PreserveOriginal,ReturnKind::Preserved,{0x147be0,0x0e62402acf90c0e1ull,32},true},
    {"_ZN2nn6socket12GetLastErrnoEv",Operation::GetErrno,ReturnKind::Signed32,{0x147a70,0xf1a84682b1c62f09ull,32},true},
    {"_ZN2nn6socket12GetLastErrorEv",Operation::GetErrno,ReturnKind::Signed32,{0x147a60,0xd9b5e3fccbb5c8e1ull,32},true},
    {"_ZN2nn6socket12SetLastErrnoEi",Operation::SetErrno,ReturnKind::Void,{0x147a90,0x7b143bdbd6dde725ull,32},false},
    {"_ZN2nn6socket12SetLastErrorENS0_5ErrnoE",Operation::SetErrno,ReturnKind::Void,{0x147a80,0x409709159ea59e21ull,32},false},
    {"_ZN2nn6socket12SocketExemptENS0_6FamilyENS0_4TypeENS0_8ProtocolE",Operation::UnsupportedSocket,ReturnKind::Signed32,{0x147710,0x7da9987403fd85a5ull,32},false},
    {"_ZN2nn6socket12SocketExemptEiii",Operation::UnsupportedSocket,ReturnKind::Signed32,{0x147720,0x7da9987403fd85a5ull,32},false},
    {"_ZN2nn6socket13GetHostByAddrEPKvji",Operation::HostResolveFailure,ReturnKind::Pointer,{0x147cd0,0x5e6787f2920d6f84ull,32},false},
    {"_ZN2nn6socket13GetHostByAddrEPKvjiPKNS0_14ResolverOptionEm",Operation::HostResolveFailure,ReturnKind::Pointer,{0x147cf0,0xf1a84682b1c62f09ull,32},false},
    {"_ZN2nn6socket13GetHostByNameEPKc",Operation::HostResolveFailure,ReturnKind::Pointer,{0x147c90,0xcecf117a45bee03cull,32},false},
    {"_ZN2nn6socket13GetHostByNameEPKcPKNS0_14ResolverOptionEm",Operation::HostResolveFailure,ReturnKind::Pointer,{0x147cb0,0xad92dfecda7a08f4ull,32},false},
    {"_ZN2nn6socket13IsInitializedEv",Operation::IsInitialized,ReturnKind::Bool,{0x147b60,0x93d0ec8810af0cd9ull,32},false},
    {"_ZN2nn6socket15DuplicateSocketEim",Operation::UnsupportedSocket,ReturnKind::Signed32,{0x147d90,0x0c4a267faf510845ull,32},false},
    {"_ZN2nn6socket17ResolverGetOptionEPNS0_14ResolverOptionENS0_17ResolverOptionKeyE",Operation::PreserveOriginal,ReturnKind::Preserved,{0x147d60,0x884402c2babae501ull,32},false},
    {"_ZN2nn6socket17ResolverSetOptionERKNS0_14ResolverOptionE",Operation::PreserveOriginal,ReturnKind::Preserved,{0x147d70,0x93d0ec8810af0cd9ull,32},false},
    {"_ZN2nn6socket4BindEiPK8sockaddrj",Operation::Bind,ReturnKind::Signed32,{0x1475f0,0x0c4a267faf510845ull,32},true},
    {"_ZN2nn6socket4BindEiPKNS0_8SockAddrEj",Operation::Bind,ReturnKind::Signed32,{0x1475e0,0x0e62402acf90c0e1ull,32},true},
    {"_ZN2nn6socket4PollEP6pollfdmi",Operation::Poll,ReturnKind::Signed32,{0x147790,0x8d6ac2e65fcc285cull,32},true},
    {"_ZN2nn6socket4PollEPNS0_6PollFdEmi",Operation::Poll,ReturnKind::Signed32,{0x147780,0x075a7230349c5f89ull,32},false},
    {"_ZN2nn6socket4ReadEiPvm",Operation::Read,ReturnKind::Signed64,{0x147740,0x7da9987403fd85a5ull,32},false},
    {"_ZN2nn6socket4RecvEiPvmNS0_7MsgFlagE",Operation::Recv,ReturnKind::Signed64,{0x147540,0xaf726d6a58886419ull,32},false},
    {"_ZN2nn6socket4RecvEiPvmi",Operation::Recv,ReturnKind::Signed64,{0x147550,0xafa466739d3c9c05ull,32},false},
    {"_ZN2nn6socket4SendEiPKvmNS0_7MsgFlagE",Operation::Send,ReturnKind::Signed64,{0x147580,0x801a240d428e0fa9ull,32},false},
    {"_ZN2nn6socket4SendEiPKvmi",Operation::Send,ReturnKind::Signed64,{0x147590,0x16858b8c702c7145ull,32},false},
    {"_ZN2nn6socket5CloseEi",Operation::Close,ReturnKind::Signed32,{0x147750,0x7da9987403fd85a5ull,32},true},
    {"_ZN2nn6socket5FcntlEiNS0_12FcntlCommandEz",Operation::Fcntl,ReturnKind::Signed32,{0x1477a0,0x87569f77e2654542ull,32},false},
    {"_ZN2nn6socket5FcntlEiiz",Operation::Fcntl,ReturnKind::Signed32,{0x147840,0x87569f77e2654542ull,32},false},
    {"_ZN2nn6socket5IoctlEiNS0_12IoctlCommandEPvm",Operation::UnsupportedSocket,ReturnKind::Signed32,{0x147b20,0x801a240d428e0fa9ull,32},false},
    {"_ZN2nn6socket5IoctlEijPvm",Operation::UnsupportedSocket,ReturnKind::Signed32,{0x147b30,0x16858b8c702c7145ull,32},false},
    {"_ZN2nn6socket5WriteEiPKvm",Operation::Write,ReturnKind::Signed64,{0x147730,0x7da9987403fd85a5ull,32},false},
    {"_ZN2nn6socket6AcceptEiP8sockaddrPj",Operation::UnsupportedSocket,ReturnKind::Signed32,{0x1475d0,0x9ca3c8f485024d25ull,32},false},
    {"_ZN2nn6socket6AcceptEiPNS0_8SockAddrEPj",Operation::UnsupportedSocket,ReturnKind::Signed32,{0x1475c0,0x93d0ec8810af0cd9ull,32},false},
    {"_ZN2nn6socket6ListenEii",Operation::UnsupportedSocket,ReturnKind::Signed32,{0x147680,0xd94c5a188feb19a5ull,32},false},
    {"_ZN2nn6socket6SelectEiP6fd_setS2_S2_P7timeval",Operation::UnsupportedSocket,ReturnKind::Signed32,{0x147770,0x2be04c0d7e1e7185ull,32},false},
    {"_ZN2nn6socket6SelectEiPNS0_5FdSetES2_S2_PNS0_7TimeValE",Operation::UnsupportedSocket,ReturnKind::Signed32,{0x147760,0xef680faa4e8bf961ull,32},false},
    {"_ZN2nn6socket6SendToEiPKvmNS0_7MsgFlagEPKNS0_8SockAddrEj",Operation::SendTo,ReturnKind::Signed64,{0x1475a0,0x884402c2babae501ull,32},true},
    {"_ZN2nn6socket6SendToEiPKvmiPK8sockaddrj",Operation::SendTo,ReturnKind::Signed64,{0x1475b0,0x7fb001f72c6187c5ull,32},true},
    {"_ZN2nn6socket6SocketENS0_6FamilyENS0_4TypeENS0_8ProtocolE",Operation::Socket,ReturnKind::Signed32,{0x1476f0,0x47882cbf9f162559ull,32},true},
    {"_ZN2nn6socket6SocketEiii",Operation::Socket,ReturnKind::Signed32,{0x147700,0x7da9987403fd85a5ull,32},true},
    {"_ZN2nn6socket6detail10GetSockOptEiNS0_5LevelENS0_6OptionEPvPj",Operation::GetSockOpt,ReturnKind::Signed32,{0x14ed70,0x8806d7cb8701cc8full,32},false},
    {"_ZN2nn6socket6detail10GetSockOptEiiiPvPj",Operation::GetSockOpt,ReturnKind::Signed32,{0x14eef0,0x42281f3b94f85833ull,32},false},
    {"_ZN2nn6socket6detail10InetHtonllEm",Operation::PreserveOriginal,ReturnKind::Preserved,{0x151ef0,0x69d1991815a90397ull,32},false},
    {"_ZN2nn6socket6detail10InetNtohllEm",Operation::PreserveOriginal,ReturnKind::Preserved,{0x151f20,0xd296335c87bc0957ull,32},false},
    {"_ZN2nn6socket6detail10InitializeEPvmmi",Operation::Initialize,ReturnKind::Result32,{0x14bf40,0xcb7364fe51ff7c55ull,32},false},
    {"_ZN2nn6socket6detail10InitializeERKNS0_6ConfigE",Operation::InitializeConfig,ReturnKind::Result32,{0x14c310,0xe1f8c19466539535ull,32},false},
    {"_ZN2nn6socket6detail10SetSockOptEiNS0_5LevelENS0_6OptionEPKvj",Operation::SetSockOpt,ReturnKind::Signed32,{0x14f4a0,0x33bfeef54f3226d3ull,32},false},
    {"_ZN2nn6socket6detail10SetSockOptEiiiPKvj",Operation::SetSockOpt,ReturnKind::Signed32,{0x14f600,0x139ac66af655ee05ull,32},false},
    {"_ZN2nn6socket6detail11GAIStrErrorENS0_7AiErrnoE",Operation::PreserveOriginal,ReturnKind::Preserved,{0x151f90,0x3f589b9d19062391ull,32},false},
    {"_ZN2nn6socket6detail11GAIStrErrorEi",Operation::PreserveOriginal,ReturnKind::Preserved,{0x151fa0,0x262840e4ed4db1b5ull,32},false},
    {"_ZN2nn6socket6detail11GetAddrInfoEPKcS3_PK8addrinfoPPS4_PKNS0_14ResolverOptionEm",Operation::ResolveFailure,ReturnKind::Signed32,{0x151fe0,0x038b14cb0903d7ffull,32},false},
    {"_ZN2nn6socket6detail11GetAddrInfoEPKcS3_PKNS0_8AddrInfoEPPS4_PKNS0_14ResolverOptionEm",Operation::ResolveFailure,ReturnKind::Signed32,{0x151fd0,0x262840e4ed4db1b5ull,32},false},
    {"_ZN2nn6socket6detail11GetPeerNameEiP8sockaddrPj",Operation::UnsupportedSocket,ReturnKind::Signed32,{0x14e910,0x42281f3b94f85833ull,32},false},
    {"_ZN2nn6socket6detail11GetPeerNameEiPNS0_8SockAddrEPj",Operation::UnsupportedSocket,ReturnKind::Signed32,{0x14e7a0,0x42281f3b94f85833ull,32},false},
    {"_ZN2nn6socket6detail11GetSockNameEiP8sockaddrPj",Operation::GetSockName,ReturnKind::Signed32,{0x14ec00,0x42281f3b94f85833ull,32},false},
    {"_ZN2nn6socket6detail11GetSockNameEiPNS0_8SockAddrEPj",Operation::GetSockName,ReturnKind::Signed32,{0x14ea80,0x42281f3b94f85833ull,32},false},
    {"_ZN2nn6socket6detail12FreeAddrInfoEP8addrinfo",Operation::PreserveOriginal,ReturnKind::Preserved,{0x152030,0x44d604ed5b569017ull,32},false},
    {"_ZN2nn6socket6detail12FreeAddrInfoEPNS0_8AddrInfoE",Operation::PreserveOriginal,ReturnKind::Preserved,{0x152020,0x4f2d0d885685f240ull,32},false},
    {"_ZN2nn6socket6detail12GetLastErrnoEv",Operation::GetErrno,ReturnKind::Signed32,{0x14c460,0x20ef6cd47f131454ull,32},false},
    {"_ZN2nn6socket6detail12GetLastErrorEv",Operation::GetErrno,ReturnKind::Signed32,{0x14c420,0x20ef6cd47f131454ull,32},false},
    {"_ZN2nn6socket6detail12SetLastErrnoEi",Operation::SetErrno,ReturnKind::Void,{0x14c3e0,0x6c6800223c21fa6aull,32},false},
    {"_ZN2nn6socket6detail12SetLastErrorENS0_5ErrnoE",Operation::SetErrno,ReturnKind::Void,{0x14ba80,0x73fe68245ebf774aull,32},false},
    {"_ZN2nn6socket6detail12SocketExemptENS0_6FamilyENS0_4TypeENS0_8ProtocolE",Operation::UnsupportedSocket,ReturnKind::Signed32,{0x150e30,0x9ed1b11c0c5e23d6ull,32},false},
    {"_ZN2nn6socket6detail12SocketExemptEiii",Operation::UnsupportedSocket,ReturnKind::Signed32,{0x150f40,0x504a32e1bab665f5ull,32},false},
    {"_ZN2nn6socket6detail13GetHostByAddrEPKvjiPKNS0_14ResolverOptionEm",Operation::HostResolveFailure,ReturnKind::Pointer,{0x151f60,0x56647dc2a62ba8f5ull,32},false},
    {"_ZN2nn6socket6detail13GetHostByNameEPKcPKNS0_14ResolverOptionEm",Operation::HostResolveFailure,ReturnKind::Pointer,{0x151f40,0x56647dc2a62ba8f5ull,32},false},
    {"_ZN2nn6socket6detail13IsInitializedEv",Operation::IsInitialized,ReturnKind::Bool,{0x14b950,0xbfbfd4c3e8029b74ull,32},false},
    {"_ZN2nn6socket6detail15DuplicateSocketEim",Operation::UnsupportedSocket,ReturnKind::Signed32,{0x1515e0,0xc55535ed9e10ea75ull,32},false},
    {"_ZN2nn6socket6detail17ResolverGetOptionEPNS0_14ResolverOptionEj",Operation::PreserveOriginal,ReturnKind::Preserved,{0x151fb0,0x262840e4ed4db1b5ull,32},false},
    {"_ZN2nn6socket6detail17ResolverSetOptionERKNS0_14ResolverOptionE",Operation::PreserveOriginal,ReturnKind::Preserved,{0x151fc0,0x262840e4ed4db1b5ull,32},false},
    {"_ZN2nn6socket6detail4BindEiPK8sockaddrj",Operation::Bind,ReturnKind::Signed32,{0x14e3e0,0xb8ac3d85ea7fdc86ull,32},false},
    {"_ZN2nn6socket6detail4BindEiPKNS0_8SockAddrEj",Operation::Bind,ReturnKind::Signed32,{0x14e2a0,0xb8ac3d85ea7fdc86ull,32},false},
    {"_ZN2nn6socket6detail4PollEP6pollfdmi",Operation::Poll,ReturnKind::Signed32,{0x150ab0,0xa31e0fa244772f37ull,32},false},
    {"_ZN2nn6socket6detail4PollEPNS0_6PollFdEmi",Operation::Poll,ReturnKind::Signed32,{0x150950,0xa31e0fa244772f37ull,32},false},
    {"_ZN2nn6socket6detail4ReadEiPvm",Operation::Read,ReturnKind::Signed64,{0x14fbe0,0x622244a547fdabe6ull,32},false},
    {"_ZN2nn6socket6detail4RecvEiPvmNS0_7MsgFlagE",Operation::Recv,ReturnKind::Signed64,{0x14c4a0,0xe2d46ebe9d864963ull,32},false},
    {"_ZN2nn6socket6detail4RecvEiPvmi",Operation::Recv,ReturnKind::Signed64,{0x14c610,0xe2d46ebe9d864963ull,32},false},
    {"_ZN2nn6socket6detail4SendEiPKvmNS0_7MsgFlagE",Operation::Send,ReturnKind::Signed64,{0x14d4b0,0xf8747030c1b74623ull,32},false},
    {"_ZN2nn6socket6detail4SendEiPKvmi",Operation::Send,ReturnKind::Signed64,{0x14d620,0xf8747030c1b74623ull,32},false},
    {"_ZN2nn6socket6detail5CloseEi",Operation::Close,ReturnKind::Signed32,{0x151330,0xc55535ed9e10ea75ull,32},false},
    {"_ZN2nn6socket6detail5FcntlEiNS0_12FcntlCommandEz",Operation::Fcntl,ReturnKind::Signed32,{0x14f180,0x8cf3fd776ebbd93dull,32},false},
    {"_ZN2nn6socket6detail5FcntlEiiz",Operation::Fcntl,ReturnKind::Signed32,{0x14f310,0x8cf3fd776ebbd93dull,32},false},
    {"_ZN2nn6socket6detail5IoctlEiNS0_12IoctlCommandEPvm",Operation::UnsupportedSocket,ReturnKind::Signed32,{0x150380,0x955a225c75d7a62eull,32},false},
    {"_ZN2nn6socket6detail5IoctlEijPvm",Operation::UnsupportedSocket,ReturnKind::Signed32,{0x14fda0,0xbf0ec368768f862eull,32},false},
    {"_ZN2nn6socket6detail5WriteEiPKvm",Operation::Write,ReturnKind::Signed64,{0x14fa80,0x622244a547fdabe6ull,32},false},
    {"_ZN2nn6socket6detail6AcceptEiP8sockaddrPj",Operation::UnsupportedSocket,ReturnKind::Signed32,{0x14e110,0x42281f3b94f85833ull,32},false},
    {"_ZN2nn6socket6detail6AcceptEiPNS0_8SockAddrEPj",Operation::UnsupportedSocket,ReturnKind::Signed32,{0x14df80,0x4e8b0f7e3056c9b3ull,32},false},
    {"_ZN2nn6socket6detail6ListenEii",Operation::UnsupportedSocket,ReturnKind::Signed32,{0x14f070,0x0e31576be1036255ull,32},false},
    {"_ZN2nn6socket6detail6SelectEiP6fd_setS3_S3_P7timeval",Operation::UnsupportedSocket,ReturnKind::Signed32,{0x1501a0,0xba58238bc737a39eull,32},false},
    {"_ZN2nn6socket6detail6SelectEiPNS0_5FdSetES3_S3_PNS0_7TimeValE",Operation::UnsupportedSocket,ReturnKind::Signed32,{0x14ffc0,0x3a1e8284ae7e6e9eull,32},false},
    {"_ZN2nn6socket6detail6SendToEiPKvmNS0_7MsgFlagEPKNS0_8SockAddrEj",Operation::SendTo,ReturnKind::Signed64,{0x14d790,0x847b7072b333ddf0ull,32},false},
    {"_ZN2nn6socket6detail6SendToEiPKvmiPK8sockaddrj",Operation::SendTo,ReturnKind::Signed64,{0x14d930,0x847b7072b333ddf0ull,32},false},
    {"_ZN2nn6socket6detail6SocketENS0_6FamilyENS0_4TypeENS0_8ProtocolE",Operation::Socket,ReturnKind::Signed32,{0x150c10,0x9ed1b11c0c5e23d6ull,32},false},
    {"_ZN2nn6socket6detail6SocketEiii",Operation::Socket,ReturnKind::Signed32,{0x150d20,0x504a32e1bab665f5ull,32},false},
    {"_ZN2nn6socket6detail7ConnectEiPK8sockaddrj",Operation::UnsupportedSocket,ReturnKind::Signed32,{0x14e660,0xb8ac3d85ea7fdc86ull,32},false},
    {"_ZN2nn6socket6detail7ConnectEiPKNS0_8SockAddrEj",Operation::UnsupportedSocket,ReturnKind::Signed32,{0x14e520,0xb8ac3d85ea7fdc86ull,32},false},
    {"_ZN2nn6socket6detail7EventFdEmi",Operation::UnsupportedSocket,ReturnKind::Signed32,{0x1516f0,0xc555b5ed9e11c3f5ull,32},false},
    {"_ZN2nn6socket6detail7RecvMsgEiP6msghdri",Operation::UnsupportedSocket,ReturnKind::Signed64,{0x14cf70,0x7ef7a1817a811abeull,32},false},
    {"_ZN2nn6socket6detail7RecvMsgEiPNS0_6MsgHdrENS0_7MsgFlagE",Operation::UnsupportedSocket,ReturnKind::Signed64,{0x14cb00,0x7ef7a1817a811abeull,32},false},
    {"_ZN2nn6socket6detail7SendMsgEiPK6msghdri",Operation::UnsupportedSocket,ReturnKind::Signed64,{0x14dee0,0x8d3b32b78117b16cull,32},false},
    {"_ZN2nn6socket6detail7SendMsgEiPKNS0_6MsgHdrENS0_7MsgFlagE",Operation::UnsupportedSocket,ReturnKind::Signed64,{0x14dad0,0x8d3b32b78117b16cull,32},false},
    {"_ZN2nn6socket6detail8FinalizeEv",Operation::Finalize,ReturnKind::Result32,{0x14c320,0x6c6800223c21fa6aull,32},false},
    {"_ZN2nn6socket6detail8InetNtopENS0_6FamilyEPKvPcj",Operation::InetNtop,ReturnKind::Pointer,{0x151e50,0xd83bf0fc2afd5ef5ull,32},false},
    {"_ZN2nn6socket6detail8InetNtopEiPKvPcj",Operation::InetNtop,ReturnKind::Pointer,{0x151e60,0xd83bf0fc2afd5ef5ull,32},false},
    {"_ZN2nn6socket6detail8InetPtonENS0_6FamilyEPKcPv",Operation::InetPton,ReturnKind::Signed32,{0x151e70,0xd83bf0fc2afd5ef5ull,32},false},
    {"_ZN2nn6socket6detail8InetPtonEiPKcPv",Operation::InetPton,ReturnKind::Signed32,{0x151e80,0xd83bf0fc2afd5ef5ull,32},false},
    {"_ZN2nn6socket6detail8RecvFromEiPvmNS0_7MsgFlagEPNS0_8SockAddrEPj",Operation::RecvFrom,ReturnKind::Signed64,{0x14c780,0x4ce6fd9709d1a3c3ull,32},false},
    {"_ZN2nn6socket6detail8RecvFromEiPvmiP8sockaddrPj",Operation::RecvFrom,ReturnKind::Signed64,{0x14c940,0x4ce6fd9709d1a3c3ull,32},false},
    {"_ZN2nn6socket6detail8ShutdownEiNS0_14ShutdownMethodE",Operation::Shutdown,ReturnKind::Signed32,{0x14f750,0x0e31576be1036255ull,32},false},
    {"_ZN2nn6socket6detail8ShutdownEii",Operation::Shutdown,ReturnKind::Signed32,{0x14f860,0x0e31576be1036255ull,32},false},
    {"_ZN2nn6socket6detail9HStrErrorENS0_6HErrnoE",Operation::PreserveOriginal,ReturnKind::Preserved,{0x151f70,0x1ed70d6d5ef8f9a9ull,32},false},
    {"_ZN2nn6socket6detail9HStrErrorEi",Operation::PreserveOriginal,ReturnKind::Preserved,{0x151f80,0x904e01b754c64b25ull,32},false},
    {"_ZN2nn6socket6detail9InetHtonlEj",Operation::PreserveOriginal,ReturnKind::Preserved,{0x151ee0,0x248fee76c9f5cf5dull,32},false},
    {"_ZN2nn6socket6detail9InetHtonsEt",Operation::PreserveOriginal,ReturnKind::Preserved,{0x151ed0,0xe35d77982cfa11f7ull,32},false},
    {"_ZN2nn6socket6detail9InetNtohlEj",Operation::PreserveOriginal,ReturnKind::Preserved,{0x151f10,0x248fee76c9f5cf5dull,32},false},
    {"_ZN2nn6socket6detail9InetNtohsEt",Operation::PreserveOriginal,ReturnKind::Preserved,{0x151f00,0xe35d77982cfa11f7ull,32},false},
    {"_ZN2nn6socket7ConnectEiPK8sockaddrj",Operation::UnsupportedSocket,ReturnKind::Signed32,{0x147610,0x0384ee0d57f22265ull,32},false},
    {"_ZN2nn6socket7ConnectEiPKNS0_8SockAddrEj",Operation::UnsupportedSocket,ReturnKind::Signed32,{0x147600,0xe7c44ca265cef649ull,32},false},
    {"_ZN2nn6socket7EventFdEmNS0_12EventFdFlagsE",Operation::UnsupportedSocket,ReturnKind::Signed32,{0x1478e0,0x203f2ee4b8302c0dull,32},true},
    {"_ZN2nn6socket7RecvMsgEiP6msghdri",Operation::UnsupportedSocket,ReturnKind::Signed64,{0x147ab0,0xafa466739d3c9c05ull,32},false},
    {"_ZN2nn6socket7RecvMsgEiPNS0_6MsgHdrENS0_7MsgFlagE",Operation::UnsupportedSocket,ReturnKind::Signed64,{0x147aa0,0xaf726d6a58886419ull,32},false},
    {"_ZN2nn6socket7SendMsgEiPK6msghdri",Operation::UnsupportedSocket,ReturnKind::Signed64,{0x147af0,0xa49ffdea8c1021a5ull,32},false},
    {"_ZN2nn6socket7SendMsgEiPKNS0_6MsgHdrENS0_7MsgFlagE",Operation::UnsupportedSocket,ReturnKind::Signed64,{0x147ae0,0x2162dda9e7cb0fc1ull,32},false},
    {"_ZN2nn6socket8FinalizeEv",Operation::Finalize,ReturnKind::Result32,{0x147b90,0x8631a72d1749e8f2ull,32},true},
    {"_ZN2nn6socket8InetNtopENS0_6FamilyEPKvPcj",Operation::InetNtop,ReturnKind::Pointer,{0x1479a0,0x80d79c25a389c701ull,32},false},
    {"_ZN2nn6socket8InetNtopEiPKvPcj",Operation::InetNtop,ReturnKind::Pointer,{0x1479b0,0xc39a2e6b0d14e925ull,32},true},
    {"_ZN2nn6socket8InetPtonENS0_6FamilyEPKcPv",Operation::InetPton,ReturnKind::Signed32,{0x147980,0xce6c9dead98d2b99ull,32},false},
    {"_ZN2nn6socket8InetPtonEiPKcPv",Operation::InetPton,ReturnKind::Signed32,{0x147990,0x0f1924ef58fb5345ull,32},true},
    {"_ZN2nn6socket8RecvFromEiPvmNS0_7MsgFlagEPNS0_8SockAddrEPj",Operation::RecvFrom,ReturnKind::Signed64,{0x147560,0x2162dda9e7cb0fc1ull,32},true},
    {"_ZN2nn6socket8RecvFromEiPvmiP8sockaddrPj",Operation::RecvFrom,ReturnKind::Signed64,{0x147570,0xa49ffdea8c1021a5ull,32},true},
    {"_ZN2nn6socket8ShutdownEiNS0_14ShutdownMethodE",Operation::Shutdown,ReturnKind::Signed32,{0x1476c0,0x7c75d0f583dbed41ull,32},false},
    {"_ZN2nn6socket8ShutdownEii",Operation::Shutdown,ReturnKind::Signed32,{0x1476d0,0x73e1d029f5829005ull,32},true},
    {"_ZN2nn6socket9HStrErrorENS0_6HErrnoE",Operation::PreserveOriginal,ReturnKind::Preserved,{0x147d20,0xaf726d6a58886419ull,32},false},
    {"_ZN2nn6socket9HStrErrorEi",Operation::PreserveOriginal,ReturnKind::Preserved,{0x147d30,0xafa466739d3c9c05ull,32},false},
    {"_ZN2nn6socket9InetHtonlEj",Operation::PreserveOriginal,ReturnKind::Preserved,{0x147a10,0x5e2fa47c72ac5f85ull,32},true},
    {"_ZN2nn6socket9InetHtonsEt",Operation::PreserveOriginal,ReturnKind::Preserved,{0x147a00,0x5e2fa47c72ac5f85ull,32},true},
    {"_ZN2nn6socket9InetNtohlEj",Operation::PreserveOriginal,ReturnKind::Preserved,{0x147a40,0x5e2fa47c72ac5f85ull,32},true},
    {"_ZN2nn6socket9InetNtohsEt",Operation::PreserveOriginal,ReturnKind::Preserved,{0x147a30,0x5e2fa47c72ac5f85ull,32},true},
    {"nnsocketAccept",Operation::UnsupportedSocket,ReturnKind::Signed32,{0x147ee0,0x8352259c42fbb3edull,32},true},
    {"nnsocketBind",Operation::Bind,ReturnKind::Signed32,{0x147ef0,0x46c5c60e758f2a8dull,32},true},
    {"nnsocketClose",Operation::Close,ReturnKind::Signed32,{0x147fb0,0xc0c6e66876f13ec2ull,32},true},
    {"nnsocketConnect",Operation::UnsupportedSocket,ReturnKind::Signed32,{0x147f00,0x7051881d2ac1650dull,32},true},
    {"nnsocketDuplicateSocket",Operation::UnsupportedSocket,ReturnKind::Signed32,{0x148310,0xf780b31d91dd02fbull,32},false},
    {"nnsocketEventFd",Operation::UnsupportedSocket,ReturnKind::Signed32,{0x148320,0xb3133623e797cdc5ull,32},false},
    {"nnsocketEventFdRead",Operation::UnsupportedSocket,ReturnKind::Signed32,{0x148330,0x31dd1a9a5b787f71ull,32},false},
    {"nnsocketEventFdWrite",Operation::UnsupportedSocket,ReturnKind::Signed32,{0x148340,0xcb1f98748862e213ull,32},false},
    {"nnsocketFcntl",Operation::Fcntl,ReturnKind::Signed32,{0x147fe0,0x87569f77e2654542ull,32},true},
    {"nnsocketFinalize",Operation::Finalize,ReturnKind::Result22,{0x1481a0,0xeaa817f96e921451ull,32},false},
    {"nnsocketFreeAddrInfo",Operation::PreserveOriginal,ReturnKind::Preserved,{0x148260,0xaf1170ec2d0ac321ull,32},true},
    {"nnsocketGAIStrError",Operation::PreserveOriginal,ReturnKind::Preserved,{0x1482f0,0x30b7752287b70f31ull,32},true},
    {"nnsocketGetAddrInfo",Operation::ResolveFailure,ReturnKind::Signed32,{0x1481c0,0xcc66a6c07f62d832ull,32},true},
    {"nnsocketGetAddrInfoWithOptions",Operation::ResolveFailure,ReturnKind::Signed32,{0x1481d0,0xf67a30838ec19ae4ull,32},false},
    {"nnsocketGetHostByAddr",Operation::HostResolveFailure,ReturnKind::Pointer,{0x148200,0x249fe8b56e7af2f4ull,32},false},
    {"nnsocketGetHostByAddrWithOptions",Operation::HostResolveFailure,ReturnKind::Pointer,{0x148210,0x8c9d5229affa18b3ull,32},false},
    {"nnsocketGetHostByName",Operation::HostResolveFailure,ReturnKind::Pointer,{0x1481e0,0xb3c6592dc4ee3890ull,32},false},
    {"nnsocketGetHostByNameWithOptions",Operation::HostResolveFailure,ReturnKind::Pointer,{0x1481f0,0x19a6ac40f6af01fbull,32},true},
    {"nnsocketGetLastErrno",Operation::GetErrno,ReturnKind::Signed32,{0x148120,0xa27be8dd7677a8c1ull,32},true},
    {"nnsocketGetPeerName",Operation::UnsupportedSocket,ReturnKind::Signed32,{0x147f10,0x15b231944110faedull,32},true},
    {"nnsocketGetSockName",Operation::GetSockName,ReturnKind::Signed32,{0x147f20,0xf8790fc581909badull,32},true},
    {"nnsocketGetSockOpt",Operation::GetSockOpt,ReturnKind::Signed32,{0x147f30,0x5e3fdcbdc70cc69aull,32},true},
    {"nnsocketHStrError",Operation::PreserveOriginal,ReturnKind::Preserved,{0x148280,0xfda8b8a06ad3f152ull,32},false},
    {"nnsocketInetHtonl",Operation::PreserveOriginal,ReturnKind::Preserved,{0x1480d0,0xa48315ee86ae40f5ull,32},true},
    {"nnsocketInetHtonll",Operation::PreserveOriginal,ReturnKind::Preserved,{0x1480e0,0xd8e1477d0858bde9ull,32},false},
    {"nnsocketInetHtons",Operation::PreserveOriginal,ReturnKind::Preserved,{0x1480c0,0xfee8a0a58b627651ull,32},true},
    {"nnsocketInetNtohl",Operation::PreserveOriginal,ReturnKind::Preserved,{0x148100,0x427b1d57a260bb65ull,32},true},
    {"nnsocketInetNtohll",Operation::PreserveOriginal,ReturnKind::Preserved,{0x148110,0x9d3bf9976250f499ull,32},false},
    {"nnsocketInetNtohs",Operation::PreserveOriginal,ReturnKind::Preserved,{0x1480f0,0x803d857b79aa4c5dull,32},true},
    {"nnsocketInetNtop",Operation::InetNtop,ReturnKind::Pointer,{0x148090,0x8f20576979daabadull,32},false},
    {"nnsocketInetPton",Operation::InetPton,ReturnKind::Signed32,{0x148080,0x2b645d41c3e72b6dull,32},false},
    {"nnsocketInitialize",Operation::Initialize,ReturnKind::Result22,{0x148180,0x93be8701a4e73455ull,32},false},
    {"nnsocketIoctl",Operation::UnsupportedSocket,ReturnKind::Signed32,{0x148160,0x7cbbaef7af68fe8dull,32},false},
    {"nnsocketListen",Operation::UnsupportedSocket,ReturnKind::Signed32,{0x147f40,0xfc491e7393974f1eull,32},true},
    {"nnsocketPoll",Operation::Poll,ReturnKind::Signed32,{0x147fd0,0x788a0742a73e3196ull,32},true},
    {"nnsocketRead",Operation::Read,ReturnKind::Signed64,{0x147fa0,0xab1570fbae230a19ull,32},false},
    {"nnsocketRecv",Operation::Recv,ReturnKind::Signed64,{0x147ea0,0x0c012b14eb0e1e20ull,32},true},
    {"nnsocketRecvFrom",Operation::RecvFrom,ReturnKind::Signed64,{0x147eb0,0x121969e071b4328dull,32},false},
    {"nnsocketRecvMsg",Operation::UnsupportedSocket,ReturnKind::Signed64,{0x148140,0xd92bac5671841435ull,32},true},
    {"nnsocketResolverGetOption",Operation::PreserveOriginal,ReturnKind::Preserved,{0x148290,0xebd93de47b98a62eull,32},true},
    {"nnsocketResolverSetOption",Operation::PreserveOriginal,ReturnKind::Preserved,{0x1482c0,0x7d0811943e90dea6ull,32},true},
    {"nnsocketSelect",Operation::UnsupportedSocket,ReturnKind::Signed32,{0x147fc0,0x342995b41b46114dull,32},false},
    {"nnsocketSend",Operation::Send,ReturnKind::Signed64,{0x147ec0,0x3ba52bef26e66d0dull,32},true},
    {"nnsocketSendMsg",Operation::UnsupportedSocket,ReturnKind::Signed64,{0x148150,0x615333fec8534ad5ull,32},true},
    {"nnsocketSendTo",Operation::SendTo,ReturnKind::Signed64,{0x147ed0,0xa08b476b027c132dull,32},false},
    {"nnsocketSetLastErrno",Operation::SetErrno,ReturnKind::Void,{0x148130,0x722a1d697b3b8b8dull,32},true},
    {"nnsocketSetSockOpt",Operation::SetSockOpt,ReturnKind::Signed32,{0x147f50,0x5641fd861ed6f6f6ull,32},true},
    {"nnsocketShutdown",Operation::Shutdown,ReturnKind::Signed32,{0x147f70,0xa71c558ed29f2f49ull,32},true},
    {"nnsocketSocket",Operation::Socket,ReturnKind::Signed32,{0x147f80,0xbb6ea16b8951be6eull,32},true},
    {"nnsocketWrite",Operation::Write,ReturnKind::Signed64,{0x147f90,0xab1570fbae230a19ull,32},false},
};
const Profile kProduct120{"ff773e90972d544eb79406eaa65396d53c43efb9000000000000000000000000","e4bd58cac28cf13a08fb417aa013cdcb3f9622af000000000000000000000000",main_guards,aliases};
} // namespace
const Profile& product120() { return kProduct120; }


uint64_t hash_prefix(std::span<const uint8_t> bytes) noexcept {
    uint64_t hash=0xcbf29ce484222325ull;
    for(auto byte:bytes)hash=(hash^byte)*0x100000001b3ull;
    return hash;
}
namespace {
bool controlled(std::string_view name) {
    return name.starts_with("_ZN2nn6socket") || name.starts_with("nnsocket") ||
           name.starts_with("_ZN2nn4nifm");
}
bool guarded(const ModuleView& module,const Guard& guard,bool entry) {
    const auto* info=module.info;
    if(!info || !guard.bytes || (guard.offset&3) || guard.offset<info->text ||
       guard.bytes>info->text_size || guard.offset-info->text>info->text_size-guard.bytes ||
       guard.offset>module.image.size() || guard.bytes>module.image.size()-guard.offset) return false;
    if(entry && (info->text_size<4 || guard.offset>info->text_size-4 ||
        guard.offset/4>=module.table.size() || !module.table[guard.offset/4]))return false;
    return hash_prefix(module.image.subspan(size_t(guard.offset),guard.bytes))==guard.hash;
}
const Alias* alias_at(const Profile& profile,std::string_view name) {
    const auto found=std::find_if(profile.aliases.begin(),profile.aliases.end(),
        [&](const auto& alias){return alias.name==name;});
    return found==profile.aliases.end()?nullptr:&*found;
}
bool main_import(const ModuleInfo& main,std::string_view name) {
    for(size_t i=0;i<main.symbol_count;++i) {
        const auto& symbol=main.symbols[i];
        if(symbol.name && symbol.imported && symbol.type==2 && name==symbol.name)return true;
    }
    return false;
}
const Symbol* sdk_symbol(const ModuleInfo& sdk,std::string_view name) {
    const Symbol* found=nullptr;
    for(size_t i=0;i<sdk.symbol_count;++i) {
        const auto& symbol=sdk.symbols[i];
        if(!symbol.name || name!=symbol.name)continue;
        if(symbol.imported || symbol.type!=2)return nullptr;
        // Ambiguous definitions must not let a malformed first/last entry win.
        if(found && found->value!=symbol.value)return nullptr;
        found=&symbol;
    }
    return found;
}
}
Plan prepare(bool enabled,const Profile& profile,const ModuleView& main,const ModuleView& sdk,
             Phase phase,const Lookup& lookup) {
    Plan plan;
    if(!enabled)return plan; // Do not even query/resolve a default-off backend.
    const auto reject=[&](std::string reason) {
        plan.status=Status::Rejected;plan.error=std::move(reason);plan.bindings.clear();
        plan.preserved_aliases=0;return plan;
    };
    if(phase!=Phase::BeforeRelocations)return reject("Native LAN bindings require the pre-relocation phase");
    if(!main.info || !sdk.info || !main.info->build_id || !sdk.info->build_id ||
       profile.main_build_id.empty() || profile.sdk_build_id.empty() ||
       profile.main_build_id!=main.info->build_id || profile.sdk_build_id!=sdk.info->build_id)
        return reject("Native LAN source build identity is unsupported");
    if(!lookup.hle || !lookup.thunk_cached || !lookup.handler)
        return reject("Native LAN binding queries are incomplete");
    if((main.info->symbol_count && !main.info->symbols) || (sdk.info->symbol_count && !sdk.info->symbols) ||
       profile.aliases.empty() || profile.main_guards.empty())return reject("Native LAN profile metadata is incomplete");
    // Respect loader.cpp's actual table allocation as well as the SDK text span.
    if(sdk.table.size()<sdk.info->text_size/4)return reject("Native LAN SDK function table is truncated");
    for(const auto& guard:profile.main_guards)
        if(!guarded(main,guard,false))return reject("Native LAN main code prefix does not match");
    for(size_t i=0;i<profile.aliases.size();++i) {
        const auto& alias=profile.aliases[i];
        if(alias.name.empty() || unsigned(alias.operation)>=unsigned(Operation::Count) ||
           unsigned(alias.result)>unsigned(ReturnKind::Preserved))
            return reject("Native LAN alias specification is invalid");
        for(size_t k=0;k<i;++k)if(profile.aliases[k].name==alias.name)
            return reject("Native LAN profile contains duplicate aliases");
    }
    for(size_t i=0;i<main.info->symbol_count;++i) {
        const auto& symbol=main.info->symbols[i];
        if(symbol.name && symbol.imported && symbol.type==2 && controlled(symbol.name) &&
           !alias_at(profile,symbol.name))return reject("Native LAN main import has no binding policy: "+std::string(symbol.name));
    }
    for(const auto& alias:profile.aliases) {
        const bool imported=main_import(*main.info,alias.name);
        if(alias.main_import_required && !imported)
            return reject("Native LAN required main alias is missing: "+std::string(alias.name));
        const auto* symbol=sdk_symbol(*sdk.info,alias.name);
        if(!symbol || symbol->value!=alias.sdk.offset || !guarded(sdk,alias.sdk,true))
            return reject("Native LAN required SDK alias/offset/prefix mismatch: "+std::string(alias.name));
        if(alias.operation==Operation::PreserveOriginal) {
            ++plan.preserved_aliases;continue;
        }
        if(lookup.thunk_cached(alias.name))
            return reject("Native LAN alias already has a cached HLE thunk: "+std::string(alias.name));
        Fn replacement=lookup.handler(alias.operation,alias.result);
        if(!replacement)return reject("Native LAN operation has no explicit handler: "+std::string(alias.name));
        Fn* entry=sdk.table.data()+alias.sdk.offset/4;
        for(const auto& before:plan.bindings)
            if(before.sdk_entry==entry && before.replacement!=replacement)
                return reject("Native LAN aliases require conflicting handlers for one SDK entry");
        Fn previous=lookup.hle(alias.name);
        plan.bindings.push_back({alias.name,alias.operation,alias.result,replacement,previous,entry,*entry,
                                 imported,imported && previous!=nullptr});
    }
    if(plan.bindings.empty())return reject("Native LAN profile contains no backend bindings");
    plan.status=Status::Ready;
    return plan;
}
bool apply(const Plan& plan,Phase phase,const Lookup& lookup,Committer& committer) {
    if(plan.status!=Status::Ready || phase!=Phase::BeforeRelocations ||
       !lookup.hle || !lookup.thunk_cached || !lookup.handler)return false;
    // No writes until every captured HLE/table original is still current.
    for(const auto& binding:plan.bindings)
        if(!binding.sdk_entry || !binding.replacement ||
           *binding.sdk_entry!=binding.original_sdk ||
           lookup.hle(binding.name)!=binding.original_hle || lookup.thunk_cached(binding.name) ||
           lookup.handler(binding.operation,binding.result)!=binding.replacement)return false;
    return committer.commit(plan.bindings);
}

} // namespace nx::native_lan::profile
