# hello_world (M9 first app)

ESP-IDF `examples/get-started/hello_world` 移植到项目内的版本。
源来自 `/opt/esp-idf/examples/get-started/hello_world/`（AUR 包内）；
本目录是从该 example `cp -r` 后重命名源文件（去掉 `hello_world_` 前缀）
得到的本地副本，ESP-IDF 保持只读。

## 构建 + 烧录

```bash
source /opt/esp-idf/export.sh
cd bluetooth/esp32-wroom-32e
python3 tools/build.py    # idf.py set-target esp32 && idf.py build
python3 tools/burn.py     # idf.py flash -p $BOARD_A_PORT
```

预期串口输出：`Hello world!` + ESP32 启动日志。