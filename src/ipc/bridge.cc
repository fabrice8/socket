#include "../extension/extension.hh"
#include "../window/window.hh"
#include "../core/protocol_handlers.hh"
#include "ipc.hh"

extern const SSC::Map SSC::getUserConfig ();
extern bool SSC::isDebugEnabled ();

namespace SSC::IPC {
  static Vector<Bridge*> instances;
  static Mutex mutex;

  // create a proxy module so imports of the module of concern are imported
  // exactly once at the canonical URL (file:///...) in contrast to module
  // URLs (socket:...)

  static constexpr auto moduleTemplate =
  R"S(
  import module from '{{url}}'
  export * from '{{url}}'
  export default module
  )S";

  static const Vector<String> allowedNodeCoreModules = {
    "async_hooks",
    "assert",
    "buffer",
    "console",
    "constants",
    "child_process",
    "crypto",
    "dgram",
    "dns",
    "dns/promises",
    "events",
    "fs",
    "fs/constants",
    "fs/promises",
    "http",
    "https",
    "ip",
    "module",
    "net",
    "os",
    "os/constants",
    "path",
    "path/posix",
    "path/win32",
    "perf_hooks",
    "process",
    "querystring",
    "stream",
    "stream/web",
    "string_decoder",
    "sys",
    "test",
    "timers",
    "timers/promises",
    "tty",
    "url",
    "util",
    "vm",
    "worker_threads"
  };

#if SSC_PLATFORM_DESKTOP
  static FileSystemWatcher* developerResourcesFileSystemWatcher = nullptr;
  static void initializeDeveloperResourcesFileSystemWatcher (SharedPointer<Core> core) {
    auto defaultUserConfig = SSC::getUserConfig();
    if (
      developerResourcesFileSystemWatcher == nullptr &&
      isDebugEnabled() &&
      defaultUserConfig["webview_watch"] == "true"
    ) {
      developerResourcesFileSystemWatcher = new FileSystemWatcher(getcwd());
      developerResourcesFileSystemWatcher->core = core.get();
      developerResourcesFileSystemWatcher->start([=](
        const auto& path,
        const auto& events,
        const auto& context
      ) mutable {
        Lock lock(SSC::IPC::mutex);

        static const auto cwd = getcwd();
        const auto relativePath = fs::relative(path, cwd).string();
        const auto json = JSON::Object::Entries {{"path", relativePath}};
        const auto result = SSC::IPC::Result(json);

        for (auto& bridge : instances) {
          auto userConfig = bridge->userConfig;
          if (
            !platform.ios &&
            !platform.android &&
            userConfig["webview_watch"] == "true" &&
            bridge->userConfig["webview_service_worker_mode"] != "hybrid" &&
            (!userConfig.contains("webview_watch_reload") || userConfig.at("webview_watch_reload") != "false")
          ) {
            // check if changed path was a service worker, if so unregister it so it can be reloaded
            for (const auto& entry : developerResourcesFileSystemWatcher->core->serviceWorker.registrations) {
              const auto& registration = entry.second;
            #if defined(__ANDROID__)
              auto scriptURL = String("https://");
            #else
              auto scriptURL = String("socket://");
            #endif

              scriptURL += userConfig["meta_bundle_identifier"];

              if (!relativePath.starts_with("/")) {
                scriptURL += "/";
              }

              scriptURL += relativePath;
              if (registration.scriptURL == scriptURL) {
                // 1. unregister service worker
                // 2. re-register service worker
                // 3. wait for it to be registered
                // 4. emit 'filedidchange' event
                bridge->core->serviceWorker.unregisterServiceWorker(entry.first);
                bridge->core->setTimeout(8, [bridge, result, &registration] () {
                  bridge->core->setInterval(8, [bridge, result, &registration] (auto cancel) {
                    if (registration.state == ServiceWorkerContainer::Registration::State::Activated) {
                      cancel();

                      uint64_t timeout = 500;
                      if (bridge->userConfig["webview_watch_service_worker_reload_timeout"].size() > 0) {
                        try {
                          timeout = std::stoull(bridge->userConfig["webview_watch_service_worker_reload_timeout"]);
                        } catch (...) {}
                      }

                      bridge->core->setTimeout(timeout, [bridge, result] () {
                        bridge->emit("filedidchange", result.json().str());
                      });
                    }
                  });

                  bridge->core->serviceWorker.registerServiceWorker(registration.options);
                });
                return;
              }
            }
          }

          bridge->emit("filedidchange", result.json().str());
        }
      });
    }
  }
#endif

  Bridge::Bridge (SharedPointer<Core> core, Map userConfig)
    : core(core),
      userConfig(userConfig),
      router(this),
      navigator(this),
      schemeHandlers(this)
  {
    Lock lock(SSC::IPC::mutex);
    instances.push_back(this);

    this->id = rand64();

    this->bluetooth.sendFunction = [this](
      const String& seq,
      const JSON::Any value,
      const SSC::Post post
    ) {
      this->send(seq, value.str(), post);
    };

    this->bluetooth.emitFunction = [this](
      const String& seq,
      const JSON::Any value
    ) {
      this->emit(seq, value.str());
    };

    core->networkStatus.addObserver(this->networkStatusObserver, [this](auto json) {
      if (json.has("name")) {
        this->emit(json["name"].str(), json.str());
      }
    });

    core->geolocation.addPermissionChangeObserver(this->geolocationPermissionChangeObserver, [this] (auto json) {
      JSON::Object event = JSON::Object::Entries {
        {"name", "geolocation"},
        {"state", json["state"]}
      };
      this->emit("permissionchange", event.str());
    });

    // on Linux, much of the Notification API is supported so these observers
    // below are not needed as those events already occur in the webview
    // we are patching for the other platforms
  #if !SSC_PLATFORM_LINUX
    core->notifications.addPermissionChangeObserver(this->notificationsPermissionChangeObserver, [this](auto json) {
      JSON::Object event = JSON::Object::Entries {
        {"name", "notifications"},
        {"state", json["state"]}
      };
      this->emit("permissionchange", event.str());
    });

    if (userConfig["permissions_allow_notifications"] != "false") {
      core->notifications.addNotificationResponseObserver(this->notificationResponseObserver, [this](auto json) {
        this->emit("notificationresponse", json.str());
      });

      core->notifications.addNotificationPresentedObserver(this->notificationPresentedObserver, [this](auto json) {
        this->emit("notificationpresented", json.str());
      });
    }
  #endif

  #if SSC_PLATFORM_DESKTOP
    initializeDeveloperResourcesFileSystemWatcher(core);
  #endif
  }

  Bridge::~Bridge () {
    // remove observers
    core->geolocation.removePermissionChangeObserver(this->geolocationPermissionChangeObserver);
    core->networkStatus.removeObserver(this->networkStatusObserver);
    core->notifications.removePermissionChangeObserver(this->notificationsPermissionChangeObserver);
    core->notifications.removeNotificationResponseObserver(this->notificationResponseObserver);
    core->notifications.removeNotificationPresentedObserver(this->notificationPresentedObserver);

    do {
      Lock lock(SSC::IPC::mutex);
      const auto cursor = std::find(instances.begin(), instances.end(), this);
      if (cursor != instances.end()) {
        instances.erase(cursor);
      }

      #if SSC_PLATFORM_DESKTOP
        if (instances.size() == 0) {
          if (developerResourcesFileSystemWatcher) {
            developerResourcesFileSystemWatcher->stop();
            delete developerResourcesFileSystemWatcher;
          }
        }
      #endif
    } while (0);
  }

  void Bridge::init () {
    this->router.init();
    this->navigator.init();
    this->schemeHandlers.init();
  }

  void Bridge::configureWebView (WebView* webview) {
    this->core->notifications.configureWebView(webview);
    this->schemeHandlers.configureWebView(webview);
    this->navigator.configureWebView(webview);
  }

  bool Bridge::evaluateJavaScript (const String& source) {
    if (this->core->shuttingDown) {
      return false;
    }

    if (this->evaluateJavaScriptFunction != nullptr) {
      this->evaluateJavaScriptFunction(source);
      return true;
    }

    return false;
  }

  bool Bridge::dispatch (const DispatchCallback& callback) {
    if (!this->core || this->core->shuttingDown) {
      return false;
    }

    if (this->dispatchFunction != nullptr) {
      this->dispatchFunction(callback);
      return true;
    }

    return false;
  }

  bool Bridge::navigate (const String& url) {
    if (!this->core || this->core->shuttingDown) {
      return false;
    }

    if (this->navigateFunction != nullptr) {
      this->navigateFunction(url);
      return true;
    }

    return false;
  }

  bool Bridge::route (const String& uri, SharedPointer<char *> bytes, size_t size) {
    return this->route(uri, bytes, size, nullptr);
  }

  bool Bridge::route (
    const String& uri,
    SharedPointer<char*> bytes,
    size_t size,
    Router::ResultCallback callback
  ) {
    if (callback != nullptr) {
      return this->router.invoke(uri, bytes, size, callback);
    } else {
      return this->router.invoke(uri, bytes, size);
    }
  }

  bool Bridge::send (
    const Message::Seq& seq,
    const String& data,
    const Post& post
  ) {
    if (this->core->shuttingDown) {
      return false;
    }

    if (post.body != nullptr || seq == "-1") {
      const auto script = this->core->createPost(seq, data, post);
      return this->evaluateJavaScript(script);
    }

    const auto value = encodeURIComponent(data);
    const auto script = getResolveToRenderProcessJavaScript(
      seq.size() == 0 ? "-1" : seq,
      "0",
      value
    );

    return this->evaluateJavaScript(script);
  }

  bool Bridge::send (const Message::Seq& seq, const JSON::Any& json, const Post& post) {
    return this->send(seq, json.str(), post);
  }

  bool Bridge::emit (const String& name, const String& data) {
    if (this->core->shuttingDown) {
      return false;
    }

    const auto value = encodeURIComponent(data);
    const auto script = getEmitToRenderProcessJavaScript(name, value);
    return this->evaluateJavaScript(script);
  }

  bool Bridge::emit (const String& name, const JSON::Any& json) {
    return this->emit(name, json.str());
  }

  const Vector<String>& Bridge::getAllowedNodeCoreModules () const {
    return allowedNodeCoreModules;
  }

  void Bridge::configureSchemeHandlers (const SchemeHandlers::Configuration& configuration) {
    this->schemeHandlers.configure(configuration);
    this->schemeHandlers.registerSchemeHandler("ipc", [this](
      const auto& request,
      const auto bridge,
      auto& callbacks,
      auto callback
    ) {
      auto message = Message(request.url(), true);

      // handle special 'ipc://post' case
      if (message.name == "post") {
        uint64_t id = 0;

        try {
          id = std::stoull(message.get("id"));
        } catch (...) {
          auto response = SchemeHandlers::Response(request, 400);
          response.send(JSON::Object::Entries {
            {"err", JSON::Object::Entries {
              {"message", "Invalid 'id' given in parameters"}
            }}
          });

          callback(response);
          return;
        }

        if (!this->core->hasPost(id)) {
          auto response = SchemeHandlers::Response(request, 404);
          response.send(JSON::Object::Entries {
            {"err", JSON::Object::Entries {
              {"message", "A 'Post' was not found for the given 'id' in parameters"},
              {"type", "NotFoundError"}
            }}
          });

          callback(response);
          return;
        }

        auto response = SchemeHandlers::Response(request, 200);
        const auto post = this->core->getPost(id);

        // handle raw headers in 'Post' object
        if (post.headers.size() > 0) {
          const auto lines = split(trim(post.headers), '\n');
          for (const auto& line : lines) {
            const auto pair = split(trim(line), ':');
            const auto key = trim(pair[0]);
            const auto value = trim(pair[1]);
            response.setHeader(key, value);
          }
        }

        response.write(post.length, post.body);
        callback(response);
        this->core->removePost(id);
        return;
      }

      message.isHTTP = true;
      message.cancel = std::make_shared<MessageCancellation>();

      callbacks.cancel = [message] () {
        if (message.cancel->handler != nullptr) {
          message.cancel->handler(message.cancel->data);
        }
      };

      const auto size = request.body.size;
      const auto bytes = request.body.bytes;
      const auto invoked = this->router.invoke(message, request.body.bytes, size, [request, message, callback](Result result) {
        if (!request.isActive()) {
          return;
        }

        auto response = SchemeHandlers::Response(request);

        response.setHeaders(result.headers);
        response.setHeader("access-control-allow-origin", "*");
        response.setHeader("access-control-allow-methods", "GET, POST, PUT, DELETE");
        response.setHeader("access-control-allow-headers", "*");
        response.setHeader("access-control-allow-credentials", "true");

        // handle event source streams
        if (result.post.eventStream != nullptr) {
          response.setHeader("content-type", "text/event-stream");
          response.setHeader("cache-control", "no-store");
          *result.post.eventStream = [request, response, message, callback](
            const char* name,
            const char* data,
            bool finished
          ) mutable {
            if (request.isCancelled()) {
              if (message.cancel->handler != nullptr) {
                message.cancel->handler(message.cancel->data);
              }
              return false;
            }

            response.writeHead(200);

            const auto event = SchemeHandlers::Response::Event { name, data };

            if (event.count() > 0) {
              response.write(event.str());
            }

            if (finished) {
              callback(response);
            }

            return true;
          };
          return;
        }

        // handle chunk streams
        if (result.post.chunkStream != nullptr) {
          response.setHeader("transfer-encoding", "chunked");
          *result.post.chunkStream = [request, response, message, callback](
            const char* chunk,
            size_t size,
            bool finished
          ) mutable {
            if (request.isCancelled()) {
              if (message.cancel->handler != nullptr) {
                message.cancel->handler(message.cancel->data);
              }
              return false;
            }

            response.writeHead(200);
            response.write(size, chunk);

            if (finished) {
              callback(response);
            }

            return true;
          };
          return;
        }

        if (result.post.body != nullptr) {
          response.write(result.post.length, result.post.body);
        } else {
          response.write(result.json());
        }

        callback(response);
      });

      if (!invoked) {
        auto response = SchemeHandlers::Response(request, 404);
        response.send(JSON::Object::Entries {
          {"err", JSON::Object::Entries {
            {"message", "Not found"},
            {"type", "NotFoundError"},
            {"url", request.url()}
          }}
        });

        return callback(response);
      }

      if (message.get("resolve") == "false") {
        auto response = SchemeHandlers::Response(request, 200);
        return callback(response);
      }
    });

    this->schemeHandlers.registerSchemeHandler("socket", [this](
      const auto& request,
      const auto bridge,
      auto& callbacks,
      auto callback
    ) {
      auto userConfig = this->userConfig;
      auto bundleIdentifier = userConfig["meta_bundle_identifier"];
      // the location of static application resources
      const auto applicationResources = FileResource::getResourcesPath().string();
      // default response is 404
      auto response = SchemeHandlers::Response(request, 404);

      // the resouce path that may be request
      String resourcePath;

      // the content location relative to the request origin
      String contentLocation;

      // application resource or service worker request at `socket://<bundle_identifier>/*`
      if (request.hostname == bundleIdentifier) {
        const auto resolved = this->navigator.location.resolve(request.pathname, applicationResources);

        if (resolved.redirect) {
          if (request.method == "GET") {
            auto location = resolved.pathname;
            if (request.query.size() > 0) {
              location += "?" + request.query;
            }

            if (request.fragment.size() > 0) {
              location += "#" + request.fragment;
            }

            response.redirect(location);
            return callback(response);
          }
        } else if (resolved.isResource()) {
          resourcePath = resolved.pathname;
        } else if (resolved.isMount()) {
          resourcePath = resolved.mount.filename;
        } else if (request.pathname == "" || request.pathname == "/") {
          if (userConfig.contains("webview_default_index")) {
            resourcePath = userConfig["webview_default_index"];
            if (resourcePath.starts_with("./")) {
              resourcePath = applicationResources + resourcePath.substr(1);
            } else if (resourcePath.starts_with("/")) {
              resourcePath = applicationResources + resourcePath;
            } else {
              resourcePath = applicationResources + + "/" + resourcePath;
            }
          }
        }

        if (resourcePath.size() == 0 && resolved.pathname.size() > 0) {
          resourcePath = applicationResources + resolved.pathname;
        }

        // handle HEAD and GET requests for a file resource
        if (resourcePath.size() > 0) {
          contentLocation = replace(resourcePath, applicationResources, "");

          auto resource = FileResource(resourcePath);

          if (!resource.exists()) {
            response.writeHead(404);
          } else {
            if (contentLocation.size() > 0) {
              response.setHeader("content-location", contentLocation);
            }

            if (request.method == "OPTIONS") {
              response.setHeader("access-control-allow-origin", "*");
              response.setHeader("access-control-allow-methods", "GET, HEAD");
              response.setHeader("access-control-allow-headers", "*");
              response.setHeader("access-control-allow-credentials", "true");
              response.writeHead(200);
            }

            if (request.method == "HEAD") {
              const auto contentType = resource.mimeType();
              const auto contentLength = resource.size();

              if (contentType.size() > 0) {
                response.setHeader("content-type", contentType);
              }

              if (contentLength > 0) {
                response.setHeader("content-length", contentLength);
              }

              response.writeHead(200);
            }

            if (request.method == "GET") {
              if (resource.mimeType() != "text/html") {
                response.send(resource);
              } else {
                const auto html = injectHTMLPreload(
                  this->core.get(),
                  userConfig,
                  resource.str(),
                  this->preload
                );

                response.setHeader("content-type", "text/html");
                response.setHeader("content-length", html.size());
                response.writeHead(200);
                response.write(html);
              }
            }
          }

          return callback(response);
        }

        if (this->core->serviceWorker.registrations.size() > 0) {
          const auto fetch = ServiceWorkerContainer::FetchRequest {
            request.method,
            request.scheme,
            request.hostname,
            request.pathname,
            request.query,
            request.headers,
            ServiceWorkerContainer::FetchBuffer { request.body.size, request.body.bytes },
            ServiceWorkerContainer::Client { request.client.id, this->preload }
          };

          const auto fetched = this->core->serviceWorker.fetch(fetch, [request, callback, response] (auto res) mutable {
            if (!request.isActive()) {
              return;
            }

            if (res.statusCode == 0) {
              response.fail("ServiceWorker request failed");
            } else {
              response.writeHead(res.statusCode, res.headers);
              response.write(res.buffer.size, res.buffer.bytes);
            }

            callback(response);
          });

          if (fetched) {
            this->core->setTimeout(32000, [request] () mutable {
              if (request.isActive()) {
                auto response = SchemeHandlers::Response(request, 408);
                response.fail("ServiceWorker request timed out.");
              }
            });
            return;
          }
        }

        response.writeHead(404);
        return callback(response);
      }

      // module or stdlib import/fetch `socket:<module>/<path>` which will just
      // proxy an import into a normal resource request above
      if (request.hostname.size() == 0) {
        auto pathname = request.pathname;

        if (!pathname.ends_with(".js")) {
          pathname += ".js";
        }

        if (!pathname.starts_with("/")) {
          pathname = "/" + pathname;
        }

        resourcePath = applicationResources + "/socket" + pathname;
        contentLocation = "/socket" + pathname;

        auto resource = FileResource(resourcePath);

        if (resource.exists()) {
          const auto url = "socket://" + bundleIdentifier + "/socket" + pathname;
          const auto module = tmpl(moduleTemplate, Map {{"url", url}});
          const auto contentType = resource.mimeType();

          if (contentType.size() > 0) {
            response.setHeader("content-type", contentType);
          }

          response.setHeader("content-length", module.size());

          if (contentLocation.size() > 0) {
            response.setHeader("content-location", contentLocation);
          }

          response.writeHead(200);
          response.write(trim(module));
        }

        return callback(response);
      }

      response.writeHead(404);
      callback(response);
    });

    this->schemeHandlers.registerSchemeHandler("node", [this](
      const auto& request,
      const auto router,
      auto& callbacks,
      auto callback
    ) {
      auto userConfig = this->userConfig;
      auto bundleIdentifier = userConfig["meta_bundle_identifier"];
      // the location of static application resources
      const auto applicationResources = FileResource::getResourcesPath().string();
      // default response is 404
      auto response = SchemeHandlers::Response(request, 404);

      // the resouce path that may be request
      String resourcePath;

      // the content location relative to the request origin
      String contentLocation;

      // module or stdlib import/fetch `socket:<module>/<path>` which will just
      // proxy an import into a normal resource request above
      if (request.hostname.size() == 0) {
        static const auto allowedNodeCoreModules = this->getAllowedNodeCoreModules();
        const auto isAllowedNodeCoreModule = allowedNodeCoreModules.end() != std::find(
          allowedNodeCoreModules.begin(),
          allowedNodeCoreModules.end(),
          request.pathname.substr(1)
        );

        if (!isAllowedNodeCoreModule) {
          response.writeHead(404);
          return callback(response);
        }

        auto pathname = request.pathname;

        if (!pathname.ends_with(".js")) {
          pathname += ".js";
        }

        if (!pathname.starts_with("/")) {
          pathname = "/" + pathname;
        }

        contentLocation = "/socket" + pathname;
        resourcePath = applicationResources + contentLocation;

        auto resource = FileResource(resourcePath);

        if (!resource.exists()) {
          if (!pathname.ends_with(".js")) {
            pathname = request.pathname;

            if (!pathname.starts_with("/")) {
              pathname = "/" + pathname;
            }

            if (pathname.ends_with("/")) {
              pathname = pathname.substr(0, pathname.size() - 1);
            }

            contentLocation = "/socket" + pathname + "/index.js";
            resourcePath = applicationResources + contentLocation;
          }

          resource = FileResource(resourcePath);
        }

        if (resource.exists()) {
          const auto url = "socket://" + bundleIdentifier + "/socket" + pathname;
          const auto module = tmpl(moduleTemplate, Map {{"url", url}});
          const auto contentType = resource.mimeType();

          if (contentType.size() > 0) {
            response.setHeader("content-type", contentType);
          }

          response.setHeader("content-length", module.size());

          if (contentLocation.size() > 0) {
            response.setHeader("content-location", contentLocation);
          }

          response.writeHead(200);
          response.write(trim(module));
        }

        return callback(response);
      }

      response.writeHead(404);
      callback(response);
    });

    Map protocolHandlers = {
      {"npm", "/socket/npm/service-worker.js"}
    };

    for (const auto& entry : split(this->userConfig["webview_protocol-handlers"], " ")) {
      const auto scheme = replace(trim(entry), ":", "");
      if (this->core->protocolHandlers.registerHandler(scheme)) {
        protocolHandlers.insert_or_assign(scheme, "");
      }
    }

    for (const auto& entry : this->userConfig) {
      const auto& key = entry.first;
      if (key.starts_with("webview_protocol-handlers_")) {
        const auto scheme = replace(replace(trim(key), "webview_protocol-handlers_", ""), ":", "");;
        const auto data = entry.second;
        if (this->core->protocolHandlers.registerHandler(scheme, { data })) {
          protocolHandlers.insert_or_assign(scheme, data);
        }
      }
    }

    for (const auto& entry : protocolHandlers) {
      const auto& scheme = entry.first;
      const auto id = rand64();

      auto scriptURL = trim(entry.second);

      if (scriptURL.size() == 0) {
        continue;
      }

      if (!scriptURL.starts_with(".") && !scriptURL.starts_with("/")) {
        continue;
      }

      if (scriptURL.starts_with(".")) {
        scriptURL = scriptURL.substr(1, scriptURL.size());
      }

      String scope = "/";

      auto scopeParts = split(scriptURL, "/");
      if (scopeParts.size() > 0) {
        scopeParts = Vector<String>(scopeParts.begin(), scopeParts.end() - 1);
        scope = join(scopeParts, "/");
      }

      scriptURL = (
      #if SSC_PLATFORM_ANDROID
        "https://" +
      #else
        "socket://" +
      #endif
        this->userConfig["meta_bundle_identifier"] +
        scriptURL
      );

      this->core->serviceWorker.registerServiceWorker({
        .type = ServiceWorkerContainer::RegistrationOptions::Type::Module,
        .scope = scope,
        .scriptURL = scriptURL,
        .scheme = scheme,
        .id = id
      });

      this->schemeHandlers.registerSchemeHandler(scheme, [this](
        const auto& request,
        const auto bridge,
        auto& callbacks,
        auto callback
      ) {
        if (this->core->serviceWorker.registrations.size() > 0) {
          auto hostname = request.hostname;
          auto pathname = request.pathname;

          if (request.scheme == "npm") {
            hostname = this->userConfig["meta_bundle_identifier"];
          }

          const auto scope = this->core->protocolHandlers.getServiceWorkerScope(request.scheme);

          if (scope.size() > 0) {
            pathname = scope + pathname;
          }

          const auto fetch = ServiceWorkerContainer::FetchRequest {
            request.method,
            request.scheme,
            hostname,
            pathname,
            request.query,
            request.headers,
            ServiceWorkerContainer::FetchBuffer { request.body.size, request.body.bytes },
            ServiceWorkerContainer::Client { request.client.id, this->preload }
          };

          const auto fetched = this->core->serviceWorker.fetch(fetch, [request, callback] (auto res) mutable {
            if (!request.isActive()) {
              return;
            }

            auto response = SchemeHandlers::Response(request);

            if (res.statusCode == 0) {
              response.fail("ServiceWorker request failed");
            } else {
              response.writeHead(res.statusCode, res.headers);
              response.write(res.buffer.size, res.buffer.bytes);
            }

            callback(response);
          });

          if (fetched) {
            this->core->setTimeout(32000, [request] () mutable {
              if (request.isActive()) {
                auto response = SchemeHandlers::Response(request, 408);
                response.fail("Protocol handler ServiceWorker request timed out.");
              }
            });
            return;
          }
        }

        auto response = SchemeHandlers::Response(request);
        response.writeHead(404);
        callback(response);
      });
    }
  }
}
