# ESP32-WROOM-32E 环境搭建实施计划（M9 / 蓝牙方向第一里程碑）

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 调通 ESP32-WROOM-32E 开发环境：hello_world app 能编译、能烧录、串口能看到 `Hello world!`。**不**涉及手柄或蓝牙协议。

**Architecture:** ESP-IDF v6.0.2（AUR @ `/opt/esp-idf`）作为只读 SDK；项目 `bluetooth/esp32-wroom-32e/` 作为可写工作区，每个 app 是独立 ESP-IDF 项目（`apps/<app>/`），编译产物落到 `build/<app>/`（与 `apps/` 平列）。`tools/capture_uart.py` 上移到顶层 `tools/`（CLI 不变）；ESP32 专用 `build.py` / `burn.py` 跟随项目。

**Tech Stack:** ESP-IDF v6.0.2, AUR esp-idf 包, xtensa-esp-elf 工具链 (`~/.espressif/tools/xtensa-esp-elf/...`), Python 3 (pyserial), CMake (ESP-IDF 内置), `idf.py`.

**Spec:** `docs/superpowers/specs/2026-09-05-esp32-env-setup-design.md`

## Global Constraints

- **Non-invasive build**: ESP-IDF `/opt/esp-idf` is read-only; never edit `/opt/esp-idf/examples/...`. Copy example into our project, then modify the copy.
- **ESP-IDF location**: `/opt/esp-idf` (AUR); activate via `source /opt/esp-idf/export.sh` (idempotent). Do NOT clone to `~/workspace/esp-idf`.
- **Target chip**: ESP32 (Xtensa LX6, original); `CONFIG_IDF_TARGET="esp32"` pinned in `sdkconfig.defaults` per app.
- **Build output**: `bluetooth/esp32-wroom-32e/build/<app>/` (top-level under board), via `idf.py -B ../../build/<app>`.
- **Multi app**: `tools/build.py` / `tools/burn.py` accept `--app <name>`; default app = `hello_world`.
- **`.env` (top-level, not committed)**: no new keys; ESP32 reuses `BOARD_A_PORT` / `BOARD_B_PORT` / `CTRL_PIN`.
- **`tools/capture_uart.py`**: moved from `wireless/tools/` to top-level `tools/`. CLI unchanged (`--board-a/--board-b/--rst-a/--rst-b`).
- **`wireless/tools/burn.py`**: SLE-only (ws63flash); **not** touched.
- **Wire (shared)**: ESP32 DevKitC 接入位置与 board_a / board_b 物理位置相同；ctrl 板同一组 pin 通用。
- **Code style**: C uses `.clang-format` (LLVM, 4-space, 100 col); `cmake-format -c .cmake-format.yaml -i` after editing CMakeLists. No `(void)arg`. No Chinese in code/comments.
- **No placeholders, no TBD**: every step contains the exact content (commands, file paths, expected outputs).

---

## File Structure

**Create** (under `bluetooth/esp32-wroom-32e/`):
- `README.md` — platform overview
- `docs/development.md` — env + workflow notes
- `apps/hello_world/CMakeLists.txt` — ESP-IDF project root CMake (kept from example, minor adaptation)
- `apps/hello_world/main/CMakeLists.txt` — main component CMake (SRCS updated to `main.c`)
- `apps/hello_world/main/main.c` — hello world source (renamed from `examples/get-started/hello_world/main/hello_world_main.c`)
- `apps/hello_world/sdkconfig.defaults` — `CONFIG_IDF_TARGET="esp32"`
- `apps/hello_world/README.md` — app doc
- `components/.gitkeep` — reserved directory for **app-shared components** (e.g. `bt_common/`, `tlv_format/`); auto-discovered by ESP-IDF (`project.cmake:500`). Created now to avoid future restructuring; stays empty in M9.
- `tools/build.py` — `idf.py` build wrapper
- `tools/burn.py` — `idf.py` flash wrapper

**Move**:
- `wireless/tools/capture_uart.py` → `tools/capture_uart.py` (no behavior change)

**Modify**:
- `.gitignore` — add `bluetooth/esp32-wroom-32e/build/`, `bluetooth/*/build/`
- `AGENTS.md` — project status (BT 方向), 共享工具段（capture_uart.py 路径）
- `README.md` — 项目结构图、硬件表

**Read-only references** (not modified):
- `/opt/esp-idf` (AUR, untouched)
- `/opt/esp-idf/examples/get-started/hello_world/` (source of truth for hello_world app)
- `.env` (top-level, user-managed, not in repo)

---

## Task 1: Project skeleton + .gitignore

**Files:**
- Create: `bluetooth/esp32-wroom-32e/README.md`
- Create: `bluetooth/esp32-wroom-32e/docs/development.md` (stub, will be filled in Task 6)
- Create: `bluetooth/esp32-wroom-32e/apps/.gitkeep` (empty)
- Create: `bluetooth/esp32-wroom-32e/components/.gitkeep` (empty — reserved for future app-shared code, see File Structure note)
- Create: `bluetooth/esp32-wroom-32e/tools/.gitkeep` (empty)
- Modify: `.gitignore` (add bluetooth build patterns)

**Interfaces:**
- Consumes: nothing
- Produces: empty directory tree + `.gitignore` pattern so future `build/` outputs stay out of git

- [ ] **Step 1: Verify ESP-IDF is loadable**

Run:
```bash
source /opt/esp-idf/export.sh >/dev/null 2>&1
which idf.py
idf.py --version
```
Expected: `idf.py` path under `/opt/esp-idf/tools/idf.py`; version string contains `v6.0.2`. If not, stop and ask the user.

- [ ] **Step 2: Create directory skeleton**

Run (from project root):
```bash
mkdir -p bluetooth/esp32-wroom-32e/apps
mkdir -p bluetooth/esp32-wroom-32e/components
mkdir -p bluetooth/esp32-wroom-32e/tools
mkdir -p bluetooth/esp32-wroom-32e/docs
touch bluetooth/esp32-wroom-32e/apps/.gitkeep
touch bluetooth/esp32-wroom-32e/components/.gitkeep
touch bluetooth/esp32-wroom-32e/tools/.gitkeep
ls -la bluetooth/esp32-wroom-32e/
```
Expected output shows `apps/  components/  docs/  tools/` each containing `.gitkeep`. No error.

Note: `components/` is created empty for M9 but reserved for future **app-shared components** (e.g. `bt_common/`, `tlv_format/`). ESP-IDF auto-discovers components under this directory (`project.cmake:500`).

- [ ] **Step 3: Update `.gitignore`**

Read top-level `.gitignore` (create with this content if absent):

```gitignore
# Bluetooth project build outputs
bluetooth/*/build/

# Existing SLE patterns (preserve whatever is already there)
```

Append (preserving existing content):
```
bluetooth/*/build/
```

Verify:
```bash
tail -5 .gitignore
grep -E "^bluetooth/" .gitignore
```
Expected: line `bluetooth/*/build/` present at end.

- [ ] **Step 4: Stub platform README.md**

Create `bluetooth/esp32-wroom-32e/README.md` with:

```markdown
# ESP32-WROOM-32E（蓝牙方向，M9）

飞智八爪鱼5 自制接收器项目中的 **蓝牙方向** 第一里程碑：
搭建 ESP32-WROOM-32E 开发环境，为后续 BR/EDR HID 主机研究
（连接手柄的蓝牙模式，详 `docs/controller-modes.md`）做准备。

**本里程碑只做环境**：hello_world 编译 + 烧录 + 串口输出。
**不**涉及手柄或蓝牙协议。

## 快速上手

```bash
source /opt/esp-idf/export.sh
cd bluetooth/esp32-wroom-32e
python3 tools/build.py            # 默认 hello_world app
python3 tools/burn.py             # 默认 hello_world，需连接 DevKitC
python3 ../../tools/capture_uart.py --board-a --duration 10 --odir /tmp --ts
```

详细见 `docs/development.md`。

## 项目布局

```
bluetooth/esp32-wroom-32e/
├── apps/                # 每个 app 一个独立 ESP-IDF 项目
│   └── hello_world/
├── components/          # 跨 app 共享的组件（ESP-IDF 自动发现，M9 暂空）
├── build/               # 编译产物（不入库）
└── tools/
    ├── build.py
    └── burn.py
```
```

Verify:
```bash
cat bluetooth/esp32-wroom-32e/README.md | head -10
```

- [ ] **Step 5: Stub docs/development.md**

Create `bluetooth/esp32-wroom-32e/docs/development.md` with placeholder header (will be filled in Task 6):

```markdown
# ESP32-WROOM-32E 开发笔记

> 占位 — 在 Task 6（AGENTS.md / README.md 文档同步）阶段补完。
```

- [ ] **Step 6: Verify and commit**

Run:
```bash
cd .worktrees/m9-esp32-env  # or main worktree if executing there
git status --short
git add .gitignore bluetooth/esp32-wroom-32e/
git status --short
```
Expected: staged changes include `.gitignore` (modified), `bluetooth/esp32-wroom-32e/{README.md, docs/development.md, apps/.gitkeep, tools/.gitkeep}` (new).

```bash
git commit -m "chore(bluetooth): ESP32 project skeleton + build/ ignored

Create bluetooth/esp32-wroom-32e/{apps,tools} dirs and stub README;
ignore build/ outputs. No code yet; hello_world comes in Task 2."
```

---

## Task 2: First app — hello_world

**Files:**
- Create: `bluetooth/esp32-wroom-32e/apps/hello_world/CMakeLists.txt` (adapted from `/opt/esp-idf/examples/get-started/hello_world/CMakeLists.txt`)
- Create: `bluetooth/esp32-wroom-32e/apps/hello_world/main/main.c` (renamed from `examples/get-started/hello_world/main/hello_world_main.c`)
- Create: `bluetooth/esp32-wroom-32e/apps/hello_world/main/CMakeLists.txt` (kept, with file rename)
- Create: `bluetooth/esp32-wroom-32e/apps/hello_world/sdkconfig.defaults`
- Create: `bluetooth/esp32-wroom-32e/apps/hello_world/README.md`

**Interfaces:**
- Consumes: ESP-IDF v6.0.2, target=esp32, build dir `bluetooth/esp32-wroom-32e/build/hello_world/`
- Produces: a runnable ESP32 image; `idf.py -C apps/hello_world -B ../../build/hello_world build` exit 0; `build/hello_world/hello_world.bin` exists

> **Note on `main/` subdir:** ESP-IDF's standard project layout auto-discovers a `main/` subdir as the project's main component (`/opt/esp-idf/tools/cmake/project.cmake:485-487`). Removing it entirely breaks `idf.py build` ("component main was not found"). So this task **keeps `main/`** but renames the redundant `hello_world_main.c` → `main.c`. The user's earlier preference ("no `main/` subdir") is overridden by the ESP-IDF project convention; this deviation is recorded here so it's traceable. If a future ESP-IDF version lifts the `main/` requirement, the rename can be revisited.

- [ ] **Step 1: Copy example into our app dir (non-invasive)**

Run:
```bash
cp -r /opt/esp-idf/examples/get-started/hello_world bluetooth/esp32-wroom-32e/apps/hello_world
ls bluetooth/esp32-wroom-32e/apps/hello_world/
```
Expected: shows `CMakeLists.txt`, `main/`, `README.md` (and possibly `sdkconfig.defaults`, `pytest_hello_world.py` — depends on ESP-IDF version; ignore the pytest).

- [ ] **Step 2: Inspect example's source structure**

Run:
```bash
ls bluetooth/esp32-wroom-32e/apps/hello_world/main/
cat bluetooth/esp32-wroom-32e/apps/hello_world/main/hello_world_main.c | head -30
cat bluetooth/esp32-wroom-32e/apps/hello_world/main/CMakeLists.txt
cat bluetooth/esp32-wroom-32e/apps/hello_world/CMakeLists.txt
```
Expected:
- `main/hello_world_main.c` contains `#include <stdio.h>` and `printf("Hello world!\n");` (or similar)
- `main/CMakeLists.txt` has `idf_component_register(SRCS "hello_world_main.c" ...)`
- Root `CMakeLists.txt` has `project(hello_world)` and likely `include($ENV{IDF_PATH}/tools/cmake/project.cmake)` plus a component subdirectory include for `main`

Save the existing root `CMakeLists.txt` content for reference.

- [ ] **Step 3: Rename source file (drop the `hello_world_` prefix)**

Run:
```bash
mv bluetooth/esp32-wroom-32e/apps/hello_world/main/hello_world_main.c \
   bluetooth/esp32-wroom-32e/apps/hello_world/main/main.c
ls bluetooth/esp32-wroom-32e/apps/hello_world/main/
```
Expected: `main/` contains `CMakeLists.txt` and `main.c` (no `hello_world_main.c`).

- [ ] **Step 4: Update `main/CMakeLists.txt` to reference the renamed source**

Edit `bluetooth/esp32-wroom-32e/apps/hello_world/main/CMakeLists.txt`.

Original (typical):
```cmake
idf_component_register(SRCS "hello_world_main.c"
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES freertos
                       ...)
```

Update the `SRCS` line to reference `main.c`:
```cmake
idf_component_register(SRCS "main.c"
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES freertos
                       ...)
```

Keep all other fields (`INCLUDE_DIRS`, `PRIV_REQUIRES`, etc.) unchanged.

Verify:
```bash
cat bluetooth/esp32-wroom-32e/apps/hello_world/main/CMakeLists.txt
```
Expected: `SRCS "main.c"` (no `hello_world_main.c` reference).

- [ ] **Step 5: Add `sdkconfig.defaults`**

Create `bluetooth/esp32-wroom-32e/apps/hello_world/sdkconfig.defaults`:
```
# Pin target chip so re-builds skip the idf.py set-target prompt
CONFIG_IDF_TARGET="esp32"
```

- [ ] **Step 6: Stub `apps/hello_world/README.md`**

Create `bluetooth/esp32-wroom-32e/apps/hello_world/README.md`:
```markdown
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
```

- [ ] **Step 7: Verify build (the critical check for this task)**

Run:
```bash
source /opt/esp-idf/export.sh >/dev/null 2>&1
cd bluetooth/esp32-wroom-32e/apps/hello_world
idf.py -B ../../build/hello_world set-target esp32
idf.py -B ../../build/hello_world build
```
Expected: tail shows `Project build complete. To flash, run: ...` and exit 0.

Verify artifacts:
```bash
ls -la ../../build/hello_world/hello_world.bin ../../build/hello_world/bootloader/bootloader.bin 2>&1 | head
```
Expected: both files exist; sizes > 0.

If build fails, **stop and diagnose** before committing. Common issues:
- `idf.py` not found → `source /opt/esp-idf/export.sh` first
- Component `main` not found → check `main/CMakeLists.txt` exists and root `CMakeLists.txt` doesn't have a wrong `add_subdirectory`

- [ ] **Step 8: Commit**

Run:
```bash
cd .worktrees/m9-esp32-env
git add bluetooth/esp32-wroom-32e/apps/hello_world/
git status --short
```
Expected: staged changes show new files under `apps/hello_world/`.

```bash
git commit -m "feat(esp32): hello_world app — ESP-IDF v6.0.2 build verified

- Copied examples/get-started/hello_world/ into apps/hello_world/
- Renamed main/hello_world_main.c -> main/main.c (drop app-name prefix)
- main/ subdir kept: ESP-IDF auto-discovers it as the main component
  (project.cmake:485-487). Removing it breaks 'idf.py build'.
- Pinned CONFIG_IDF_TARGET=esp32 in sdkconfig.defaults
- idf.py build succeeds; hello_world.bin + bootloader.bin produced
- Build artifacts land under build/hello_world/ (not inside app/)"
```

---

## Task 3: Move `capture_uart.py` to top-level (shared refactor)

**Files:**
- Move: `wireless/tools/capture_uart.py` → `tools/capture_uart.py` (git mv preserves history)
- Modify: `AGENTS.md` — update "共享工具" section to point to new location

**Interfaces:**
- Consumes: existing `wireless/tools/capture_uart.py` (SLE-only today; no behavior change)
- Produces: `tools/capture_uart.py` with identical CLI/behavior; SLE path (`--board-a/--board-b/--rst-a/--rst-b`) unchanged

- [ ] **Step 1: Verify current `wireless/tools/capture_uart.py` is working**

Run:
```bash
ls -la wireless/tools/capture_uart.py
python3 wireless/tools/capture_uart.py --help 2>&1 | head -30
```
Expected: `--help` lists `--board-a`, `--board-b`, `--rst-a`, `--rst-b`, `--duration`, `--odir`, `--ts`, etc.

- [ ] **Step 2: `git mv` to top-level**

Run (from project root):
```bash
mkdir -p tools
git mv wireless/tools/capture_uart.py tools/capture_uart.py
git status --short
```
Expected: `R` (rename) status on `tools/capture_uart.py`; `wireless/tools/capture_uart.py` deleted.

- [ ] **Step 3: Verify CLI still works at new location**

Run:
```bash
python3 tools/capture_uart.py --help 2>&1 | head -30
```
Expected: same `--help` output as Step 1 (file content unchanged, just location moved).

- [ ] **Step 4: Update `AGENTS.md` "共享工具" section**

Edit `AGENTS.md` — find the line `**共享工具**（`wireless/tools/`）：` and replace the block with:

```markdown
**共享工具**（顶层 `tools/` + SLE 专用）：
- `tools/capture_uart.py` — 串口抓取（自动连串口 + 可选延迟复位 + 落盘 + 时间戳）。跨 SLE / 蓝牙两方向通用。
- `wireless/tools/burn.py` — SLE 烧录（ws63flash），SLE 专用。

```bash
# 烧录
python3 wireless/tools/burn.py board_a                 # BS21 default app
python3 wireless/tools/burn.py board_a -a sle_probe    # BS21 指定 app
python3 wireless/tools/burn.py board_a <h3863.fwpkg>  # H3863 显式传 fwpkg

# 抓 log（SLE 板）
python3 tools/capture_uart.py --board-a --board-b --rst-a --duration 60 --odir /tmp --ts
# 抓 log（ESP32 / 蓝牙方向，端口复用 BOARD_A_PORT）
python3 tools/capture_uart.py --board-a --duration 10 --odir /tmp --ts
```
```

The `--board-a/--board-b/--rst-a/--rst-b` flags are now shared across SLE and BT projects.

Verify with `grep -nE "capture_uart" AGENTS.md` — should show references to `tools/capture_uart.py` (top-level).

- [ ] **Step 5: Commit**

Run:
```bash
git add tools/capture_uart.py AGENTS.md
git status --short
```
Expected: rename + AGENTS.md modified.

```bash
git commit -m "refactor: move capture_uart.py to top-level tools/

Shared across SLE (wireless/) and Bluetooth (bluetooth/) projects. CLI
unchanged: --board-a/--board-b/--rst-a/--rst-b work for any board whose
serial path is in top-level .env (BOARD_A_PORT / BOARD_B_PORT /
CTRL_PIN). ESP32 reuses these keys (no new .env entries)."
```

---

## Task 4: Build wrapper — `tools/build.py`

**Files:**
- Create: `bluetooth/esp32-wroom-32e/tools/build.py` (executable Python)

**Interfaces:**
- Consumes: `apps/<app>/` (each is a self-contained ESP-IDF project), `idf.py` on PATH (after `source /opt/esp-idf/export.sh`)
- Produces: invokes `idf.py -C apps/<app> -B ../../build/<app> [set-target esp32] [build]`; exit code propagates; default app = `hello_world`

- [ ] **Step 1: Verify Python and idf.py available**

Run:
```bash
source /opt/esp-idf/export.sh >/dev/null 2>&1
which idf.py
python3 --version
```
Expected: `idf.py` found; Python 3.x.

- [ ] **Step 2: Write `tools/build.py`**

Create `bluetooth/esp32-wroom-32e/tools/build.py`:

```python
#!/usr/bin/env python3
"""ESP-IDF build wrapper for bluetooth/esp32-wroom-32e apps.

Usage:
    python3 tools/build.py            # default app = hello_world
    python3 tools/build.py --app bt_inquiry
    python3 tools/build.py --clean     # rm build/<app>/
    python3 tools/build.py --no-set-target  # skip set-target esp32

Activates /opt/esp-idf/export.sh, then runs idf.py with -C apps/<app>
and -B ../../build/<app> (so build output lands at project-level build/<app>).
"""

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parent.parent
APPS_DIR = PROJECT_ROOT / "apps"
BUILD_DIR = PROJECT_ROOT / "build"
IDF_PATH = Path("/opt/esp-idf")


def activate_idf() -> None:
    if shutil.which("idf.py") is not None:
        return
    export_sh = IDF_PATH / "export.sh"
    if not export_sh.exists():
        sys.exit(f"idf.py not on PATH and {export_sh} not found; "
                 "install esp-idf (yay -S esp-idf) or source its env manually")
    subprocess.run(["bash", "-c", f"source {export_sh} >/dev/null 2>&1 && "
                                   "which idf.py"],
                   check=True)


def app_dir(name: str) -> Path:
    p = APPS_DIR / name
    if not p.is_dir():
        sys.exit(f"app not found: {p} (existing apps: "
                 f"{', '.join(d.name for d in APPS_DIR.iterdir() if d.is_dir())})")
    return p


def run(cmd: list[str], cwd: Path) -> None:
    print(f"$ {' '.join(cmd)} (cwd={cwd})", flush=True)
    res = subprocess.run(cmd, cwd=cwd)
    if res.returncode != 0:
        sys.exit(res.returncode)


def main() -> None:
    ap = argparse.ArgumentParser(description="Build an ESP-IDF app under bluetooth/esp32-wroom-32e")
    ap.add_argument("--app", default="hello_world", help="app name under apps/")
    ap.add_argument("--clean", action="store_true",
                    help="rm build/<app>/ before building")
    ap.add_argument("--no-set-target", action="store_true",
                    help="skip 'idf.py set-target esp32' (assumes already set)")
    args = ap.parse_args()

    activate_idf()

    app = app_dir(args.app)
    build = BUILD_DIR / args.app
    build_rel = os.path.relpath(build, app)
    # When running from apps/<app>/, "build" is at ../../build/<app>/

    if args.clean and build.exists():
        print(f"removing {build}", flush=True)
        shutil.rmtree(build)

    if not args.no_set_target:
        run(["idf.py", "-B", build_rel, "set-target", "esp32"], cwd=app)

    run(["idf.py", "-B", build_rel, "build"], cwd=app)


if __name__ == "__main__":
    main()
```

Make executable: `chmod +x bluetooth/esp32-wroom-32e/tools/build.py`

- [ ] **Step 3: Verify `--help` works**

Run:
```bash
source /opt/esp-idf/export.sh >/dev/null 2>&1
python3 bluetooth/esp32-wroom-32e/tools/build.py --help
```
Expected output (relevant lines):
```
usage: build.py [-h] [--app APP] [--clean] [--no-set-target]
```

- [ ] **Step 4: Verify build invocation reuses Task 2's output (no full rebuild)**

Run (with existing `build/hello_world/` from Task 2):
```bash
source /opt/esp-idf/export.sh >/dev/null 2>&1
python3 bluetooth/esp32-wroom-32e/tools/build.py --no-set-target 2>&1 | tail -10
```
Expected: tail shows `Project build complete.` (no rebuild needed because Task 2 already produced artifacts; set-target was already done).

If Task 2 was cleaned (no existing build), omit `--no-set-target`:
```bash
python3 bluetooth/esp32-wroom-32e/tools/build.py 2>&1 | tail -10
```
Expected: `Project build complete.`

- [ ] **Step 5: Verify `--clean` actually clears the build dir**

Run:
```bash
ls bluetooth/esp32-wroom-32e/build/hello_world/hello_world.bin
python3 bluetooth/esp32-wroom-32e/tools/build.py --clean --no-set-target 2>&1 | tail -5
ls bluetooth/esp32-wroom-32e/build/hello_world/ 2>&1 | head -3
```
Expected: `--clean` produces a fresh build (notice "Removing" output); `build/hello_world/hello_world.bin` exists again.

- [ ] **Step 6: Commit**

```bash
git add bluetooth/esp32-wroom-32e/tools/build.py
git commit -m "feat(esp32): build.py wrapper around idf.py

Default app = hello_world. --app <n> selects another app under apps/.
--clean wipes build/<app>/. --no-set-target skips the interactive
'idf.py set-target esp32' step.

Uses idf.py -B ../../build/<app> so build outputs land at project-level
build/<app>/ (not inside apps/<app>/)."
```

---

## Task 5: Burn wrapper — `tools/burn.py`

**Files:**
- Create: `bluetooth/esp32-wroom-32e/tools/burn.py` (executable Python)

**Interfaces:**
- Consumes: top-level `.env` (reads `BOARD_A_PORT` or `BOARD_B_PORT`); CLI selects which board (default `--board-a`)
- Produces: invokes `idf.py -C apps/<app> -B ../../build/<app> -p <port> flash`; exit code propagates

- [ ] **Step 1: Verify `.env` has `BOARD_A_PORT`**

Run:
```bash
test -f .env && echo "exists" || echo "missing"
grep -E "^(BOARD_A_PORT|BOARD_B_PORT)=" .env 2>/dev/null
```
Expected: `.env` exists; one of `BOARD_A_PORT` / `BOARD_B_PORT` is set to a `/dev/serial/by-path/...` path.

If `.env` is missing, **stop and ask the user** to populate it from `.env.example`.

- [ ] **Step 2: Write `tools/burn.py`**

Create `bluetooth/esp32-wroom-32e/tools/burn.py`:

```python
#!/usr/bin/env python3
"""ESP-IDF flash wrapper for bluetooth/esp32-wroom-32e apps.

Usage:
    python3 tools/burn.py                       # default: hello_world, --board-a
    python3 tools/burn.py --board-b             # use BOARD_B_PORT
    python3 tools/burn.py --app bt_inquiry
    python3 tools/burn.py --port /dev/ttyUSB0   # explicit override (skips .env)

Reads serial port from top-level .env (BOARD_A_PORT or BOARD_B_PORT).
ESP32 reuses the same keys as SLE — no new .env entries required.
"""

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parent.parent
REPO_ROOT = PROJECT_ROOT.parent.parent
ENV_FILE = REPO_ROOT / ".env"
APPS_DIR = PROJECT_ROOT / "apps"
BUILD_DIR = PROJECT_ROOT / "build"
IDF_PATH = Path("/opt/esp-idf")


def load_env() -> dict[str, str]:
    env: dict[str, str] = {}
    if ENV_FILE.is_file():
        for line in ENV_FILE.read_text().splitlines():
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            if "=" in line:
                k, v = line.split("=", 1)
                env[k.strip()] = v.strip().strip('"').strip("'")
    return env


def activate_idf() -> None:
    if shutil.which("idf.py") is not None:
        return
    export_sh = IDF_PATH / "export.sh"
    if not export_sh.exists():
        sys.exit("idf.py not on PATH; install esp-idf (yay -S esp-idf) or "
                 "source /opt/esp-idf/export.sh first")
    subprocess.run(["bash", "-c", f"source {export_sh} >/dev/null 2>&1"],
                   check=True)


def app_dir(name: str) -> Path:
    p = APPS_DIR / name
    if not p.is_dir():
        sys.exit(f"app not found: {p}")
    return p


def run(cmd: list[str], cwd: Path) -> None:
    print(f"$ {' '.join(cmd)} (cwd={cwd})", flush=True)
    res = subprocess.run(cmd, cwd=cwd)
    if res.returncode != 0:
        sys.exit(res.returncode)


def main() -> None:
    ap = argparse.ArgumentParser(description="Flash an ESP-IDF app under bluetooth/esp32-wroom-32e")
    ap.add_argument("--app", default="hello_world")
    ap.add_argument("--board-a", action="store_true", default=True,
                    help="use BOARD_A_PORT from .env (default)")
    ap.add_argument("--board-b", action="store_true",
                    help="use BOARD_B_PORT from .env")
    ap.add_argument("--port", help="explicit serial port override")
    args = ap.parse_args()

    env = load_env()

    if args.port:
        port = args.port
    else:
        key = "BOARD_B_PORT" if args.board_b else "BOARD_A_PORT"
        if key not in env:
            sys.exit(f"{key} not set in {ENV_FILE}; "
                     "either add it or pass --port <path>")
        port = env[key]

    activate_idf()

    app = app_dir(args.app)
    build = BUILD_DIR / args.app
    build_rel = os.path.relpath(build, app)

    run(["idf.py", "-B", build_rel, "-p", port, "flash"], cwd=app)


if __name__ == "__main__":
    main()
```

Make executable: `chmod +x bluetooth/esp32-wroom-32e/tools/burn.py`

- [ ] **Step 3: Verify `--help`**

Run:
```bash
source /opt/esp-idf/export.sh >/dev/null 2>&1
python3 bluetooth/esp32-wroom-32e/tools/burn.py --help
```
Expected: lists `--app`, `--board-a`, `--board-b`, `--port`.

- [ ] **Step 4: Verify port resolution from `.env` (dry-run style)**

Add a temporary print to inspect, then run:
```bash
python3 -c "
import sys; sys.path.insert(0, 'bluetooth/esp32-wroom-32e/tools')
import burn
print('resolved port:', burn.load_env().get('BOARD_A_PORT'))
"
```
Expected: prints the serial path from `.env`. Remove the inspection script after.

If `.env` has no `BOARD_A_PORT`, this prints `None` — stop and populate `.env`.

- [ ] **Step 5: Smoke-test `burn.py` invocation WITHOUT actually flashing (no board)**

Use the `--port` override pointing at a non-existent path to confirm the wrapper reaches `idf.py flash`:
```bash
python3 bluetooth/esp32-wroom-32e/tools/burn.py --port /dev/serial/by-path/no-such-port 2>&1 | tail -10
```
Expected: `idf.py` exits non-zero with a serial-port-not-found error (proving the wrapper correctly passes the port through to idf.py). Exit code != 0 is fine.

If `idf.py` itself fails to start (e.g., export.sh not sourced), the wrapper exits with its own message.

- [ ] **Step 6: Commit**

```bash
git add bluetooth/esp32-wroom-32e/tools/burn.py
git commit -m "feat(esp32): burn.py wrapper around idf.py flash

Default port from .env BOARD_A_PORT; --board-b selects BOARD_B_PORT;
--port overrides explicitly. Wraps 'idf.py -B ../../build/<app>
-p <port> flash'.

ESP32 reuses the SLE .env keys — no new entries needed."
```

- [ ] **Step 7: Manual hardware verification (one-time, requires ESP32 DevKitC)**

**Do NOT commit this step.** This is the user-facing verification from spec section 八 step 3.

Prereq: ESP32-WROOM-32E DevKitC plugged into the slot whose path matches `BOARD_A_PORT` (or `BOARD_B_PORT`).

Run:
```bash
python3 bluetooth/esp32-wroom-32e/tools/burn.py 2>&1 | tail -20
```
Expected tail (verbatim):
```
Leaving...
Hard resetting via RTS pin...
Done
```
Or:
```
Hash of data verified.
Leaving...
Hard resetting via RTS pin...
```

If output ends with `Connecting........__...` (no `___...___` dot-fill), the board is not connected to that port. Stop and check wiring.

After successful flash, run capture (next task verifies this works):

```bash
python3 tools/capture_uart.py --board-a --duration 10 --odir /tmp --ts
```
Expected: `Hello world!` + ESP32 boot log.

---

## Task 6: Documentation sync (AGENTS.md + README.md)

**Files:**
- Modify: `AGENTS.md` — "项目状态" + platform sections
- Modify: `README.md` — "项目目标" / "硬件环境" / "项目结构" / "开发状态"

**Interfaces:**
- Consumes: existing AGENTS.md structure (kept intact), existing README.md
- Produces: docs that reflect the new BT direction + tools move

- [ ] **Step 1: Read current `AGENTS.md` and `README.md`**

Run:
```bash
head -40 AGENTS.md
echo "---"
head -40 README.md
```
Use the content for the edits below.

- [ ] **Step 2: Append BT direction note to `AGENTS.md` "项目状态"**

Locate the **first** section heading `## 项目状态` in AGENTS.md. After the existing paragraph(s), append:

```markdown

### 蓝牙方向（新，与 SLE 独立）

- ESP32-WROOM-32E（ESP32-D0WD-V3，Xtensa LX6 双核 240MHz，rev3.1）
- ESP-IDF v6.0.2（yay AUR `esp-idf`，`/opt/esp-idf`，**不**复制到 `~/workspace`）
- 仅经典 ESP32 在 ESP-IDF 全家族中带 BR/EDR（S2/S3/C3/C5/C6/H2/C61/E22 全 BLE-only），故 ESP32-WROOM-32E 是 BR/EDR HID 主机研究的唯一对口芯片
- 项目在 `bluetooth/esp32-wroom-32e/`（与 `wireless/` 平级），非侵入式编译，多 app（`apps/<app>/` + `build/<app>/`）
- 烧录 `bluetooth/esp32-wroom-32e/tools/{build,burn}.py` 调 ESP-IDF；串口抓取走顶层 `tools/capture_uart.py`（与 SLE 共用，端口从 `.env` 的 `BOARD_A_PORT`/`BOARD_B_PORT` 复用，不增键）
- **M9 = 环境搭建**（hello_world 编译/烧录/串口），不涉及手柄；后续 M10/M3+ 起做 BT inquiry、HID 主机连接
- 设计文档：`docs/superpowers/specs/2026-09-05-esp32-env-setup-design.md`，实施计划：`docs/superpowers/plans/2026-09-05-esp32-env-setup.md`
```

- [ ] **Step 3: Append BT entry to `AGENTS.md` platforms list**

Find any existing platform enumeration (e.g., "## 平台"). If present, add:

```markdown

### ESP32-WROOM-32E 开发板（蓝牙方向，新平台）

详见 `bluetooth/esp32-wroom-32e/README.md`（芯片规格、ESP-IDF 位置、构建/烧录/串口流程）。

- **非侵入式**：ESP-IDF `/opt/esp-idf` 全程只读；`apps/` 下 example 通过 `cp -r` 复制后再改
- **多 app**：每个 app 是独立 ESP-IDF 项目（顶层 `CMakeLists.txt` + `main/` 组件）；编译产物通过 `-B ../../build/<app>` 落到 board 顶层 `build/<app>/`
- **共享工具**：与 SLE 共用顶层 `tools/capture_uart.py`；ESP32 专用 `tools/{build,burn}.py` 跟随项目
```

If no such enumeration exists, skip this step and add the same content under a new heading `## 平台 / ESP32-WROOM-32E` near the top.

- [ ] **Step 4: Update `README.md` "硬件环境" table**

Edit the `| 设备 | 型号 | 用途 |` table in `README.md`. Add a row:

```markdown
| ESP32-WROOM-32E | BearPi-style DevKitC | 蓝牙方向（BR/EDR HID）研究 |
```

- [ ] **Step 5: Update `README.md` "项目结构" tree**

Locate the code block showing the project structure. Replace it with:

```
flydigi-receiver/
├── docs/                          # 通用文档
│   ├── sle-analysis.md            # SLE 协议分析与可行性评估
│   ├── controller-modes.md        # 手柄模式与协议详解
│   ├── history.md                 # 项目历史与尝试记录
│   ├── sle-chip-comparison.md     # 芯片规格对比
│   ├── reference/                 # 官方文档存档
│   └── superpowers/               # 历史计划/设计（按日期）
├── tools/                         # 跨平台共享工具
│   ├── notify.sh                  # 提示音
│   └── capture_uart.py            # 串口抓取（SLE + BT 共享）
├── wireless/                      # 无线接收器（SLE 专用）
│   ├── ai-bs21-32s-kit/           # BS21 平台（已挂起）
│   ├── bearpi-pico-h3863/         # H3863 平台（主平台）
│   └── tools/
│       └── burn.py                # SLE 烧录（ws63flash）
├── bluetooth/                     # 蓝牙方向（与 wireless/ 平级）
│   └── esp32-wroom-32e/
│       ├── README.md              # 平台概览
│       ├── docs/development.md    # ESP-IDF 流程
│       ├── apps/                  # 各 app（hello_world 等）
│       │   └── hello_world/
│       │       ├── CMakeLists.txt
│       │       ├── main/
│       │       │   ├── CMakeLists.txt
│       │       │   └── main.c
│       │       ├── sdkconfig.defaults
│       │       └── README.md
│       ├── components/            # 跨 app 共享的 ESP-IDF 组件（自动发现）
│       ├── build/                 # 编译产物（不入库）
│       └── tools/                 # build.py / burn.py
├── .env                           # 顶层串口 + ctrl pin（不入库）
└── AGENTS.md                      # 工程记忆
```

- [ ] **Step 6: Update `README.md` "开发状态" section**

Edit the "开发状态" section to add a BT line:

```markdown
- **H3863（主平台）**：开发环境打通（Hello World 验证），SLE 接收器功能待迁移
- **BS21（已挂起）**：因 SDK 限制后期可能废弃
- **ESP32-WROOM-32E（蓝牙方向）**：M9 环境搭建中，hello_world 编译/烧录/串口验证完成；M10+ 起做 BT inquiry、HID 主机连接
```

- [ ] **Step 7: Fill in `bluetooth/esp32-wroom-32e/docs/development.md`**

Replace the placeholder from Task 1 Step 5 with:

```markdown
# ESP32-WROOM-32E 开发笔记（M9 / 蓝牙方向）

## 环境前提

- ESP-IDF v6.0.2 已通过 yay AUR 安装：`yay -S esp-idf`
- 工具链在 `~/.espressif/tools/xtensa-esp-elf/esp-15.2.0_20251204/`
- 激活：`source /opt/esp-idf/export.sh`（idempotent，可放 `.bashrc`）
- 验证：`idf.py --version` → `ESP-IDF v6.0.2`
- ESP32 在 ESP-IDF v6.0 仍受支持（`examples/get-started/hello_world/README.md` 的 Supported Targets 含 `ESP32`）

## 项目布局

- `apps/<app>/` — 每个 app 一个独立 ESP-IDF 项目（顶层 `CMakeLists.txt` + 源文件 + `sdkconfig.defaults`）
- `build/<app>/` — 编译产物，通过 `idf.py -B ../../build/<app>` 落到 board 顶层
- `tools/build.py` — `idf.py set-target && build` 包装；`--app <n>`、 `--clean`、 `--no-set-target`
- `tools/burn.py` — `idf.py flash` 包装；端口从顶层 `.env` 的 `BOARD_A_PORT` / `BOARD_B_PORT` 读
- `docs/` — 平台专属文档

## 串口抓取

与 SLE 共用顶层 `tools/capture_uart.py`：

```bash
python3 tools/capture_uart.py --board-a --duration 10 --odir /tmp --ts
```

不需要新增 `.env` 键——ESP32 复用 SLE 的 `BOARD_A_PORT` / `BOARD_B_PORT` / `CTRL_PIN`。

## 添加新 app

```bash
# 1. 从 ESP-IDF example 复制（不修改原 example）
cp -r /opt/esp-idf/examples/bluetooth/blufi bluetooth/esp32-wroom-32e/apps/blufi

# 2. 保留 example 自带的 main/ 子目录（ESP-IDF 自动发现为 main 组件）
# 3. 写 apps/blufi/sdkconfig.defaults（如需）
# 4. 构建 + 烧录
python3 tools/build.py --app blufi
python3 tools/burn.py --app blufi
```

## 添加共享 component（跨 app 复用代码）

```bash
# 例：BT 通用工具组件（被 hello_world / bt_inquiry / 后续 app 都依赖）
mkdir -p bluetooth/esp32-wroom-32e/components/bt_common/include
```

`components/<name>/` 下标准结构：

```
components/bt_common/
├── CMakeLists.txt       # idf_component_register(SRCS "bt_common.c" REQUIRES bt driver)
├── include/             # 公开头文件（其他组件 #include "bt_common.h" 用）
│   └── bt_common.h
├── bt_common.c
└── bt_common.h          # 私有头文件（仅本组件内）
```

ESP-IDF 自动发现 `components/<name>/`（`project.cmake:500`），无需在根 `CMakeLists.txt` 写 `EXTRA_COMPONENT_DIRS`。

其他组件 / app 通过 `idf_component_register(... REQUIRES bt_common)` 引用；头文件用 `#include "bt_common.h"`（公开头在 `include/` 子目录里，ESP-IDF 自动加进 include 路径）。

## 故障排查

| 症状 | 可能原因 | 处理 |
|---|---|---|
| `idf.py: command not found` | export.sh 未 source | `source /opt/esp-idf/export.sh` |
| 烧录卡在 `Connecting...` | 端口错 / 模块未上电 | 查 `.env` 的 `BOARD_A_PORT`；检查 DevKitC USB；`lsusb` 看 CP2102/CH340 |
| 烧录后串口无输出 | GPIO 不对 / 波特率不对 | DevKitC UART0 默认 GPIO1/3、115200；`idf.py monitor` 验证 |
| `Hard resetting via RTS pin` 后无 log | USB-UART 桥未触发 boot | 按 DevKitC 上的 EN 按钮手动复位；或确认 CP2102 DTR/RTS 接对 |
| `main` 组件找不到 | `main/CMakeLists.txt` 缺失或 `add_subdirectory(main)` 误删 | 恢复 `main/CMakeLists.txt`（含 `idf_component_register(SRCS ...)`） |

## 后续里程碑（不在 M9 范围）

- M10：BT inquiry（经典 BT 扫描，看到手柄）
- M11+：BluedR HID 主机连接手柄（`esp_hid_host`）
- HID 报告 TLV 化（复用现有 `formatter`）
```

- [ ] **Step 8: Verify all docs are coherent**

Run:
```bash
grep -nE "capture_uart|bluetooth/" AGENTS.md README.md | head -20
```
Expected: `AGENTS.md` and `README.md` reference `tools/capture_uart.py` (top-level) and `bluetooth/esp32-wroom-32e/`.

- [ ] **Step 9: Commit**

```bash
git add AGENTS.md README.md bluetooth/esp32-wroom-32e/docs/development.md
git commit -m "docs: sync AGENTS.md + README.md for BT direction + tools move

- AGENTS.md: add 蓝牙方向 section, ESP32 platform block; update
  共享工具 entry (capture_uart.py -> top-level tools/, burn.py stays
  wireless/tools/ for SLE-only ws63flash).
- README.md: add ESP32-WROOM-32E to hardware table; restructure
  project tree to show tools/, bluetooth/, and merged wireless/tools/.
- bluetooth/esp32-wroom-32e/docs/development.md: full env + workflow
  notes (ESP-IDF location, build dir convention, app addition, debug)."
```

---

## Done (full M9 verification, spec section 八)

After Task 6, perform the spec's verification matrix end-to-end:

- [ ] **Final: `idf.py --version`**

```bash
source /opt/esp-idf/export.sh >/dev/null 2>&1
idf.py --version
```
Expected: `ESP-IDF v6.0.2`.

- [ ] **Final: `tools/build.py` from clean**

```bash
rm -rf bluetooth/esp32-wroom-32e/build
python3 bluetooth/esp32-wroom-32e/tools/build.py 2>&1 | tail -3
```
Expected: `Project build complete. To flash, run: ...`; exit 0.

- [ ] **Final: `tools/burn.py` against DevKitC**

```bash
python3 bluetooth/esp32-wroom-32e/tools/burn.py 2>&1 | tail -5
```
Expected: tail shows `Hash of data verified. Leaving... Hard resetting via RTS pin...` (or equivalent success line).

- [ ] **Final: `capture_uart.py` shows output**

```bash
python3 tools/capture_uart.py --board-a --duration 10 --odir /tmp --ts
```
Expected: stdout and `/tmp/capture_<ts>.log` contain `Hello world!` and `rst:0x1 (POWERON)` (or similar ESP32 boot log).

All four pass → M9 complete.