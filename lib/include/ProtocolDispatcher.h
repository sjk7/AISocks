#ifndef AISOCKS_PROTOCOL_DISPATCHER_H
#define AISOCKS_PROTOCOL_DISPATCHER_H

#include "ServerBase.h"
#include "TcpSocket.h"
#include <string>
#include <chrono>

namespace aiSocks {

template <typename ClientState> class ProtocolDispatcher {
    public:
    virtual ~ProtocolDispatcher() = default;

    virtual ServerResult onReadable(TcpSocket& sock, ClientState& s) = 0;
    virtual ServerResult onWritable(TcpSocket& sock, ClientState& s) = 0;
};

} // namespace aiSocks

#endif // AISOCKS_PROTOCOL_DISPATCHER_H
