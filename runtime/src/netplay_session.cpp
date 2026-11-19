#include "netplay_session.h"
#include "netplay_save.h"
#include "netplay_probe.h"
#include <algorithm>
#include <atomic>
#include <cstring>
#include <mutex>
#include <random>
#include <stdexcept>
#include <thread>
#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace nx::lan {
namespace {
using Clock = std::chrono::steady_clock;
#if defined(_WIN32)
using Handle = SOCKET;
constexpr Handle invalid = INVALID_SOCKET;
int socket_error() { return WSAGetLastError(); }
bool pending(int e) { return e==WSAEWOULDBLOCK || e==WSAETIMEDOUT || e==WSAEINPROGRESS || e==WSAEINTR; }
void close_socket(Handle h) { closesocket(h); }
constexpr int both_directions = SD_BOTH;
#else
using Handle = int;
constexpr Handle invalid = -1;
int socket_error() { return errno; }
bool pending(int e) { return e==EAGAIN || e==EWOULDBLOCK || e==EINPROGRESS || e==EINTR; }
void close_socket(Handle h) { ::close(h); }
constexpr int both_directions = SHUT_RDWR;
#endif
struct SocketRuntime {
    SocketRuntime() {
#if defined(_WIN32)
        WSADATA data{};
        if (WSAStartup(MAKEWORD(2,2),&data)) throw std::runtime_error("Could not initialize LAN sockets");
#endif
    }
    ~SocketRuntime() {
#if defined(_WIN32)
        WSACleanup();
#endif
    }
};
std::shared_ptr<SocketRuntime> sockets() {
    static std::mutex lock;
    static std::weak_ptr<SocketRuntime> shared;
    std::lock_guard guard(lock);
    auto result=shared.lock();
    if(!result) { result=std::make_shared<SocketRuntime>(); shared=result; }
    return result;
}
struct Socket {
    std::atomic<Handle> handle{invalid};
    Socket()=default;
    explicit Socket(Handle h):handle(h) {}
    Socket(Socket&& other) noexcept:handle(other.handle.exchange(invalid)) {}
    Socket& operator=(Socket&& other) noexcept {
        reset(other.handle.exchange(invalid)); return *this;
    }
    ~Socket() { reset(); }
    void interrupt() const {
        const auto h=handle.load(); if(h!=invalid) ::shutdown(h,both_directions);
    }
    void reset(Handle next=invalid) {
        const auto old=handle.exchange(next); if(old!=invalid) close_socket(old);
    }
    Handle get() const { return handle.load(); }
};
[[noreturn]] void fail_socket(const char* operation) {
    throw std::runtime_error(std::string("LAN ")+operation+" failed (socket error "+std::to_string(socket_error())+")");
}
void nonblocking(Handle h,bool on) {
#if defined(_WIN32)
    u_long mode=on?1:0;
    if(ioctlsocket(h,FIONBIO,&mode)) fail_socket("socket mode");
#else
    const int flags=fcntl(h,F_GETFL,0);
    if(flags<0 || fcntl(h,F_SETFL,on?flags|O_NONBLOCK:flags&~O_NONBLOCK)<0) fail_socket("socket mode");
#endif
}
void configure(Handle h) {
    const int one=1;
    if(setsockopt(h,IPPROTO_TCP,TCP_NODELAY,reinterpret_cast<const char*>(&one),sizeof one)) fail_socket("low-latency mode");
    nonblocking(h,true);
}
sockaddr_in address(const std::string& text,uint16_t port,bool listener=false) {
    sockaddr_in a{}; a.sin_family=AF_INET; a.sin_port=htons(port);
    if(inet_pton(AF_INET,text.c_str(),&a.sin_addr)!=1 ||
       (!(listener&&a.sin_addr.s_addr==INADDR_ANY)&&!private_ipv4(ntohl(a.sin_addr.s_addr))))
        throw std::runtime_error("Enter a private LAN, VPN or loopback IPv4 address");
    return a;
}
Nonce random_nonce() {
    std::random_device random; Nonce result{};
    for(size_t i=0;i<result.size();i+=4) {
        const auto word=random(); for(size_t j=0;j<4;++j) result[i+j]=uint8_t(word>>(j*8));
    }
    return result;
}
void append64(std::vector<uint8_t>& out,uint64_t value) {
    for(int i=0;i<8;++i) { out.push_back(uint8_t(value)); value>>=8; }
}
uint64_t number(std::span<const uint8_t> bytes,size_t offset) {
    if(offset>bytes.size() || bytes.size()-offset<8) throw std::runtime_error("Truncated LAN session data");
    uint64_t v=0; for(size_t i=0;i<8;++i) v|=uint64_t(bytes[offset+i])<<(8*i); return v;
}
}

struct Session::Impl {
    // Runtime declared first so every socket closes before the last WSA cleanup.
    std::shared_ptr<SocketRuntime> runtime;
    Socket listener;
    std::array<Socket,kPlayers> peers;
    SessionOptions options;
    std::array<PositionProbeGuard,kPlayers> position_probes;
    std::atomic<bool> probe_ready{false};
    Nonce nonce{};
    uint8_t local_role=0,count=0;
    uint8_t common_rate=60,common_lead=0;
    uint64_t tick=0,common_epoch=0,common_seed=0;
    std::atomic<bool> stopped{false};
    bool started=false,ready=false,loading=false;
    std::mutex io;
    struct Counters {
        std::array<std::atomic<uint64_t>,kDiagnosticMessageTypes> sent_messages{},received_messages{};
        std::atomic<uint64_t> sent_bytes{0},received_bytes{0};
        std::atomic<uint64_t> send_waits{0},receive_waits{0},wait_ns{0},peak_wait_ns{0};
        std::atomic<uint64_t> timeouts{0},receive_timeouts{0},cancellations{0};
        std::atomic<uint64_t> disconnects{0},socket_errors{0},invalid_packets{0};
        SessionDiagnostics snapshot() const noexcept {
            SessionDiagnostics out;
            for(size_t i=0;i<kDiagnosticMessageTypes;++i) {
                out.sent_messages[i]=sent_messages[i].load(std::memory_order_relaxed);
                out.received_messages[i]=received_messages[i].load(std::memory_order_relaxed);
            }
#define NX_DIAGNOSTIC_LOAD(field) out.field=field.load(std::memory_order_relaxed)
            NX_DIAGNOSTIC_LOAD(sent_bytes); NX_DIAGNOSTIC_LOAD(received_bytes);
            NX_DIAGNOSTIC_LOAD(send_waits); NX_DIAGNOSTIC_LOAD(receive_waits);
            NX_DIAGNOSTIC_LOAD(wait_ns); NX_DIAGNOSTIC_LOAD(peak_wait_ns);
            NX_DIAGNOSTIC_LOAD(timeouts); NX_DIAGNOSTIC_LOAD(receive_timeouts);
            NX_DIAGNOSTIC_LOAD(cancellations); NX_DIAGNOSTIC_LOAD(disconnects);
            NX_DIAGNOSTIC_LOAD(socket_errors); NX_DIAGNOSTIC_LOAD(invalid_packets);
#undef NX_DIAGNOSTIC_LOAD
            return out;
        }
    } counters;
    [[noreturn]] void io_error(const char* operation,int error=socket_error()) {
        counters.socket_errors.fetch_add(1,std::memory_order_relaxed);
        throw std::runtime_error(std::string("LAN ")+operation+" failed (socket error "+std::to_string(error)+")");
    }

    Clock::time_point deadline() const { return Clock::now()+options.timeout; }
    void check(Clock::time_point until) {
        if(stopped || (options.idle&&!options.idle())) {
            counters.cancellations.fetch_add(1,std::memory_order_relaxed);
            throw std::runtime_error("LAN session cancelled");
        }
        if(Clock::now()>=until) {
            counters.timeouts.fetch_add(1,std::memory_order_relaxed);
            throw std::runtime_error("LAN peer timed out");
        }
    }
    void wait(Handle h,bool writing,Clock::time_point until) {
        check(until);
        fd_set set; FD_ZERO(&set); FD_SET(h,&set);
        timeval timeout{0,20'000};
        (writing?counters.send_waits:counters.receive_waits).fetch_add(1,std::memory_order_relaxed);
        const auto started_wait=Clock::now();
        const int result=select(int(h+1),writing?nullptr:&set,writing?&set:nullptr,nullptr,&timeout);
        const int result_error=result<0?socket_error():0;
        const auto elapsed=uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-started_wait).count());
        counters.wait_ns.fetch_add(elapsed,std::memory_order_relaxed);
        auto peak=counters.peak_wait_ns.load(std::memory_order_relaxed);
        while(peak<elapsed && !counters.peak_wait_ns.compare_exchange_weak(peak,elapsed,std::memory_order_relaxed)) {}
        if(result<0 && !pending(result_error)) io_error("wait",result_error);
        check(until);
    }
    void send_bytes(Handle h,std::span<const uint8_t> bytes,Clock::time_point until) {
        size_t at=0;
        while(at<bytes.size()) {
            check(until);
#if defined(MSG_NOSIGNAL)
            constexpr int flags=MSG_NOSIGNAL;
#else
            constexpr int flags=0;
#endif
            const int n=int(::send(h,reinterpret_cast<const char*>(bytes.data()+at),int(bytes.size()-at),flags));
            if(n>0) { at+=size_t(n); counters.sent_bytes.fetch_add(uint64_t(n),std::memory_order_relaxed); }
            else if(n==0) { counters.disconnects.fetch_add(1,std::memory_order_relaxed); throw std::runtime_error("LAN peer disconnected"); }
            else if(pending(socket_error())) wait(h,true,until);
            else io_error("send");
        }
    }
    void receive_bytes(Handle h,std::span<uint8_t> bytes,Clock::time_point until) {
        size_t at=0;
        try { while(at<bytes.size()) {
            check(until);
            const int n=int(::recv(h,reinterpret_cast<char*>(bytes.data()+at),int(bytes.size()-at),0));
            if(n>0) { at+=size_t(n); counters.received_bytes.fetch_add(uint64_t(n),std::memory_order_relaxed); }
            else if(n==0) { counters.disconnects.fetch_add(1,std::memory_order_relaxed); throw std::runtime_error("LAN peer disconnected"); }
            else if(pending(socket_error())) wait(h,false,until);
            else io_error("receive");
        } } catch(const std::runtime_error& error) {
            if(std::strcmp(error.what(),"LAN peer timed out")==0) counters.receive_timeouts.fetch_add(1,std::memory_order_relaxed);
            throw;
        }
    }
    void send(Handle h,const Packet& p,Clock::time_point until) {
        send_bytes(h,encode_packet(p,options.key),until);
        counters.sent_messages[size_t(p.type)].fetch_add(1,std::memory_order_relaxed);
    }
    Packet receive(Handle h,Clock::time_point until) {
        std::array<uint8_t,kHeaderBytes> header{}; receive_bytes(h,header,until);
        size_t size;
        try { size=packet_payload_size(header); }
        catch(const std::runtime_error&) { counters.invalid_packets.fetch_add(1,std::memory_order_relaxed); throw; }
        std::vector<uint8_t> packet(kHeaderBytes+size+kMacBytes);
        std::copy(header.begin(),header.end(),packet.begin());
        receive_bytes(h,std::span(packet).subspan(kHeaderBytes),until);
        Packet decoded;
        try { decoded=decode_packet(packet,options.key); }
        catch(const std::runtime_error&) { counters.invalid_packets.fetch_add(1,std::memory_order_relaxed); throw; }
        counters.received_messages[size_t(decoded.type)].fetch_add(1,std::memory_order_relaxed);
        return decoded;
    }
    Packet expected(Handle h,Message type,uint16_t role,uint64_t sequence,Clock::time_point until) {
        auto packet=receive(h,until);
        if(packet.session!=nonce || packet.sender!=role || packet.tick!=sequence)
            throw std::runtime_error("LAN session, role or tick mismatch");
        if(packet.type==Message::Reject) throw std::runtime_error("LAN simulation or session data mismatch");
        if(packet.type!=type) throw std::runtime_error("Unexpected LAN message");
        return packet;
    }
    Packet message(Message type,uint16_t sender,uint64_t sequence,std::vector<uint8_t> payload={}) const {
        Packet p; p.type=type; p.sender=sender; p.tick=sequence; p.session=nonce; p.payload=std::move(payload); return p;
    }
    void begin(const SessionOptions& config) {
        if(started) throw std::runtime_error("Create a new LAN session before reconnecting");
        if(config.players<2||config.players>kPlayers||config.probe_warmup>60||
           (config.frame_rate!=30 && config.frame_rate!=60)||config.input_lead>1||
           config.timeout<std::chrono::milliseconds(100)||config.timeout>std::chrono::minutes(5)||
           config.frame_timeout<std::chrono::milliseconds(100)||config.frame_timeout>std::chrono::seconds(30)||
           config.startup_timeout<std::chrono::milliseconds(100)||config.startup_timeout>std::chrono::minutes(15))
            throw std::runtime_error("Invalid LAN room settings");
        options=config; runtime=sockets(); started=true; count=config.players;
    }
    void interrupt() noexcept {
        stopped=true; listener.interrupt(); for(auto& peer:peers) peer.interrupt();
    }
};
Session::Session():impl(std::make_unique<Impl>()) {}
Session::~Session() { stop(); }
void Session::stop() noexcept { impl->interrupt(); }
uint8_t Session::role() const { return impl->local_role; }
uint8_t Session::players() const { return impl->count; }
uint64_t Session::epoch() const { return impl->common_epoch; }
uint64_t Session::seed() const { return impl->common_seed; }
bool Session::position_probe_ready() const { return impl->probe_ready.load(); }
uint8_t Session::frame_rate() const { return impl->common_rate; }
uint8_t Session::input_lead() const { return impl->common_lead; }
SessionDiagnostics Session::diagnostics() const noexcept { return impl->counters.snapshot(); }
std::vector<std::string> local_addresses() {
    const auto runtime=sockets();
    char name[256]{};
    if(gethostname(name,sizeof(name)-1)) return {};
    addrinfo hints{}; hints.ai_family=AF_INET; hints.ai_socktype=SOCK_STREAM;
    addrinfo* addresses=nullptr;
    if(getaddrinfo(name,nullptr,&hints,&addresses)) return {};
    std::unique_ptr<addrinfo,decltype(&freeaddrinfo)> owner(addresses,&freeaddrinfo);
    std::vector<std::string> result;
    for(auto* entry=addresses;entry;entry=entry->ai_next) {
        const auto* ip=reinterpret_cast<const sockaddr_in*>(entry->ai_addr);
        const auto host=ntohl(ip->sin_addr.s_addr);
        if(!private_ipv4(host) || (host>>24)==127) continue;
        char text[INET_ADDRSTRLEN]{};
        if(inet_ntop(AF_INET,&ip->sin_addr,text,sizeof text)) result.emplace_back(text);
    }
    std::sort(result.begin(),result.end()); result.erase(std::unique(result.begin(),result.end()),result.end());
    return result;
}

void Session::host(const SessionOptions& options,const std::function<void(uint16_t)>& listening) {
    auto& s=*impl; std::lock_guard lock(s.io); s.begin(options);
    try {
        if (options.allow_save_transfer && (options.initial_save.empty() || options.initial_save.size()>kMaxSaveBytes || digest(options.initial_save)!=options.save))
            throw std::runtime_error("Invalid host session-save snapshot");
        s.local_role=0; s.nonce=random_nonce();
        s.common_rate=options.frame_rate; s.common_lead=options.input_lead;
        s.common_epoch=options.epoch?options.epoch:uint64_t(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count());
        s.common_seed=options.seed?options.seed:number(s.nonce,0);
        s.listener.reset(::socket(AF_INET,SOCK_STREAM,IPPROTO_TCP));
        if(s.listener.get()==invalid) fail_socket("listen socket");
        nonblocking(s.listener.get(),true);
        const auto a=address(options.address,options.port,true);
        if(::bind(s.listener.get(),reinterpret_cast<const sockaddr*>(&a),sizeof a) || ::listen(s.listener.get(),8)) fail_socket("listen");
        sockaddr_in bound{};
#if defined(_WIN32)
        int bound_size=sizeof bound;
#else
        socklen_t bound_size=sizeof bound;
#endif
        if(getsockname(s.listener.get(),reinterpret_cast<sockaddr*>(&bound),&bound_size)) fail_socket("local address");
        if(listening) listening(ntohs(bound.sin_port));
        const auto until=s.deadline(); unsigned rejected=0;
        for(uint8_t role=1;role<s.count;) {
            s.check(until); sockaddr_in from{};
#if defined(_WIN32)
            int from_size=sizeof from;
#else
            socklen_t from_size=sizeof from;
#endif
            Socket candidate(::accept(s.listener.get(),reinterpret_cast<sockaddr*>(&from),&from_size));
            if(candidate.get()==invalid) {
                if(pending(socket_error())) { s.wait(s.listener.get(),false,until); continue; }
                fail_socket("accept");
            }
            if(!private_ipv4(ntohl(from.sin_addr.s_addr))) continue;
            configure(candidate.get());
            try {
                const auto handshake_until=std::min(until,Clock::now()+std::chrono::seconds(2));
                auto hello=s.receive(candidate.get(),handshake_until);
                const bool transfer=options.allow_save_transfer && hello.payload.size()==65 && hello.payload.back()==1;
                if(hello.type!=Message::Hello||hello.sender!=0xffff||hello.tick||
                   (hello.payload.size()!=64 && !(hello.payload.size()==65 && hello.payload.back()==1)) ||
                   !std::equal(options.build.begin(),options.build.end(),hello.payload.begin()) ||
                   (!transfer && !std::equal(options.save.begin(),options.save.end(),hello.payload.begin()+32)))
                    throw std::runtime_error("LAN build, input mode or save differs");
                std::vector<uint8_t> welcome{role,s.count,0,0,0,0,0,0};
                welcome[2]=transfer?1:0;
                welcome[3]=s.common_rate; welcome[4]=s.common_lead;
                append64(welcome,s.common_epoch); append64(welcome,s.common_seed);
                welcome.insert(welcome.end(),hello.session.begin(),hello.session.end());
                welcome.insert(welcome.end(),options.build.begin(),options.build.end());
                welcome.insert(welcome.end(),options.save.begin(),options.save.end());
                s.send(candidate.get(),s.message(Message::Welcome,0,0,std::move(welcome)),handshake_until);
                if(transfer) {
                    std::vector<uint8_t> size; append64(size,options.initial_save.size());
                    s.send(candidate.get(),s.message(Message::SaveBegin,0,0,std::move(size)),until);
                    uint64_t sequence=0;
                    for(size_t at=0;at<options.initial_save.size();at+=kMaxPayload) {
                        const auto n=std::min(kMaxPayload,options.initial_save.size()-at);
                        std::vector<uint8_t> chunk(options.initial_save.begin()+at,options.initial_save.begin()+at+n);
                        s.send(candidate.get(),s.message(Message::SaveChunk,0,sequence++,std::move(chunk)),until);
                    }
                    s.send(candidate.get(),s.message(Message::SaveEnd,0,sequence),until);
                    const auto ack=s.receive(candidate.get(),until);
                    if(ack.type!=Message::SaveAck || ack.sender!=role || ack.tick!=sequence || ack.session!=s.nonce ||
                       ack.payload.size()!=options.save.size() || !std::equal(options.save.begin(),options.save.end(),ack.payload.begin()))
                        throw std::runtime_error("LAN client did not confirm its isolated session save");
                }
                s.peers[role]=std::move(candidate); ++role;
            } catch(...) {
                if(s.stopped) throw;
                if(++rejected>=32) throw std::runtime_error("Too many rejected LAN connections; create a new room");
            }
        }
        for(uint8_t role=1;role<s.count;++role) s.send(s.peers[role].get(),s.message(Message::Start,0,0),until);
        s.listener.reset(); s.ready=true;
    } catch(...) { s.interrupt(); throw; }
}

void Session::join(const SessionOptions& options) {
    auto& s=*impl; std::lock_guard lock(s.io); s.begin(options);
    try {
        s.peers[0].reset(::socket(AF_INET,SOCK_STREAM,IPPROTO_TCP));
        if(s.peers[0].get()==invalid) fail_socket("connect socket");
        configure(s.peers[0].get()); const auto a=address(options.address,options.port);
        const auto until=s.deadline();
        if(::connect(s.peers[0].get(),reinterpret_cast<const sockaddr*>(&a),sizeof a)) {
            if(!pending(socket_error())) fail_socket("connect");
            for(;;) {
                s.check(until);
                fd_set writable,failed;
                FD_ZERO(&writable); FD_SET(s.peers[0].get(),&writable);
                FD_ZERO(&failed); FD_SET(s.peers[0].get(),&failed);
                timeval timeout{0,20'000};
                // Winsock reports a refused nonblocking connect in exceptfds,
                // rather than writefds. Check SO_ERROR for either completion.
                const int result=select(int(s.peers[0].get()+1),nullptr,&writable,&failed,&timeout);
                if(result<0 && !pending(socket_error())) fail_socket("connect wait");
                if(result>0) break;
            }
            int error=0;
#if defined(_WIN32)
            int size=sizeof error;
#else
            socklen_t size=sizeof error;
#endif
            if(getsockopt(s.peers[0].get(),SOL_SOCKET,SO_ERROR,reinterpret_cast<char*>(&error),&size) || error)
                throw std::runtime_error("Could not connect to the LAN host");
        }
        Packet hello; hello.type=Message::Hello; hello.sender=0xffff; hello.session=random_nonce();
        hello.payload.assign(options.build.begin(),options.build.end()); hello.payload.insert(hello.payload.end(),options.save.begin(),options.save.end());
        if(options.allow_save_transfer) hello.payload.push_back(1);
        s.send(s.peers[0].get(),hello,until);
        auto welcome=s.receive(s.peers[0].get(),until);
        if(welcome.type!=Message::Welcome||welcome.sender||welcome.tick||welcome.payload.size()!=104 ||
           welcome.payload[0]<1 || welcome.payload[0]>=welcome.payload[1] || welcome.payload[1]<2 || welcome.payload[1]>kPlayers ||
           !std::equal(hello.session.begin(),hello.session.end(),welcome.payload.begin()+24) ||
           !std::equal(options.build.begin(),options.build.end(),welcome.payload.begin()+40) ||
           ((!options.allow_save_transfer || welcome.payload[2]!=1) && !std::equal(options.save.begin(),options.save.end(),welcome.payload.begin()+72)))
            throw std::runtime_error("LAN host settings or pairing response do not match");
        if(welcome.payload[2]>1 || (welcome.payload[2] && !options.allow_save_transfer)) throw std::runtime_error("Unsupported LAN save transfer");
        if(options.allow_save_transfer && !welcome.payload[2]) throw std::runtime_error("LAN host does not support an isolated session-save transfer");
        if ((welcome.payload[3]!=30 && welcome.payload[3]!=60) || welcome.payload[4]>1)
            throw std::runtime_error("Unsupported LAN simulation target or input buffer");
        for(size_t i=5;i<8;++i) if(welcome.payload[i]) throw std::runtime_error("Invalid LAN welcome flags");
        s.nonce=welcome.session; s.local_role=welcome.payload[0]; s.count=welcome.payload[1];
        s.common_rate=welcome.payload[3]; s.common_lead=welcome.payload[4];
        s.common_epoch=number(welcome.payload,8); s.common_seed=number(welcome.payload,16);
        if(welcome.payload[2]) {
            if(!options.install_save) throw std::runtime_error("An isolated client save destination is required");
            const auto begin=s.expected(s.peers[0].get(),Message::SaveBegin,0,0,until);
            if(begin.payload.size()!=8) throw std::runtime_error("Invalid LAN session-save size");
            const auto size=number(begin.payload,0);
            if(!size || size>kMaxSaveBytes) throw std::runtime_error("LAN session save exceeds the supported size");
            std::vector<uint8_t> snapshot; snapshot.reserve(size_t(size));
            uint64_t sequence=0;
            while(snapshot.size()<size) {
                const auto chunk=s.expected(s.peers[0].get(),Message::SaveChunk,0,sequence++,until);
                if(chunk.payload.empty() || chunk.payload.size()>size-snapshot.size()) throw std::runtime_error("Invalid LAN save chunk");
                snapshot.insert(snapshot.end(),chunk.payload.begin(),chunk.payload.end());
            }
            if(!s.expected(s.peers[0].get(),Message::SaveEnd,0,sequence,until).payload.empty()) throw std::runtime_error("Invalid LAN save trailer");
            const auto actual=digest(snapshot);
            if(!std::equal(actual.begin(),actual.end(),welcome.payload.begin()+72)) throw std::runtime_error("LAN session-save fingerprint mismatch");
            options.install_save(snapshot);
            s.send(s.peers[0].get(),s.message(Message::SaveAck,s.local_role,sequence,{actual.begin(),actual.end()}),until);
        }
        s.expected(s.peers[0].get(),Message::Start,0,0,until); s.ready=true;
    } catch(...) { s.interrupt(); throw; }
}

FrameInputs Session::exchange(const plat::PadState& local,uint64_t hash,const PlayerState& state,uint64_t work,uint64_t event) {
    auto& s=*impl; std::lock_guard lock(s.io);
    if(!s.ready||s.stopped) throw std::runtime_error("LAN session is not connected");
    // Tick 0 is the lobby's neutral exchange; tick 1 may wait while the other
    // PC loads modules and warms shaders. Only use the short deadline once
    // both games have reached their first complete framework frame.
    // Explicit loading markers retain the loading budget, including a marker
    // received from another peer. The first all-zero commit restores the short
    // established-game deadline for the following exchange.
    const auto until=Clock::now()+((s.tick<2 || event || s.loading)?s.options.startup_timeout:s.options.frame_timeout);
    try {
        FrameInputs frame; frame.tick=s.tick; frame.players=s.count; frame.state_hash=hash;
        frame.state=state;
        if(s.local_role==0) {
            frame.pads[0]=local;
            frame.frame_work_ns[0]=work; frame.loading_events[0]=event;
            for(uint8_t role=1;role<s.count;++role) {
                const auto p=s.expected(s.peers[role].get(),Message::Input,role,s.tick,until);
                uint64_t peer_hash=0; PlayerState peer_state;
                frame.pads[role]=decode_input(p.payload,peer_hash,&peer_state,&frame.frame_work_ns[role],&frame.loading_events[role]);
                if((hash&&peer_hash&&hash!=peer_hash) || !s.position_probes[role].observe(event || frame.loading_events[role]?PlayerState{}:state,peer_state,s.options.probe_warmup)) {
                    for(uint8_t i=1;i<s.count;++i) s.send(s.peers[i].get(),s.message(Message::Reject,0,s.tick),until);
                    throw std::runtime_error("LAN simulation diverged; restart from the same session state");
                }
            }
            bool verified=true;
            for(uint8_t role=1;role<s.count;++role) verified=verified && s.position_probes[role].ready();
            s.probe_ready=verified;
            const auto commit=s.message(Message::Commit,0,s.tick,encode_commit(frame));
            for(uint8_t role=1;role<s.count;++role) s.send(s.peers[role].get(),commit,until);
            // Input N+1 can only be sent after Commit N was authenticated and
            // consumed. It acknowledges that commit on the same ordered TCP
            // stream; a separate per-tick Ack only adds another packet/wait.
        } else {
            s.send(s.peers[0].get(),s.message(Message::Input,s.local_role,s.tick,encode_input(local,hash,state,work,event)),until);
            const auto commit=s.expected(s.peers[0].get(),Message::Commit,0,s.tick,until);
            frame=decode_commit(commit.payload); frame.tick=s.tick;
            if(frame.players!=s.count || (hash&&frame.state_hash&&hash!=frame.state_hash) || !s.position_probes[0].observe(event || frame.loading_events[0]?PlayerState{}:state,frame.state,s.options.probe_warmup)) throw std::runtime_error("LAN simulation state mismatch");
            s.probe_ready=s.position_probes[0].ready();
        }
        s.loading=std::any_of(frame.loading_events.begin(),frame.loading_events.end(),[](uint64_t marker){return marker!=0;});
        ++s.tick; return frame;
    } catch(...) { s.interrupt(); throw; }
}
}
