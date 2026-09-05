set(srcs "main.c" "esp_hid_gap.c" "hid_report.c")
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
