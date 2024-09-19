#ifndef SOCKET_RUNTIME_CORE_SOCKET_H
#define SOCKET_RUNTIME_CORE_SOCKET_H

#include "../platform/platform.hh"
#include "post.hh"

namespace SSC {
  class Core;
  typedef enum {
    SOCKET_TYPE_NONE = 0,
    SOCKET_TYPE_TCP = 1 << 1,
    SOCKET_TYPE_UDP = 1 << 2,
    SOCKET_TYPE_MAX = 0xF
  } socket_type_t;

  typedef enum {
    SOCKET_FLAG_NONE = 0,
    SOCKET_FLAG_EPHEMERAL = 1 << 1
  } socket_flag_t;

  typedef enum {
    SOCKET_STATE_NONE = 0,
    // general states
    SOCKET_STATE_CLOSED = 1 << 1,
    // udp states (10)
    SOCKET_STATE_UDP_BOUND = 1 << 10,
    SOCKET_STATE_UDP_CONNECTED = 1 << 11,
    SOCKET_STATE_UDP_RECV_STARTED = 1 << 12,
    SOCKET_STATE_UDP_PAUSED = 1 << 13,
    // tcp states (20)
    SOCKET_STATE_TCP_BOUND = 1 << 20,
    SOCKET_STATE_TCP_CONNECTED = 1 << 21,
    SOCKET_STATE_TCP_PAUSED = 1 << 13,
    SOCKET_STATE_MAX = 1 << 0xF
  } socket_state_t;

  struct LocalPeerInfo {
    struct sockaddr_storage addr;
    String address = "";
    String family = "";
    int port = 0;
    int err = 0;

    int getsockname (uv_udp_t *socket, struct sockaddr *addr);
    int getsockname (uv_tcp_t *socket, struct sockaddr *addr);
    void init (uv_udp_t *socket);
    void init (uv_tcp_t *socket);
    void init (const struct sockaddr_storage *addr);
  };

  struct RemotePeerInfo {
    struct sockaddr_storage addr;
    String address = "";
    String family = "";
    int port = 0;
    int err = 0;

    int getpeername (uv_udp_t *socket, struct sockaddr *addr);
    int getpeername (uv_tcp_t *socket, struct sockaddr *addr);
    void init (uv_udp_t *socket);
    void init (uv_tcp_t *socket);
    void init (const struct sockaddr_storage *addr);
  };

  /**
   * A generic structure for a bound or connected socket.
   */
  class Socket {
    public:
      struct RequestContext {
        using Callback = Function<void(int, Post)>;
        SharedPointer<char[]> bytes = nullptr;
        size_t size = 0;
        uv_buf_t buffer;
        Callback callback;
        Socket* socket = nullptr;
        RequestContext (Callback callback) { this->callback = callback; }
        RequestContext (size_t size, SharedPointer<char[]> bytes, Callback callback) {
          if (bytes != nullptr) {
            this->buffer = uv_buf_init(bytes.get(), size);
            this->bytes = bytes;
          }
          this->size = size;
          this->callback = callback;
        }
      };

      using UDPReceiveCallback = Function<void(
        ssize_t,
        const uv_buf_t*,
        const struct sockaddr*
      )>;

      // uv handles
      union {
        uv_udp_t udp;
        uv_tcp_t tcp; // XXX: FIXME
      } handle;

      // sockaddr
      struct sockaddr_in addr;

      // callbacks
      UDPReceiveCallback receiveCallback;
      Vector<Function<void()>> onclose;

      // instance state
      uint64_t id = 0;
      Mutex mutex;
      Core *core = nullptr;

      struct {
        struct {
          bool reuseAddr = false;
          bool ipv6Only = false; // @TODO
        } udp;
      } options;

      // peer state
      LocalPeerInfo local;
      RemotePeerInfo remote;
      socket_type_t type = SOCKET_TYPE_NONE;
      socket_flag_t flags = SOCKET_FLAG_NONE;
      socket_state_t state = SOCKET_STATE_NONE;

      /**
      * Private `Socket` class constructor
      */
      Socket (Core *core, socket_type_t peerType, uint64_t peerId, bool isEphemeral);
      ~Socket ();

      int init ();
      int initRemotePeerInfo ();
      int initLocalPeerInfo ();
      void addState (socket_state_t value);
      void removeState (socket_state_t value);
      bool hasState (socket_state_t value);
      const RemotePeerInfo* getRemotePeerInfo ();
      const LocalPeerInfo* getLocalPeerInfo ();
      bool isUDP ();
      bool isTCP ();
      bool isEphemeral ();
      bool isBound ();
      bool isActive ();
      bool isClosing ();
      bool isClosed ();
      bool isConnected ();
      bool isPaused ();
      int bind ();
      int bind (const String& address, int port);
      int bind (const String& address, int port, bool reuseAddr);
      int rebind ();
      int connect (const String& address, int port);
      int disconnect ();
      void send (
        SharedPointer<char[]> bytes,
        size_t size,
        int port,
        const String& address,
        const Socket::RequestContext::Callback& callback
      );
      int recvstart ();
      int recvstart (UDPReceiveCallback onrecv);
      int recvstop ();
      int resume ();
      int pause ();
      void close ();
      void close (Function<void()> onclose);
  };
}
#endif
