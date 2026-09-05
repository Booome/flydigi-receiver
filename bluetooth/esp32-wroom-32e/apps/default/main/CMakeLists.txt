set(srcs "main.c" "bt_stack.c" "hid_report.c")
set(include_dirs ".")

idf_component_register(
  SRCS
  "${srcs}"
  INCLUDE_DIRS
  "${include_dirs}"
  REQUIRES
  esp_hid
  PRIV_REQUIRES
  nvs_flash
)
