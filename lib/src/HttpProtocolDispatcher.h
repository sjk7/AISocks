#ifndef AISOCKS_HTTP_PROTOCOL_DISPATCHER_H
#define AISOCKS_HTTP_PROTOCOL_DISPATCHER_H

#include "ProtocolDispatcher.h"
#include "HttpPollServer.h"

namespace aiSocks {

class HttpProtocolDispatcher : public ProtocolDispatcher<HttpClientState> {
    public:
    explicit HttpProtocolDispatcher(HttpPollServer& server) : server_(server) {}

    ServerResult onReadable(TcpSocket& sock, HttpClientState& s) override;
    ServerResult onWritable(TcpSocket& sock, HttpClientState& s) override;

    private:
    HttpPollServer& server_;
};

} // namespace aiSocks

#endif // AISOCKS_HTTP_PROTOCOL_DISPATCHER_H
