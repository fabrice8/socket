#include "../core/platform.hh"
#include "navigator.hh"
#include "bridge.hh"

#include "../window/window.hh"

#if SSC_PLATFORM_APPLE
@implementation SSCNavigationDelegate
-    (void) webView: (WKWebView*) webview
  didFailNavigation: (WKNavigation*) navigation
          withError: (NSError*) error
{
  // TODO(@jwerle)
}

-               (void) webView: (WKWebView*) webview
  didFailProvisionalNavigation: (WKNavigation*) navigation
                     withError: (NSError*) error
{
  // TODO(@jwerle)
}

-                    (void) webView: (WKWebView*) webview
    decidePolicyForNavigationAction: (WKNavigationAction*) navigationAction
                    decisionHandler: (void (^)(WKNavigationActionPolicy)) decisionHandler
{
  using namespace SSC;
  if (
    webview != nullptr &&
    webview.URL != nullptr &&
    webview.URL.absoluteString.UTF8String != nullptr &&
    navigationAction != nullptr &&
    navigationAction.request.URL.absoluteString.UTF8String != nullptr
  ) {
    const auto currentURL = String(webview.URL.absoluteString.UTF8String);
    const auto requestedURL = String(navigationAction.request.URL.absoluteString.UTF8String);

    if (!self.navigator->handleNavigationRequest(currentURL, requestedURL)) {
      return decisionHandler(WKNavigationActionPolicyCancel);
    }
  }

  decisionHandler(WKNavigationActionPolicyAllow);
}

-                    (void) webView: (WKWebView*) webview
  decidePolicyForNavigationResponse: (WKNavigationResponse*) navigationResponse
                    decisionHandler: (void (^)(WKNavigationResponsePolicy)) decisionHandler
{
  decisionHandler(WKNavigationResponsePolicyAllow);
}
@end
#endif

namespace SSC::IPC {
  Navigator::Location::Location (Bridge* bridge)
    : bridge(bridge),
      URL()
  {}

  void Navigator::Location::init () {
    // determine HOME
  #if SSC_PLATFORM_WINDOWS
    static const auto HOME = Env::get("HOMEPATH", Env::get("USERPROFILE", Env::get("HOME")));
  #elif SSC_PLATFORM_IOS
    static const auto HOME = String(NSHomeDirectory().UTF8String);
  #else
    static const auto uid = getuid();
    static const auto pwuid = getpwuid(uid);
    static const auto HOME = pwuid != nullptr
      ? String(pwuid->pw_dir)
      : Env::get("HOME", getcwd());
  #endif

    static const Map mappings = {
      {"\\$HOST_HOME", HOME},
      {"~", HOME},

      {"\\$HOST_CONTAINER",
      #if SSC_PLATFORM_IOS
        [NSSearchPathForDirectoriesInDomains(NSApplicationDirectory, NSUserDomainMask, YES) objectAtIndex: 0].UTF8String
      #elif SSC_PLATFORM_MACOS
        // `homeDirectoryForCurrentUser` resolves to sandboxed container
        // directory when in "sandbox" mode, otherwise the user's HOME directory
        NSFileManager.defaultManager.homeDirectoryForCurrentUser.absoluteString.UTF8String
      #elif SSC_PLATFORM_LINUX || SSC_PLATFORM_ANDROID
        // TODO(@jwerle): figure out `$HOST_CONTAINER` for Linux/Android
        getcwd()
      #elif SSC_PLATFORM_WINDOWS
        // TODO(@jwerle): figure out `$HOST_CONTAINER` for Windows
        getcwd()
      #else
        getcwd()
      #endif
      },

      {"\\$HOST_PROCESS_WORKING_DIRECTORY",
      #if SSC_PLATFORM_APPLE
        NSBundle.mainBundle.resourcePath.UTF8String
      #else
        getcwd()
      #endif
      }
    };

    for (const auto& entry : bridge->userConfig) {
      if (entry.first.starts_with("webview_navigator_mounts_")) {
        auto key = replace(entry.first, "webview_navigator_mounts_", "");

        if (key.starts_with("android") && !platform.android) continue;
        if (key.starts_with("ios") && !platform.ios) continue;
        if (key.starts_with("linux") && !platform.linux) continue;
        if (key.starts_with("mac") && !platform.mac) continue;
        if (key.starts_with("win") && !platform.win) continue;

        key = replace(key, "android_", "");
        key = replace(key, "ios_", "");
        key = replace(key, "linux_", "");
        key = replace(key, "mac_", "");
        key = replace(key, "win_", "");

        String path = key;

        for (const auto& map : mappings) {
          path = replace(path, map.first, map.second);
        }

        const auto& value = entry.second;
        this->mounts.insert_or_assign(path, value);
      }
    }
  }

  /**
   * .
   * ├── a-conflict-index
   * │             └── index.html
   * ├── a-conflict-index.html
   * ├── an-index-file
   * │             ├── a-html-file.html
   * │             └── index.html
   * ├── another-file.html
   * └── index.html
   *
   * Subtleties:
   * Direct file navigation always wins
   * /foo/index.html have precedent over foo.html
   * /foo redirects to /foo/ when there is a /foo/index.html
   *
   * '/' -> '/index.html'
   * '/index.html' -> '/index.html'
   * '/a-conflict-index' -> redirect to '/a-conflict-index/'
   * '/another-file' -> '/another-file.html'
   * '/another-file.html' -> '/another-file.html'
   * '/an-index-file/' -> '/an-index-file/index.html'
   * '/an-index-file' -> redirect to '/an-index-file/'
   * '/an-index-file/a-html-file' -> '/an-index-file/a-html-file.html'
   **/
  static const Navigator::Location::Resolution resolveLocationPathname (
    const String& pathname,
    const String& dirname
  ) {
    auto result = pathname;

    if (result.starts_with("/")) {
      result = result.substr(1);
    }

    // Resolve the full path
    const auto filename = (fs::path(dirname) / fs::path(result)).make_preferred();

    // 1. Try the given path if it's a file
    if (fs::is_regular_file(filename)) {
      return Navigator::Location::Resolution {
        .pathname = "/" + replace(fs::relative(filename, dirname).string(), "\\\\", "/")
      };
    }

    // 2. Try appending a `/` to the path and checking for an index.html
    const auto index = filename / fs::path("index.html");
    if (fs::is_regular_file(index)) {
      if (filename.string().ends_with("\\") || filename.string().ends_with("/")) {
        return Navigator::Location::Resolution {
          .pathname = "/" + replace(fs::relative(index, dirname).string(), "\\\\", "/"),
          .redirect = false
        };
      } else {
        return Navigator::Location::Resolution {
          .pathname = "/" + replace(fs::relative(filename, dirname).string(), "\\\\", "/") + "/",
          .redirect = true
        };
      }
    }

    // 3. Check if appending a .html file extension gives a valid file
    const auto html = Path(filename).replace_extension(".html");
    if (fs::is_regular_file(html)) {
      return Navigator::Location::Resolution {
        .pathname = "/" + replace(fs::relative(html, dirname).string(), "\\\\", "/")
      };
    }

    // If no valid path is found, return empty string
    return Navigator::Location::Resolution {};
  }

  const Navigator::Location::Resolution Navigator::Location::resolve (const Path& pathname, const Path& dirname) {
    return this->resolve(pathname.string(), dirname.string());
  }

  const Navigator::Location::Resolution Navigator::Location::resolve (const String& pathname, const String& dirname) {
    for (const auto& entry : this->mounts) {
      if (pathname.starts_with(entry.second)) {
        const auto relative = replace(pathname, entry.second, "");
        auto resolution = resolveLocationPathname(relative, entry.first);
        if (resolution.pathname.size() > 0) {
          const auto filename = Path(entry.first) / resolution.pathname.substr(1);
          resolution.type = Navigator::Location::Resolution::Type::Mount;
          resolution.mount.filename = filename;
          return resolution;
        }
      }
    }

    return resolveLocationPathname(pathname, dirname);
  }

  bool Navigator::Location::Resolution::isUnknown () const {
    return this->type == Navigator::Location::Resolution::Type::Unknown;
  }

  bool Navigator::Location::Resolution::isResource () const {
    return this->type == Navigator::Location::Resolution::Type::Resource;
  }

  bool Navigator::Location::Resolution::isMount () const {
    return this->type == Navigator::Location::Resolution::Type::Mount;
  }

  void Navigator::Location::assign (const String& url) {
    this->set(url);
    this->bridge->navigate(url);
  }

  Navigator::Navigator (Bridge* bridge)
    : bridge(bridge),
      location(bridge),
      serviceWorker(bridge->core->serviceWorker)
  {
  #if SSC_PLATFORM_APPLE
    this->navigationDelegate = [SSCNavigationDelegate new];
    this->navigationDelegate.navigator = this;
  #endif
  }

  Navigator::~Navigator () {
  #if SSC_PLATFORM_APPLE
    if (this->navigationDelegate) {
      this->navigationDelegate.navigator = nullptr;

    #if !__has_feature(objc_arc)
      [this->navigationDelegate release];
    #endif
    }

    this->navigationDelegate = nullptr;
  #endif
  }

  void Navigator::init () {
    this->location.init();
  }

  void Navigator::configureWebView (WebView* webview) {
  #if SSC_PLATFORM_APPLE
    webview.navigationDelegate = this->navigationDelegate;
  #elif SSC_PLATFORM_LINUX
    g_signal_connect(
      G_OBJECT(webview),
      "decide-policy",
      G_CALLBACK((+[](
        WebKitWebView* webview,
        WebKitPolicyDecision* decision,
        WebKitPolicyDecisionType decisionType,
        gpointer userData
      ) {
        auto navigator = reinterpret_cast<Navigator*>(userData);

        if (decisionType != WEBKIT_POLICY_DECISION_TYPE_NAVIGATION_ACTION) {
          webkit_policy_decision_use(decision);
          return true;
        }

        const auto navigation = WEBKIT_NAVIGATION_POLICY_DECISION(decision);
        const auto action = webkit_navigation_policy_decision_get_navigation_action(navigation);
        const auto request = webkit_navigation_action_get_request(action);
        const auto currentURL = String(webkit_web_view_get_uri(webview));
        const auto requestedURL = String(webkit_uri_request_get_uri(request)

        if (!navigator->handleNavigationRequest(currentURL, requestedURL)) {
          webkit_policy_decision_ignore(decision);
          return false;
        }

        return true;
      })),
      this
    );
  #elif SSC_PLATFORM_WINDOWS
    EventRegistrationToken tokenNavigation;
    webview->add_NavigationStarting(
      Microsoft::WRL::Callback<ICoreWebView2NavigationStartingEventHandler>(
        [this, &](ICoreWebView2* webview, ICoreWebView2NavigationStartingEventArgs *event) {
          PWSTR source;
          PWSTR uri;

          event->get_Uri(&uri);
          webview->get_Source(&source);

          if (uri == nullptr || source == nullptr) {
            if (uri) CoTaskMemFree(uri);
            if (source) CoTaskMemFree(source);
            return E_POIINTER;
          }

          const auto requestedURL = convertWStringToString(uri);
          const auto currentURL = convertWStringToString(source);

          if (!this->handleNavigationRequest(currentURL, requestedURL)) {
            event->put_Cancel(true);
          }

          CoTaskMemFree(uri);
          CoTaskMemFree(source);
          return S_OK;
        }
      ).Get(),
      &tokenNavigation
    );
  #endif
  }

  bool Navigator::handleNavigationRequest (
    const String& currentURL,
    const String& requestedURL
  ) {
    auto userConfig = this->bridge->userConfig;
    const auto links = parseStringList(userConfig["meta_application_links"], ' ');
    const auto applinks = parseStringList(userConfig["meta_application_links"], ' ');
    const auto currentURLComponents = URL::Components::parse(currentURL);

    bool hasAppLink = false;
    if (applinks.size() > 0 && currentURLComponents.authority.size() > 0) {
      const auto host = currentURLComponents.authority;
      for (const auto& applink : applinks) {
        const auto parts = split(applink, '?');
        if (host == parts[0]) {
          hasAppLink = true;
          break;
        }
      }
    }

    if (hasAppLink) {
      JSON::Object json = JSON::Object::Entries {{
        "url", requestedURL
      }};

      this->bridge->emit("applicationurl", json.str());
      return false;
    }

    if (
      userConfig["meta_application_protocol"].size() > 0 &&
      requestedURL.starts_with(userConfig["meta_application_protocol"]) &&
      !requestedURL.starts_with("socket://" + userConfig["meta_bundle_identifier"])
    ) {

      SSC::JSON::Object json = SSC::JSON::Object::Entries {{
        "url", requestedURL
      }};

      this->bridge->emit("applicationurl", json.str());
      return false;
    }

    if (!this->isNavigationRequestAllowed(currentURL, requestedURL)) {
      debug("Navigation was ignored for: %s", requestedURL.c_str());
      return false;
    }

    return true;
  }

  bool Navigator::isNavigationRequestAllowed (
    const String& currentURL,
    const String& requestedURL
  ) {
    static const auto devHost = getDevHost();
    auto userConfig = this->bridge->userConfig;
    const auto allowed = split(trim(userConfig["webview_navigator_policies_allowed"]), ' ');

    for (const auto& entry : split(userConfig["webview_protocol-handlers"], " ")) {
      const auto scheme = replace(trim(entry), ":", "");
      if (requestedURL.starts_with(scheme + ":")) {
        return true;
      }
    }

    for (const auto& entry : userConfig) {
      const auto& key = entry.first;
      if (key.starts_with("webview_protocol-handlers_")) {
        const auto scheme = replace(replace(trim(key), "webview_protocol-handlers_", ""), ":", "");;
        if (requestedURL.starts_with(scheme + ":")) {
          return true;
        }
      }
    }

    for (const auto& entry : allowed) {
      String pattern = entry;
      pattern = replace(pattern, "\\.", "\\.");
      pattern = replace(pattern, "\\*", "(.*)");
      pattern = replace(pattern, "\\.\\.\\*", "(.*)");
      pattern = replace(pattern, "\\/", "\\/");

      try {
        std::regex regex(pattern);
        std::smatch match;

        if (std::regex_match(requestedURL, match, regex, std::regex_constants::match_any)) {
          return true;
        }
      } catch (...) {}
    }

    if (
      requestedURL.starts_with("socket:") ||
      requestedURL.starts_with("npm:") ||
      requestedURL.starts_with(devHost)
    ) {
      return true;
    }

    return false;
  }
}
