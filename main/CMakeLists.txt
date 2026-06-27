## No explicit REQUIRES: the `main` component implicitly depends on all components
## in the build (the convention the BSP examples use), so the BSP's transitive
## headers (esp_io_expander, lvgl, esp_lvgl_port, ...) plus esp_wifi/esp_http_client
## are all available. The `sensecap-watcher` BSP itself is pulled via idf_component.yml.
idf_component_register(
    SRCS "main.c" "wifi_connect.c" "http_get.c" "claude_usage.c" "claude_status.c" "ui.c"
         "font_ext_16.c" "font_ext_22.c" "icons_lucide.c" "screenshot.c" "splash.c"
         "secure_store.c" "config.c" "provisioning.c" "speak.c" "speak_clips.c"
    INCLUDE_DIRS "."
)
