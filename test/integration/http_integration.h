#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "common/http/codec_client.h"
#include "common/network/filter_impl.h"

#include "test/integration/integration.h"
#include "test/integration/utility.h"
#include "test/test_common/printers.h"

namespace Envoy {

/**
 * HTTP codec client used during integration testing.
 */
class IntegrationCodecClient : public Http::CodecClientProd {
public:
  IntegrationCodecClient(Event::Dispatcher& dispatcher, Network::ClientConnectionPtr&& conn,
                         Upstream::HostDescriptionConstSharedPtr host_description,
                         Http::CodecClient::Type type);

  IntegrationStreamDecoderPtr makeHeaderOnlyRequest(const Http::HeaderMap& headers);
  IntegrationStreamDecoderPtr makeRequestWithBody(const Http::HeaderMap& headers,
                                                  uint64_t body_size);
  IntegrationStreamDecoderPtr makeRequestWithBody(const Http::HeaderMap& headers,
                                                  const std::string& body);
  bool sawGoAway() const { return saw_goaway_; }
  bool connected() const { return connected_; }
  void sendData(Http::StreamEncoder& encoder, absl::string_view data, bool end_stream);
  void sendData(Http::StreamEncoder& encoder, Buffer::Instance& data, bool end_stream);
  void sendData(Http::StreamEncoder& encoder, uint64_t size, bool end_stream);
  void sendTrailers(Http::StreamEncoder& encoder, const Http::HeaderMap& trailers);
  void sendReset(Http::StreamEncoder& encoder);
  std::pair<Http::StreamEncoder&, IntegrationStreamDecoderPtr>
  startRequest(const Http::HeaderMap& headers);
  bool waitForDisconnect(std::chrono::milliseconds time_to_wait = std::chrono::milliseconds(0));
  Network::ClientConnection* connection() const { return connection_.get(); }
  Network::ConnectionEvent last_connection_event() const { return last_connection_event_; }

private:
  struct ConnectionCallbacks : public Network::ConnectionCallbacks {
    ConnectionCallbacks(IntegrationCodecClient& parent) : parent_(parent) {}

    // Network::ConnectionCallbacks
    void onEvent(Network::ConnectionEvent event) override;
    void onAboveWriteBufferHighWatermark() override {}
    void onBelowWriteBufferLowWatermark() override {}

    IntegrationCodecClient& parent_;
  };

  struct CodecCallbacks : public Http::ConnectionCallbacks {
    CodecCallbacks(IntegrationCodecClient& parent) : parent_(parent) {}

    // Http::ConnectionCallbacks
    void onGoAway() override { parent_.saw_goaway_ = true; }

    IntegrationCodecClient& parent_;
  };

  void flushWrite();

  Event::Dispatcher& dispatcher_;
  ConnectionCallbacks callbacks_;
  CodecCallbacks codec_callbacks_;
  bool connected_{};
  bool disconnected_{};
  bool saw_goaway_{};
  Network::ConnectionEvent last_connection_event_;
};

typedef std::unique_ptr<IntegrationCodecClient> IntegrationCodecClientPtr;

/**
 * Test fixture for HTTP and HTTP/2 integration tests.
 */
class HttpIntegrationTest : public BaseIntegrationTest {
public:
  // TODO(jmarantz): Remove this once
  // https://github.com/envoyproxy/envoy-filter-example/pull/69 is reverted.
  HttpIntegrationTest(Http::CodecClient::Type downstream_protocol,
                      Network::Address::IpVersion version, TestTimeSystemPtr,
                      const std::string& config = ConfigHelper::HTTP_PROXY_CONFIG)
      : HttpIntegrationTest(downstream_protocol, version, config) {}

  HttpIntegrationTest(Http::CodecClient::Type downstream_protocol,
                      Network::Address::IpVersion version,
                      const std::string& config = ConfigHelper::HTTP_PROXY_CONFIG);

  HttpIntegrationTest(Http::CodecClient::Type downstream_protocol,
                      const InstanceConstSharedPtrFn& upstream_address_fn,
                      Network::Address::IpVersion version,
                      const std::string& config = ConfigHelper::HTTP_PROXY_CONFIG);
  virtual ~HttpIntegrationTest();

protected:
  IntegrationCodecClientPtr makeHttpConnection(uint32_t port);
  // Makes a http connection object without checking its connected state.
  IntegrationCodecClientPtr makeRawHttpConnection(Network::ClientConnectionPtr&& conn);
  // Makes a http connection object with asserting a connected state.
  IntegrationCodecClientPtr makeHttpConnection(Network::ClientConnectionPtr&& conn);

  // Sets downstream_protocol_ and alters the HTTP connection manager codec type in the
  // config_helper_.
  void setDownstreamProtocol(Http::CodecClient::Type type);

  // Sends |request_headers| and |request_body_size| bytes of body upstream.
  // Configured upstream to send |response_headers| and |response_body_size|
  // bytes of body downstream.
  //
  // Waits for the complete downstream response before returning.
  // Requires |codec_client_| to be initialized.
  IntegrationStreamDecoderPtr
  sendRequestAndWaitForResponse(const Http::TestHeaderMapImpl& request_headers,
                                uint32_t request_body_size,
                                const Http::TestHeaderMapImpl& response_headers,
                                uint32_t response_body_size, int upstream_index = 0);

  // Wait for the end of stream on the next upstream stream on any of the provided fake upstreams.
  // Sets fake_upstream_connection_ to the connection and upstream_request_ to stream.
  // In cases where the upstream that will receive the request is not deterministic, a second
  // upstream index may be provided, in which case both upstreams will be checked for requests.
  uint64_t waitForNextUpstreamRequest(const std::vector<uint64_t>& upstream_indices);
  void waitForNextUpstreamRequest(uint64_t upstream_index = 0);

  // Close |codec_client_| and |fake_upstream_connection_| cleanly.
  void cleanupUpstreamAndDownstream();

  // Utility function to add filters.
  void addFilters(std::vector<std::string> filters);

  // Check for completion of upstream_request_, and a simple "200" response.
  void checkSimpleRequestSuccess(uint64_t expected_request_size, uint64_t expected_response_size,
                                 IntegrationStreamDecoder* response);

  typedef std::function<Network::ClientConnectionPtr()> ConnectionCreationFunction;
  // Sends a simple header-only HTTP request, and waits for a response.
  IntegrationStreamDecoderPtr makeHeaderOnlyRequest(ConnectionCreationFunction* create_connection,
                                                    int upstream_index);
  void testRouterNotFound();
  void testRouterNotFoundWithBody();

  void testRouterRequestAndResponseWithBody(uint64_t request_size, uint64_t response_size,
                                            bool big_header,
                                            ConnectionCreationFunction* creator = nullptr);
  void testRouterHeaderOnlyRequestAndResponse(ConnectionCreationFunction* creator = nullptr,
                                              int upstream_index = 0);
  void testRequestAndResponseShutdownWithActiveConnection();

  // Disconnect tests
  void testRouterUpstreamDisconnectBeforeRequestComplete();
  void
  testRouterUpstreamDisconnectBeforeResponseComplete(ConnectionCreationFunction* creator = nullptr);
  void testRouterDownstreamDisconnectBeforeRequestComplete(
      ConnectionCreationFunction* creator = nullptr);
  void testRouterDownstreamDisconnectBeforeResponseComplete(
      ConnectionCreationFunction* creator = nullptr);
  void testRouterUpstreamResponseBeforeRequestComplete();

  void testTwoRequests(bool force_network_backup = false);
  void testLargeRequestHeaders(uint32_t size, uint32_t max_size = 60);

  void testAddEncodedTrailers();
  void testRetry();
  void testRetryHittingBufferLimit();
  void testRetryAttemptCountHeader();
  void testGrpcRetry();

  void testEnvoyHandling100Continue(bool additional_continue_from_upstream = false,
                                    const std::string& via = "");
  void testEnvoyProxying100Continue(bool continue_before_upstream_complete = false,
                                    bool with_encoder_filter = false);

  // HTTP/2 client tests.
  void testDownstreamResetBeforeResponseComplete();
  void testTrailers(uint64_t request_size, uint64_t response_size);

  Http::CodecClient::Type downstreamProtocol() const { return downstream_protocol_; }
  // Prefix listener stat with IP:port, including IP version dependent loopback address.
  std::string listenerStatPrefix(const std::string& stat_name);

  // The client making requests to Envoy.
  IntegrationCodecClientPtr codec_client_;
  // A placeholder for the first upstream connection.
  FakeHttpConnectionPtr fake_upstream_connection_;
  // A placeholder for the first request received at upstream.
  FakeStreamPtr upstream_request_;
  // A pointer to the request encoder, if used.
  Http::StreamEncoder* request_encoder_{nullptr};
  // The response headers sent by sendRequestAndWaitForResponse() by default.
  Http::TestHeaderMapImpl default_response_headers_{{":status", "200"}};
  Http::TestHeaderMapImpl default_request_headers_{
      {":method", "GET"}, {":path", "/test/long/url"}, {":scheme", "http"}, {":authority", "host"}};
  // The codec type for the client-to-Envoy connection
  Http::CodecClient::Type downstream_protocol_{Http::CodecClient::Type::HTTP1};
};
} // namespace Envoy
