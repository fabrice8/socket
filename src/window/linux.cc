#include <fstream>

#include "../app/app.hh"
#include "window.hh"

static GtkTargetEntry droppableTypes[] = {
  { (char*) "text/uri-list", 0, 0 }
};

#define DEFAULT_MONITOR_WIDTH 720
#define DEFAULT_MONITOR_HEIGHT 364

namespace SSC {
  Window::Window (SharedPointer<Core> core, const WindowOptions& options)
    : core(core),
      options(options),
      bridge(core, options.userConfig),
      hotkey(this),
      dialog(this)
  {
    setenv("GTK_OVERLAY_SCROLLING", "1", 1);

    auto userConfig = options.userConfig;
    auto webContext = webkit_web_context_get_default();

    if (options.index == 0) {
      // only Window#0 should set this value
      webkit_web_context_set_sandbox_enabled(webContext, true);
    }

    this->bridge.userConfig = userConfig;
    this->bridge.configureNavigatorMounts();
    this->settings = webkit_settings_new();
    // ALWAYS on or off
    webkit_settings_set_enable_webgl(this->settings, true);
    // TODO(@jwerle); make configurable with '[permissions] allow_media'
    webkit_settings_set_enable_media(this->settings, true);
    webkit_settings_set_enable_webaudio(this->settings, true);
    webkit_settings_set_zoom_text_only(this->settings, false);
    webkit_settings_set_enable_mediasource(this->settings, true);
    // TODO(@jwerle); make configurable with '[permissions] allow_dialogs'
    webkit_settings_set_allow_modal_dialogs(this->settings, true);
    webkit_settings_set_enable_dns_prefetching(this->settings, true);
    webkit_settings_set_enable_encrypted_media(this->settings, true);
    webkit_settings_set_media_playback_allows_inline(this->settings, true);
    webkit_settings_set_enable_developer_extras(this->settings, options.debug);
    webkit_settings_set_allow_universal_access_from_file_urls(this->settings, true);

    webkit_settings_set_enable_media_stream(
      this->settings,
      userConfig["permissions_allow_user_media"] != "false"
    );

    webkit_settings_set_enable_media_capabilities(
      this->settings,
      userConfig["permissions_allow_user_media"] != "false"
    );

    webkit_settings_set_enable_webrtc(
      this->settings,
      userConfig["permissions_allow_user_media"] != "false"
    );

    webkit_settings_set_javascript_can_access_clipboard(
      this->settings,
      userConfig["permissions_allow_clipboard"] != "false"
    );

    webkit_settings_set_enable_fullscreen(
      this->settings,
      userConfig["permissions_allow_fullscreen"] != "false"
    );

    webkit_settings_set_enable_html5_local_storage(
      this->settings,
      userConfig["permissions_allow_data_access"] != "false"
    );

    webkit_settings_set_enable_html5_database(
      this->settings,
      userConfig["permissions_allow_data_access"] != "false"
    );

    auto cookieManager = webkit_web_context_get_cookie_manager(webContext);
    webkit_cookie_manager_set_accept_policy(cookieManager, WEBKIT_COOKIE_POLICY_ACCEPT_ALWAYS);

    this->userContentManager = webkit_user_content_manager_new();
    webkit_user_content_manager_register_script_message_handler(this->userContentManager, "external");

    this->policies = webkit_website_policies_new_with_policies(
      "autoplay", userConfig["permission_allow_autoplay"] != "false"
        ? WEBKIT_AUTOPLAY_ALLOW
        : WEBKIT_AUTOPLAY_DENY,
      nullptr
    );

    this->webview = WEBKIT_WEB_VIEW(g_object_new(WEBKIT_TYPE_WEB_VIEW,
      "user-content-manager", this->userContentManager,
      "website-policies", this->policies,
      "web-context", webContext,
      "settings", this->settings,
      nullptr
    ));

    gtk_widget_set_can_focus(GTK_WIDGET(this->webview), true);

    this->index = this->options.index;
    this->dragStart = {0,0};
    this->shouldDrag = false;
    this->contextMenu = nullptr;
    this->contextMenuID = 0;

    this->accelGroup = gtk_accel_group_new();
    this->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    this->vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

    this->bridge.navigateFunction = [this] (const auto url) {
      this->navigate(url);
    };

    this->bridge.evaluateJavaScriptFunction = [this] (const auto source) {
      this->eval(source);
    };

    this->bridge.preload = IPC::createPreload({
      .clientId = this->bridge.id,
      .userScript = options.userScript
    });

    gtk_box_pack_end(GTK_BOX(this->vbox), GTK_WIDGET(this->webview), true, true, 0);

    gtk_container_add(GTK_CONTAINER(this->window), this->vbox);

    gtk_widget_add_events(this->window, GDK_ALL_EVENTS_MASK);
    gtk_widget_grab_focus(GTK_WIDGET(this->webview));
    gtk_widget_realize(GTK_WIDGET(this->window));

    if (options.resizable) {
      gtk_window_set_default_size(GTK_WINDOW(window), options.width, options.height);
    } else {
      gtk_widget_set_size_request(this->window, options.width, options.height);
    }

    gtk_window_set_resizable(GTK_WINDOW(this->window), options.resizable);
    gtk_window_set_position(GTK_WINDOW(this->window), GTK_WIN_POS_CENTER);
    gtk_widget_set_can_focus(GTK_WIDGET(this->window), true);

    GdkRGBA webviewBackground = {0.0, 0.0, 0.0, 0.0};
    bool hasDarkValue = this->options.backgroundColorDark.size();
    bool hasLightValue = this->options.backgroundColorLight.size();
    bool isDarkMode = false;

    auto isKDEDarkMode = []() -> bool {
      std::string home = std::getenv("HOME") ? std::getenv("HOME") : "";
      std::string filepath = home + "/.config/kdeglobals";
      std::ifstream file(filepath);

      if (!file.is_open()) {
        std::cerr << "Failed to open file: " << filepath << std::endl;
        return false;
      }

      std::string line;

      while (getline(file, line)) {
        std::string lower_line;

        std::transform(
          line.begin(),
          line.end(),
          std::back_inserter(lower_line),
          [](unsigned char c) { return std::tolower(c); }
        );

        if (lower_line.find("dark") != std::string::npos) {
          std::cout << "Found dark setting in line: " << line << std::endl;
          return true;
        }
      }

      return false;
    };

    auto isGnomeDarkMode = [&]() -> bool {
     GtkStyleContext* context = gtk_widget_get_style_context(window);

      GdkRGBA background_color;
      gtk_style_context_get_background_color(context, GTK_STATE_FLAG_NORMAL, &background_color);

      bool is_dark_theme = (0.299*  background_color.red +
                            0.587*  background_color.green +
                            0.114*  background_color.blue) < 0.5;

      return FALSE;
    };

    if (hasDarkValue || hasLightValue) {
      GdkRGBA color = {0};

      const gchar* desktop_env = getenv("XDG_CURRENT_DESKTOP");

      if (desktop_env != NULL && g_str_has_prefix(desktop_env, "GNOME")) {
        isDarkMode = isGnomeDarkMode();
      } else {
        isDarkMode = isKDEDarkMode();
      }

      if (isDarkMode && hasDarkValue) {
        gdk_rgba_parse(&color, this->options.backgroundColorDark.c_str());
      } else if (hasLightValue) {
        gdk_rgba_parse(&color, this->options.backgroundColorLight.c_str());
      }

      gtk_widget_override_background_color(window, GTK_STATE_FLAG_NORMAL, &color);
    }

    webkit_web_view_set_background_color(WEBKIT_WEB_VIEW(webview), &webviewBackground);

    this->hotkey.init();
    this->bridge.init();
    this->bridge.configureSchemeHandlers({
      .webview = settings
    });

    this->bridge.configureWebView(this->webview);

    g_signal_connect(
      this->userContentManager,
      "script-message-received::external",
      G_CALLBACK(+[](
        WebKitUserContentManager* userContentManager,
        WebKitJavascriptResult* result,
        gpointer ptr
      ) {
        auto window = static_cast<Window*>(ptr);
        auto value = webkit_javascript_result_get_js_value(result);
        auto valueString = jsc_value_to_string(value);
        auto str = String(valueString);

        if (!window->bridge.route(str, nullptr, 0)) {
          if (window->onMessage != nullptr) {
            window->onMessage(str);
          }
        }

        g_free(valueString);
      }),
      this
    );

    g_signal_connect(
      G_OBJECT(this->webview),
      "show-notification",
      G_CALLBACK(+[](
        WebKitWebView* webview,
        WebKitNotification* notification,
        gpointer userData
      ) -> bool {
        const auto window = reinterpret_cast<Window*>(userData);

        if (window == nullptr) {
          return false;
        }

        auto userConfig = window->bridge.userConfig;
        return userConfig["permissions_allow_notifications"] != "false";
      }),
      this
    );

    // handle `navigator.permissions.query()`
    g_signal_connect(
      G_OBJECT(this->webview),
      "query-permission-state",
      G_CALLBACK((+[](
        WebKitWebView* webview,
        WebKitPermissionStateQuery* query,
        gpointer user_data
      ) -> bool {
        static auto userConfig = SSC::getUserConfig();
        auto name = String(webkit_permission_state_query_get_name(query));

        if (name == "geolocation") {
          webkit_permission_state_query_finish(
            query,
            userConfig["permissions_allow_geolocation"] == "false"
              ? WEBKIT_PERMISSION_STATE_DENIED
              : WEBKIT_PERMISSION_STATE_PROMPT
          );
        }

        if (name == "notifications") {
          webkit_permission_state_query_finish(
            query,
            userConfig["permissions_allow_notifications"] == "false"
              ? WEBKIT_PERMISSION_STATE_DENIED
              : WEBKIT_PERMISSION_STATE_PROMPT
          );
        }

        webkit_permission_state_query_finish(
          query,
          WEBKIT_PERMISSION_STATE_PROMPT
        );
        return true;
      })),
      this
    );

    g_signal_connect(
      G_OBJECT(this->webview),
      "permission-request",
      G_CALLBACK((+[](
        WebKitWebView* webview,
        WebKitPermissionRequest* request,
        gpointer userData
      ) -> bool {
        Window* window = reinterpret_cast<Window*>(userData);
        static auto userConfig = SSC::getUserConfig();
        auto result = false;
        String name = "";
        String description = "{{meta_title}} would like permission to use a an unknown feature.";

        if (WEBKIT_IS_GEOLOCATION_PERMISSION_REQUEST(request)) {
          name = "geolocation";
          result = userConfig["permissions_allow_geolocation"] != "false";
          description = "{{meta_title}} would like access to your location.";
        } else if (WEBKIT_IS_NOTIFICATION_PERMISSION_REQUEST(request)) {
          name = "notifications";
          result = userConfig["permissions_allow_notifications"] != "false";
          description = "{{meta_title}} would like display notifications.";
        } else if (WEBKIT_IS_USER_MEDIA_PERMISSION_REQUEST(request)) {
          if (webkit_user_media_permission_is_for_audio_device(WEBKIT_USER_MEDIA_PERMISSION_REQUEST(request))) {
            name = "microphone";
            result = userConfig["permissions_allow_microphone"] == "false";
            description = "{{meta_title}} would like access to your microphone.";
          }

          if (webkit_user_media_permission_is_for_video_device(WEBKIT_USER_MEDIA_PERMISSION_REQUEST(request))) {
            name = "camera";
            result = userConfig["permissions_allow_camera"] == "false";
            description = "{{meta_title}} would like access to your camera.";
          }

          result = userConfig["permissions_allow_user_media"] == "false";
        } else if (WEBKIT_IS_WEBSITE_DATA_ACCESS_PERMISSION_REQUEST(request)) {
          name = "storage-access";
          result = userConfig["permissions_allow_data_access"] != "false";
          description = "{{meta_title}} would like access to local storage.";
        } else if (WEBKIT_IS_DEVICE_INFO_PERMISSION_REQUEST(request)) {
          result = userConfig["permissions_allow_device_info"] != "false";
          description = "{{meta_title}} would like access to your device information.";
        } else if (WEBKIT_IS_MEDIA_KEY_SYSTEM_PERMISSION_REQUEST(request)) {
          result = userConfig["permissions_allow_media_key_system"] != "false";
          description = "{{meta_title}} would like access to your media key system.";
        }

        if (result) {
          auto title = userConfig["meta_title"];
          GtkWidget* dialog = gtk_message_dialog_new(
            GTK_WINDOW(window->window),
            GTK_DIALOG_MODAL,
            GTK_MESSAGE_QUESTION,
            GTK_BUTTONS_YES_NO,
            "%s",
            tmpl(description, userConfig).c_str()
          );

          gtk_widget_show(dialog);
          if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_YES) {
            webkit_permission_request_allow(request);
          } else {
            webkit_permission_request_deny(request);
          }

          gtk_widget_destroy(dialog);
        } else {
          webkit_permission_request_deny(request);
        }

        if (name.size() > 0) {
          JSON::Object::Entries json = JSON::Object::Entries {
            {"name", name},
            {"state", result ? "granted" : "denied"}
          };
          // TODO(@heapwolf): properly return this data
        }

        return result;
      })),
      this
    );

    // Calling gtk_drag_source_set interferes with text selection
    /* gtk_drag_source_set(
      webview,
      (GdkModifierType)(GDK_BUTTON1_MASK | GDK_BUTTON2_MASK),
      droppableTypes,
      G_N_ELEMENTS(droppableTypes),
      GDK_ACTION_COPY
    );

    gtk_drag_dest_set(
      webview,
      GTK_DEST_DEFAULT_ALL,
      droppableTypes,
      1,
      GDK_ACTION_MOVE
    );

    g_signal_connect(
      G_OBJECT(this->webview),
      "button-release-event",
      G_CALLBACK(+[](GtkWidget* wv, GdkEventButton* event, gpointer arg) {
        auto* w = static_cast<Window*>(arg);
        w->shouldDrag = false;
        w->dragStart.x = 0;
        w->dragStart.y = 0;
        w->dragging.x = 0;
        w->dragging.y = 0;
        return FALSE;
      }),
      this
    );

    g_signal_connect(
      G_OBJECT(this->webview),
      "button-press-event",
      G_CALLBACK(+[](GtkWidget* wv, GdkEventButton* event, gpointer arg) {
        auto* w = static_cast<Window*>(arg);
        w->shouldDrag = false;

        if (event->button == GDK_BUTTON_PRIMARY) {
          auto win = GDK_WINDOW(gtk_widget_get_window(w->window));
          gint initialX;
          gint initialY;

          gdk_window_get_position(win, &initialX, &initialY);

          w->dragStart.x = initialX;
          w->dragStart.y = initialY;

          w->dragging.x = event->x_root;
          w->dragging.y = event->y_root;

          GdkDevice* device;

          gint x = event->x;
          gint y = event->y;
          String sx = std::to_string(x);
          String sy = std::to_string(y);

          String js(
            "(() => {                                                                      "
            "  const v = '--app-region';                                                   "
            "  let el = document.elementFromPoint(" + sx + "," + sy + ");                  "
            "                                                                              "
            "  while (el) {                                                                "
            "    if (getComputedStyle(el).getPropertyValue(v) == 'drag') return 'movable'; "
            "    el = el.parentElement;                                                    "
            "  }                                                                           "
            "  return ''                                                                   "
            "})()                                                                          "
          );

          webkit_web_view_evaluate_javascript(
            WEBKIT_WEB_VIEW(wv),
            js.c_str(),
            -1,
            nullptr,
            nullptr,
            nullptr,
            [](GObject* src, GAsyncResult* result, gpointer arg) {
              auto* w = static_cast<Window*>(arg);
              if (!w) return;

              GError* error = NULL;
              auto value = webkit_web_view_evaluate_javascript_finish(
                WEBKIT_WEB_VIEW(w->webview),
                result,
                &error
              );

              if (error) return;
              if (!value) return;
              if (!jsc_value_is_string(value)) return;

              auto match = std::string(jsc_value_to_string(value));
              w->shouldDrag = match == "movable";
              return;
            },
            w
          );
        }

        return FALSE;
      }),
      this
    );

    g_signal_connect(
      G_OBJECT(this->webview),
      "focus",
      G_CALLBACK(+[](
        GtkWidget* wv,
        GtkDirectionType direction,
        gpointer arg)
      {
        auto* w = static_cast<Window*>(arg);
        if (!w) return;
      }),
      this
    );

    g_signal_connect(
      G_OBJECT(this->webview),
      "drag-data-get",
      G_CALLBACK(+[](
        GtkWidget* wv,
        GdkDragContext* context,
        GtkSelectionData* data,
        guint info,
        guint time,
        gpointer arg)
      {
        auto* w = static_cast<Window*>(arg);
        if (!w) return;

        if (w->isDragInvokedInsideWindow) {
          // FIXME: Once, write a single tmp file `/tmp/{i64}.download` and
          // add it to the draggablePayload, then start the fanotify watcher
          // for that particular file.
          return;
        }

        if (w->draggablePayload.size() == 0) return;

        gchar* uris[w->draggablePayload.size() + 1];
        int i = 0;

        for (auto& file : w->draggablePayload) {
          if (file[0] == '/') {
            // file system paths must be proper URIs
            file = String("file://" + file);
          }
          uris[i++] = strdup(file.c_str());
        }

        uris[i] = NULL;

        gtk_selection_data_set_uris(data, uris);
      }),
      this
    );

    g_signal_connect(
      G_OBJECT(this->webview),
      "motion-notify-event",
      G_CALLBACK(+[](
        GtkWidget* wv,
        GdkEventMotion* event,
        gpointer arg)
      {
        auto* w = static_cast<Window*>(arg);
        if (!w) return FALSE;

        if (w->shouldDrag && event->state & GDK_BUTTON1_MASK) {
          auto win = GDK_WINDOW(gtk_widget_get_window(w->window));
          gint x;
          gint y;

          GdkRectangle frame_extents;
          gdk_window_get_frame_extents(win, &frame_extents);

          GtkAllocation allocation;
          gtk_widget_get_allocation(wv, &allocation);

          gint menubarHeight = 0;

          if (w->menubar) {
            GtkAllocation allocationMenubar;
            gtk_widget_get_allocation(w->menubar, &allocationMenubar);
            menubarHeight = allocationMenubar.height;
          }

          int offsetWidth = (frame_extents.width - allocation.width) / 2;
          int offsetHeight = (frame_extents.height - allocation.height) - offsetWidth - menubarHeight;

          gdk_window_get_position(win, &x, &y);

          gint offset_x = event->x_root - w->dragging.x;
          gint offset_y = event->y_root - w->dragging.y;

          gint newX = x + offset_x;
          gint newY = y + offset_y;

          gdk_window_move(win, newX - offsetWidth, newY - offsetHeight);

          w->dragging.x = event->x_root;
          w->dragging.y = event->y_root;
        }

        return FALSE;

        //
        // TODO(@heapwolf): refactor legacy drag and drop stuff
        //
        // char* target_uri = g_file_get_uri(drag_info->target_location);

        int count = w->draggablePayload.size();
        bool inbound = !w->isDragInvokedInsideWindow;

        // w->eval(getEmitToRenderProcessJavaScript("dragend", "{}"));

        // TODO wtf we get a toaster instead of actual focus
        gtk_window_present(GTK_WINDOW(w->window));
        // gdk_window_focus(GDK_WINDOW(w->window), nullptr);

        String json = (
          "{\"count\":" + std::to_string(count) + ","
          "\"inbound\":" + (inbound ? "true" : "false") + ","
          "\"x\":" + std::to_string(x) + ","
          "\"y\":" + std::to_string(y) + "}"
        );

        w->eval(getEmitToRenderProcessJavaScript("drag", json));
      }),
      this
    );

    // https://wiki.gnome.org/Newcomers/XdsTutorial
    // https://wiki.gnome.org/action/show/Newcomers/OldDragNDropTutorial?action=show&redirect=Newcomers%2FDragNDropTutorial

    g_signal_connect(
      G_OBJECT(this->webview),
      "drag-end",
      G_CALLBACK(+[](GtkWidget* wv, GdkDragContext* context, gpointer arg) {
        auto* w = static_cast<Window*>(arg);
        if (!w) return;

        // w->isDragInvokedInsideWindow = false;
        // w->draggablePayload.clear();
        w->eval(getEmitToRenderProcessJavaScript("dragend", "{}"));
      }),
      this
    );

    g_signal_connect(
      G_OBJECT(this->webview),
      "drag-data-received",
      G_CALLBACK(+[](
        GtkWidget* wv,
        GdkDragContext* context,
        gint x,
        gint y,
        GtkSelectionData* data,
        guint info,
        guint time,
        gpointer arg)
      {
        auto* w = static_cast<Window*>(arg);
        if (!w) return;

        gtk_drag_dest_add_uri_targets(wv);
        gchar** uris = gtk_selection_data_get_uris(data);
        int len = gtk_selection_data_get_length(data) - 1;
        if (!uris) return;

        auto v = &w->draggablePayload;

        for(size_t n = 0; uris[n] != nullptr; n++) {
          gchar* src = g_filename_from_uri(uris[n], nullptr, nullptr);
          if (src) {
            auto s = String(src);
            if (std::find(v->begin(), v->end(), s) == v->end()) {
              v->push_back(s);
            }
            g_free(src);
          }
        }
      }),
      this
    );

    g_signal_connect(
      G_OBJECT(this->webview),
      "drag-drop",
      G_CALLBACK(+[](
        GtkWidget* widget,
        GdkDragContext* context,
        gint x,
        gint y,
        guint time,
        gpointer arg)
      {
        auto* w = static_cast<Window*>(arg);
        auto count = w->draggablePayload.size();
        JSON::Array files;

        for (int i = 0 ; i < count; ++i) {
          files[i] = w->draggablePayload[i];
        }

        JSON::Object json;
        json["files"] = files;
        json["x"] = x;
        json["y"] = y;

        JSON::Object options;
        options["bubbles"] = true;

        w->eval(getEmitToRenderProcessJavaScript(
          "dropin",
          json.str(),
          "globalThis",
          options
        ));

        w->draggablePayload.clear();
        w->eval(getEmitToRenderProcessJavaScript("dragend", "{}"));
        gtk_drag_finish(context, TRUE, TRUE, time);
        return TRUE;
      }),
      this
    );
    */

    /*
    * FIXME(@jwerle): this can race - ideally, this is fixed in an abstraction
    auto onDestroy = +[](GtkWidget*, gpointer arg) {
      auto* w = static_cast<Window*>(arg);
      auto* app = App::sharedApplication();
      app->windowManager.destroyWindow(w->index);

      for (auto window : app->windowManager.windows) {
        if (window == nullptr || window.get() == w) {
          continue;
        }

        JSON::Object json = JSON::Object::Entries {
          {"data", w->index}
        };

        window->eval(getEmitToRenderProcessJavaScript("window-closed", json.str()));
      }
      return FALSE;
    };

    g_signal_connect(
      G_OBJECT(this->window),
      "destroy",
      G_CALLBACK(onDestroy),
      this
    );
    */

    g_signal_connect(
      G_OBJECT(this->window),
      "delete-event",
      G_CALLBACK(+[](GtkWidget* widget, GdkEvent*, gpointer arg) {
        auto* w = static_cast<Window*>(arg);

        if (w->options.shouldExitApplicationOnClose == false) {
          w->eval(getEmitToRenderProcessJavaScript("windowHide", "{}"));
          return gtk_widget_hide_on_delete(widget);
        }

        w->close(0);
        return FALSE;
      }),
      this
    );

    g_signal_connect(
      G_OBJECT(this->window),
      "size-allocate", // https://docs.gtk.org/gtk3/method.Window.get_size.html
      G_CALLBACK(+[](GtkWidget* widget,GtkAllocation* allocation, gpointer arg) {
        auto* w = static_cast<Window*>(arg);
        gtk_window_get_size(GTK_WINDOW(widget), &w->size.width, &w->size.height);
      }),
      this
    );

    if (this->options.aspectRatio.size() > 0) {
      g_signal_connect(
        window,
        "configure-event",
        G_CALLBACK(+[](GtkWidget* widget, GdkEventConfigure* event, gpointer ptr) {
          auto w = static_cast<Window*>(ptr);
          if (!w) return FALSE;

          // TODO(@heapwolf): make the parsed aspectRatio properties so it doesnt need to be recalculated.
          auto parts = split(w->options.aspectRatio, ':');
          float aspectWidth = 0;
          float aspectHeight = 0;

          try {
            aspectWidth = std::stof(trim(parts[0]));
            aspectHeight = std::stof(trim(parts[1]));
          } catch (...) {
            debug("invalid aspect ratio");
            return FALSE;
          }

          if (aspectWidth > 0 && aspectHeight > 0) {
            GdkGeometry geom;
            geom.min_aspect = aspectWidth / aspectHeight;
            geom.max_aspect = geom.min_aspect;
            gtk_window_set_geometry_hints(GTK_WINDOW(widget), widget, &geom, GdkWindowHints(GDK_HINT_ASPECT));
          }

          return FALSE;
        }),
        this
      );

      // gtk_window_set_aspect_ratio(GTK_WINDOW(window), aspectRatio, TRUE);
    }
  }

  Window::~Window () {
    if (this->policies) {
      g_object_unref(this->policies);
      this->policies = nullptr;
    }

    if (this->settings) {
      g_object_unref(this->settings);
      this->settings = nullptr;
    }

    if (this->userContentManager) {
      g_object_unref(this->userContentManager);
      this->userContentManager = nullptr;
    }

    if (this->webview) {
      g_object_unref(this->webview);
      this->webview = nullptr;
    }

    if (this->window) {
      auto w = this->window;
      this->window = nullptr;
      gtk_widget_destroy(w);
    }

    if (this->accelGroup) {
      g_object_unref(this->accelGroup);
      this->accelGroup = nullptr;
    }

    if (this->vbox) {
      //g_object_unref(this->vbox);
      this->vbox = nullptr;
    }
  }

  ScreenSize Window::getScreenSize () {
    auto list = gtk_window_list_toplevels();
    int width = 0;
    int height = 0;

    if (list != nullptr) {
      for (auto entry = list; entry != nullptr; entry = entry->next) {
        auto widget = (GtkWidget*) entry->data;
        auto window = GTK_WINDOW(widget);

        if (window != nullptr) {
          auto geometry = GdkRectangle {};
          auto display = gtk_widget_get_display(widget);
          auto monitor = gdk_display_get_monitor_at_window(
            display,
            gtk_widget_get_window(widget)
          );

          gdk_monitor_get_geometry(monitor, &geometry);

          if (geometry.width > 0) {
            width = geometry.width;
          }

          if (geometry.height > 0) {
            height = geometry.height;
          }

          break;
        }
      }

      g_list_free(list);
    }

    if (!height || !width) {
      auto geometry = GdkRectangle {};
      auto display = gdk_display_get_default();
      auto monitor = gdk_display_get_primary_monitor(display);

      if (monitor) {
        gdk_monitor_get_workarea(monitor, &geometry);
      }

      if (geometry.width > 0) {
        width = geometry.width;
      }

      if (geometry.height > 0) {
        height = geometry.height;
      }
    }

    if (!height) {
      height = (int) DEFAULT_MONITOR_HEIGHT;
    }

    if (!width) {
      width = (int) DEFAULT_MONITOR_WIDTH;
    }

    return ScreenSize { height, width };
  }

  void Window::eval (const String& source) {
    if (this->webview) {
      App::sharedApplication()->dispatch([=, this] {
        webkit_web_view_evaluate_javascript(
          this->webview,
          String(source).c_str(),
          -1,
          nullptr,
          nullptr,
          nullptr,
          nullptr,
          nullptr
        );
      });
    }
  }

  void Window::show () {
    gtk_widget_realize(this->window);

    this->index = this->options.index;
    if (this->options.headless == false) {
      gtk_widget_show_all(this->window);
      gtk_window_present(GTK_WINDOW(this->window));
    }
  }

  void Window::hide () {
    gtk_widget_realize(this->window);
    gtk_widget_hide(this->window);
    this->eval(getEmitToRenderProcessJavaScript("windowHide", "{}"));
  }

  void Window::setBackgroundColor (const String& rgba) {

  }

  void Window::setBackgroundColor (int r, int g, int b, float a) {
    GdkRGBA color;
    color.red = r / 255.0;
    color.green = g / 255.0;
    color.blue = b / 255.0;
    color.alpha = a;

    gtk_widget_realize(this->window);
    // FIXME(@jwerle): this is deprecated
    gtk_widget_override_background_color(
      this->window, GTK_STATE_FLAG_NORMAL, &color
    );
  }

  String Window::getBackgroundColor () {
    GtkStyleContext* context = gtk_widget_get_style_context(this->window);

    GdkRGBA color;
    gtk_style_context_get_background_color(context, gtk_widget_get_state_flags(this->window), &color);

    char str[100];

    snprintf(
      str,
      sizeof(str),
      "rgba(%d, %d, %d, %f)",
      (int)(color.red*  255),
      (int)(color.green*  255),
      (int)(color.blue*  255),
      color.alpha
    );

    return String(str);
  }

  void Window::showInspector () {
    auto inspector = webkit_web_view_get_inspector(this->webview);
    if (inspector) {
      webkit_web_inspector_show(inspector);
    }
  }

  void Window::exit (int code) {
    auto cb = this->onExit;
    this->onExit = nullptr;
    if (cb != nullptr) cb(code);
  }

  void Window::kill () {}

  void Window::close (int _) {
    if (this->window) {
      gtk_window_close(GTK_WINDOW(this->window));
    }
  }

  void Window::maximize () {
    gtk_window_maximize(GTK_WINDOW(window));
  }

  void Window::minimize () {
    gtk_window_iconify(GTK_WINDOW(window));
  }

  void Window::restore () {
    gtk_window_deiconify(GTK_WINDOW(window));
  }

  void Window::navigate (const String& url) {
    if (this->webview) {
      webkit_web_view_load_uri(this->webview, url.c_str());
    }
  }

  const String Window::getTitle () const {
    if (this->window != nullptr) {
      const auto title = gtk_window_get_title(GTK_WINDOW(this->window));
      if (title != nullptr) {
        return title;
      }
    }

    return "";
  }

  void Window::setTitle (const String& s) {
    gtk_widget_realize(window);
    gtk_window_set_title(GTK_WINDOW(window), s.c_str());
  }

  void Window::about () {
    GtkWidget* dialog = gtk_dialog_new();
    gtk_window_set_default_size(GTK_WINDOW(dialog), 300, 200);

    GtkWidget* body = gtk_dialog_get_content_area(GTK_DIALOG(GTK_WINDOW(dialog)));
    GtkContainer* content = GTK_CONTAINER(body);

    String imgPath = "/usr/share/icons/hicolor/256x256/apps/" +
      this->bridge.userConfig["build_name"] +
      ".png";

    GdkPixbuf* pixbuf = gdk_pixbuf_new_from_file_at_scale(
      imgPath.c_str(),
      60,
      60,
      true,
      nullptr
    );

    GtkWidget* img = gtk_image_new_from_pixbuf(pixbuf);
    gtk_widget_set_margin_top(img, 20);
    gtk_widget_set_margin_bottom(img, 20);

    gtk_box_pack_start(GTK_BOX(content), img, false, false, 0);

    String title_value(this->bridge.userConfig["build_name"] + " v" + this->bridge.userConfig["meta_version"]);
    String version_value("Built with ssc v" + SSC::VERSION_FULL_STRING);

    GtkWidget* label_title = gtk_label_new("");
    gtk_label_set_markup(GTK_LABEL(label_title), title_value.c_str());
    gtk_container_add(content, label_title);

    GtkWidget* label_op_version = gtk_label_new("");
    gtk_label_set_markup(GTK_LABEL(label_op_version), version_value.c_str());
    gtk_container_add(content, label_op_version);

    GtkWidget* label_copyright = gtk_label_new("");
    gtk_label_set_markup(GTK_LABEL(label_copyright), this->bridge.userConfig["meta_copyright"].c_str());
    gtk_container_add(content, label_copyright);

    g_signal_connect(
      dialog,
      "response",
      G_CALLBACK(gtk_widget_destroy),
      nullptr
    );

    gtk_widget_show_all(body);
    gtk_widget_show_all(dialog);
    gtk_window_set_title(GTK_WINDOW(dialog), "About");

    gtk_dialog_run(GTK_DIALOG(dialog));
  }

  Window::Size Window::getSize () {
    gtk_widget_get_size_request(
      this->window,
      &this->size.width,
      &this->size.height
    );

    return this->size;
  }

  const Window::Size Window::getSize () const {
    return this->size;
  }

  void Window::setSize (int width, int height, int hints) {
    gtk_widget_realize(window);
    gtk_window_set_resizable(GTK_WINDOW(window), hints != WINDOW_HINT_FIXED);

    if (hints == WINDOW_HINT_NONE) {
      gtk_window_resize(GTK_WINDOW(window), width, height);
    } else if (hints == WINDOW_HINT_FIXED) {
      gtk_widget_set_size_request(window, width, height);
    } else {
      GdkGeometry g;
      g.min_width = g.max_width = width;
      g.min_height = g.max_height = height;

      GdkWindowHints h = (hints == WINDOW_HINT_MIN
        ? GDK_HINT_MIN_SIZE
        : GDK_HINT_MAX_SIZE
      );

      gtk_window_set_geometry_hints(GTK_WINDOW(window), nullptr, &g, h);
    }

    this->size.width = width;
    this->size.height = height;
  }

  void Window::setPosition (float x, float y) {
    gtk_window_move(GTK_WINDOW(this->window), (int) x, (int) y);
    this->position.x = x;
    this->position.y = y;
  }

  void Window::setTrayMenu (const String& value) {
    this->setMenu(value, true);
  }

  void Window::setSystemMenu (const String& value) {
    this->setMenu(value, false);
  }

  void Window::setMenu (const String& menuSource, const bool& isTrayMenu) {
    if (menuSource.empty()) {
      return;
    }

    auto clear = [this](GtkWidget* menu) {
      GList* iter;
      GList* children = gtk_container_get_children(GTK_CONTAINER(menu));

      for (iter = children; iter != nullptr; iter = g_list_next(iter)) {
        gtk_widget_destroy(GTK_WIDGET(iter->data));
      }

      g_list_free(children);
      return menu;
    };

    if (isTrayMenu) {
      menutray = menutray == nullptr ? gtk_menu_new() : clear(menutray);
    } else {
      menubar = menubar == nullptr ? gtk_menu_bar_new() : clear(menubar);
    }

    GtkStyleContext* context = gtk_widget_get_style_context(this->window);

    GdkRGBA color;
    gtk_style_context_get_background_color(context, gtk_widget_get_state_flags(this->window), &color);
    gtk_widget_override_background_color(menubar, GTK_STATE_FLAG_NORMAL, &color);

    auto menus = split(menuSource, ';');

    for (auto m : menus) {
      auto menuSource = split(m, '\n');
      if (menuSource.size() == 0) continue;
      auto line = trim(menuSource[0]);
      if (line.empty()) continue;
      auto menuParts = split(line, ':');
      auto menuTitle = menuParts[0];
      // if this is a tray menu, append directly to the tray instead of a submenu.
      auto* ctx = isTrayMenu ? menutray : gtk_menu_new();
      GtkWidget* menuItem = gtk_menu_item_new_with_label(menuTitle.c_str());

      if (isTrayMenu && menuSource.size() == 1) {
        if (menuParts.size() > 1) {
          gtk_widget_set_name(menuItem, trim(menuParts[1]).c_str());
        }

        g_signal_connect(
          G_OBJECT(menuItem),
          "activate",
          G_CALLBACK(+[](GtkWidget* t, gpointer arg) {
            auto w = static_cast<Window*>(arg);
            auto title = gtk_menu_item_get_label(GTK_MENU_ITEM(t));
            auto parent = gtk_widget_get_name(t);
            w->eval(getResolveMenuSelectionJavaScript("0", title, parent, "tray"));
          }),
          this
        );
      }

      for (int i = 1; i < menuSource.size(); i++) {
        auto line = trim(menuSource[i]);
        if (line.empty()) continue;
        auto parts = split(line, ':');
        auto title = parts[0];
        String key = "";

        GtkWidget* item;

        if (parts[0].find("---") != -1) {
          item = gtk_separator_menu_item_new();
        } else {
          item = gtk_menu_item_new_with_label(title.c_str());

          if (parts.size() > 1) {
            auto value = trim(parts[1]);
            key = value == "_" ? "" : value;

            if (key.size() > 0) {
              auto accelerator = split(parts[1], '+');
              if (accelerator.size() <= 1) {
                continue;
              }

              auto modifier = trim(accelerator[1]);
              // normalize modifier to lower case
              std::transform(
                modifier.begin(),
                modifier.end(),
                modifier.begin(),
                [](auto ch) { return std::tolower(ch); }
              );
              key = trim(parts[1]) == "_" ? "" : trim(accelerator[0]);

              GdkModifierType mask = (GdkModifierType)(0);
              bool isShift = String("ABCDEFGHIJKLMNOPQRSTUVWXYZ").find(key) != -1;

              if (accelerator.size() > 1) {
                if (modifier.find("meta") != -1 || modifier.find("super") != -1) {
                  mask = (GdkModifierType)(mask | GDK_META_MASK);
                }

                if (modifier.find("commandorcontrol") != -1) {
                  mask = (GdkModifierType)(mask | GDK_CONTROL_MASK);
                } else if (modifier.find("control") != -1) {
                  mask = (GdkModifierType)(mask | GDK_CONTROL_MASK);
                }

                if (modifier.find("alt") != -1) {
                  mask = (GdkModifierType)(mask | GDK_MOD1_MASK);
                }
              }

              if (isShift || modifier.find("shift") != -1) {
                mask = (GdkModifierType)(mask | GDK_SHIFT_MASK);
              }

              gtk_widget_add_accelerator(
                item,
                "activate",
                accelGroup,
                (guint) key[0],
                mask,
                GTK_ACCEL_VISIBLE
              );

              gtk_widget_show(item);
            }
          }

          if (isTrayMenu) {
            g_signal_connect(
              G_OBJECT(item),
              "activate",
              G_CALLBACK(+[](GtkWidget* t, gpointer arg) {
                auto w = static_cast<Window*>(arg);
                auto title = gtk_menu_item_get_label(GTK_MENU_ITEM(t));
                auto parent = gtk_widget_get_name(t);

                w->eval(getResolveMenuSelectionJavaScript("0", title, parent, "tray"));
              }),
              this
            );
          } else {
            g_signal_connect(
              G_OBJECT(item),
              "activate",
              G_CALLBACK(+[](GtkWidget* t, gpointer arg) {
                auto w = static_cast<Window*>(arg);
                auto title = gtk_menu_item_get_label(GTK_MENU_ITEM(t));
                auto parent = gtk_widget_get_name(t);

                if (String(title).find("About") == 0) {
                  return w->about();
                }

                if (String(title).find("Quit") == 0) {
                  return w->exit(0);
                }

                w->eval(getResolveMenuSelectionJavaScript("0", title, parent, "system"));
              }),
              this
            );
          }
        }

        gtk_widget_set_name(item, menuTitle.c_str());
        gtk_menu_shell_append(GTK_MENU_SHELL(ctx), item);
      }

      if (isTrayMenu) {
        gtk_menu_shell_append(GTK_MENU_SHELL(menutray), menuItem);
      } else {
        gtk_menu_item_set_submenu(GTK_MENU_ITEM(menuItem), ctx);
        gtk_menu_shell_append(GTK_MENU_SHELL(menubar), menuItem);
      }
    }

    if (isTrayMenu) {
      static auto userConfig = SSC::getUserConfig();
      static auto app = App::sharedApplication();
      GtkStatusIcon* trayIcon;
      auto cwd = app->getcwd();
      auto trayIconPath = String("application_tray_icon");

      if (fs::exists(fs::path(cwd) / (trayIconPath + ".png"))) {
        trayIconPath = (fs::path(cwd) / (trayIconPath + ".png")).string();
      } else if (fs::exists(fs::path(cwd) / (trayIconPath + ".jpg"))) {
        trayIconPath = (fs::path(cwd) / (trayIconPath + ".jpg")).string();
      } else if (fs::exists(fs::path(cwd) / (trayIconPath + ".jpeg"))) {
        trayIconPath = (fs::path(cwd) / (trayIconPath + ".jpeg")).string();
      } else if (fs::exists(fs::path(cwd) / (trayIconPath + ".ico"))) {
        trayIconPath = (fs::path(cwd) / (trayIconPath + ".ico")).string();
      } else {
        trayIconPath = "";
      }

      if (trayIconPath.size() > 0) {
        trayIcon = gtk_status_icon_new_from_file(trayIconPath.c_str());
      } else {
        trayIcon = gtk_status_icon_new_from_icon_name("utilities-terminal");
      }

      if (userConfig.count("tray_tooltip") > 0) {
        gtk_status_icon_set_tooltip_text(trayIcon, userConfig["tray_tooltip"].c_str());
      }

      g_signal_connect(
        trayIcon,
        "activate",
        G_CALLBACK(+[](GtkWidget* t, gpointer arg) {
          auto w = static_cast<Window*>(arg);
          gtk_menu_popup_at_pointer(GTK_MENU(w->menutray), NULL);
          w->bridge.emit("tray", true);
        }),
        this
      );
      gtk_widget_show_all(menutray);
    } else {
      gtk_box_pack_start(GTK_BOX(this->vbox), menubar, false, false, 0);
      gtk_widget_show_all(menubar);
    }
  }

  void Window::setSystemMenuItemEnabled (bool enabled, int barPos, int menuPos) {
    // @TODO(): provide impl
  }

  void Window::closeContextMenu () {
    if (this->contextMenuID > 0) {
      const auto seq = std::to_string(this->contextMenuID);
      this->contextMenuID = 0;
      closeContextMenu(seq);
    }
  }

  void Window::closeContextMenu (const String& seq) {
    if (contextMenu != nullptr) {
      auto ptr = contextMenu;
      contextMenu = nullptr;
      closeContextMenu(ptr, seq);
    }
  }

  void Window::closeContextMenu (
    GtkWidget* contextMenu,
    const String& seq
  ) {
    if (contextMenu != nullptr) {
      gtk_menu_popdown((GtkMenu* ) contextMenu);
      gtk_widget_destroy(contextMenu);
      this->eval(getResolveMenuSelectionJavaScript(seq, "", "contextMenu", "context"));
    }
  }

  void Window::setContextMenu (
    const String& seq,
    const String& menuSource
  ) {
    closeContextMenu();
    if (menuSource.empty()) return void(0);

    // members
    this->contextMenu = gtk_menu_new();

    try {
      this->contextMenuID = std::stoi(seq);
    } catch (...) {
      this->contextMenuID = 0;
    }

    auto menuItems = split(menuSource, '\n');

    for (auto itemData : menuItems) {
      if (trim(itemData).size() == 0) {
        continue;
      }

      if (itemData.find("---") != -1) {
        auto* item = gtk_separator_menu_item_new();
        gtk_widget_show(item);
        gtk_menu_shell_append(GTK_MENU_SHELL(this->contextMenu), item);
        continue;
      }

      auto pair = split(itemData, ':');
      auto meta = String(seq + ";" + itemData);
      auto* item = gtk_menu_item_new_with_label(pair[0].c_str());

      g_signal_connect(
        G_OBJECT(item),
        "activate",
        G_CALLBACK(+[](GtkWidget* t, gpointer arg) {
          auto window = static_cast<Window*>(arg);
          if (!window) return;

          auto meta = gtk_widget_get_name(t);
          auto pair = split(meta, ';');
          auto seq = pair[0];
          auto items = split(pair[1], ":");

          if (items.size() != 2) return;
          window->eval(getResolveMenuSelectionJavaScript(seq, trim(items[0]), trim(items[1]), "context"));
        }),
        this
      );

      gtk_widget_set_name(item, meta.c_str());
      gtk_widget_show(item);
      gtk_menu_shell_append(GTK_MENU_SHELL(this->contextMenu), item);
    }

    GdkRectangle rect;
    gint x, y;

    auto win = GDK_WINDOW(gtk_widget_get_window(window));
    auto seat = gdk_display_get_default_seat(gdk_display_get_default());
    auto event = gdk_event_new(GDK_BUTTON_PRESS);
    auto mouse_device = gdk_seat_get_pointer(seat);

    gdk_window_get_device_position(win, mouse_device, &x, &y, nullptr);
    gdk_event_set_device(event, mouse_device);

    event->button.send_event = 1;
    event->button.button = GDK_BUTTON_SECONDARY;
    event->button.window = GDK_WINDOW(g_object_ref(win));
    event->button.time = GDK_CURRENT_TIME;

    rect.height = 0;
    rect.width = 0;
    rect.x = x - 1;
    rect.y = y - 1;

    gtk_widget_add_events(contextMenu, GDK_ALL_EVENTS_MASK);
    gtk_widget_set_can_focus(contextMenu, true);
    gtk_widget_show_all(contextMenu);
    gtk_widget_grab_focus(contextMenu);

    gtk_menu_popup_at_rect(
      GTK_MENU(contextMenu),
      win,
      &rect,
      GDK_GRAVITY_SOUTH_WEST,
      GDK_GRAVITY_NORTH_WEST,
      event
    );
  }
}
