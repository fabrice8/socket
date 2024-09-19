#ifndef SOCKET_RUNTIME_CORE_CONDUIT_H
#define SOCKET_RUNTIME_CORE_CONDUIT_H

#include "../module.hh"
#include "timers.hh"
#include <iostream>

namespace SSC {
  class Core;

  class CoreConduit : public CoreModule {
    public:
      using Options = std::unordered_map<String, String>;
      using StartCallback = Function<void()>;

      struct EncodedMessage {
        Options options;
        Vector<uint8_t> payload;

        inline String get (const String& key) const {
          const auto it = options.find(key);
          if (it != options.end()) {
            return it->second;
          }
          return "";
        }

        inline bool has (const String& key) const {
          const auto it = options.find(key);
          if (it != options.end()) {
            return true;
          }
          return false;
        }

        inline String pluck (const String& key) {
          auto it = options.find(key);
          if (it != options.end()) {
            String value = it->second;
            options.erase(it);
            return value;
          }
          return "";
        }

        inline Map getOptionsAsMap () {
          Map map;

          for (const auto& pair : this->options) {
            map.insert(pair);
          }
          return map;
        }
      };

      class Client {
        public:
          using CloseCallback = Function<void()>;
          using ID = uint64_t;

          // client state
          ID id = 0;
          ID clientId = 0;
          Atomic<bool> isHandshakeDone = false;
          Atomic<bool> isClosing = false;
          Atomic<bool> isClosed = false;

          // uv state
          uv_tcp_t handle;
          uv_buf_t buffer;
          uv_stream_t* stream = nullptr;

          // websocket frame buffer state
          unsigned char *frameBuffer;
          size_t frameBufferSize;

          CoreConduit* conduit = nullptr;

          Client (CoreConduit* conduit)
            : conduit(conduit),
              id(0),
              clientId(0),
              isHandshakeDone(0)
          {}

          ~Client ();

          bool emit (
            const CoreConduit::Options& options,
            SharedPointer<char[]> payload,
            size_t length,
            int opcode = 2,
            const Function<void()> callback = nullptr
          );

          void close (const CloseCallback& callback = nullptr);
      };

      // state
      std::map<uint64_t, Client*> clients;
      Atomic<bool> isStarting = false;
      Atomic<int> port = 0;
      Mutex mutex;

      CoreConduit (Core* core) : CoreModule(core) {};
      ~CoreConduit ();

      // codec
      EncodedMessage decodeMessage (const Vector<uint8_t>& data);
      Vector<uint8_t> encodeMessage (
        const Options& options,
        const Vector<uint8_t>& payload
      );

      // client access
      bool has (uint64_t id);
      CoreConduit::Client* get (uint64_t id);

      // lifecycle
      void start (const StartCallback& callback = nullptr);
      void stop ();
      bool isActive ();

    private:
      uv_tcp_t socket;
      struct sockaddr_in addr;

      void handshake (Client* client, const char *request);
      void processFrame (Client* client, const char *frame, ssize_t size);
  };
}
#endif
