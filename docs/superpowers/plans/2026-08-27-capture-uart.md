# capture_uart 串口日志捕获脚本实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 提供一个 `wireless/bs21/tools/capture_uart.py` 脚本，一键完成「连串口 → 延迟复位 → 持续捕获两板串口日志到文件」。

**Architecture:** 单文件 Python 脚本 + `pytest` 单测。核心为「纯逻辑函数（参数校验/时间戳/路径）」与「IO 主循环（pyserial + select）」分离，复位用 `uart-gpio pulse`。纯逻辑可单测，IO 用 socat 虚拟串口对做集成测试。

**Tech Stack:** Python 3（stdlib argparse/select/signal/subprocess + pyserial），pytest，socat，uart-gpio。

**Spec:** `docs/superpowers/specs/2026-08-27-capture-uart-design.md`

## Global Constraints

- 脚本放 `wireless/bs21/tools/capture_uart.py`，风格对齐 `burn.py`（模块级函数 + `main()`，模块 docstring 说明用法，无注释或仅必要 why 注释，英文）。
- 串口/复位端口从项目根 `.env` 读取（`BS21_BOARD_A_PORT`/`BS21_BOARD_B_PORT`、`BS21_BOARD_*_RST_PORT`/`BS21_BOARD_*_RST_PIN`）；函数接受显式 `env` 参数以便测试注入，`main` 缺省用模块级 `ENV`。
- CLI：`--board-a`/`--board-b`（至少选一个，可同选）、`--rst-a`/`--rst-b`（仅对已选板有效）、`--reset-delay`（默认 1.0）、`--duration`（默认 60）、`--odir`（默认 /tmp）、`--ts`、`--no-echo`。
- 复位顺序固定 A → B；复位命令 `uart-gpio pulse <rst_port> A <pin> 0 2000`。
- 输出文件 `board_a_<YYYYmmdd_HHMMSS>.log` / `board_b_...`，`--ts` 时每行（`\n` 分界）前缀 `[+<秒>.<毫秒>] `（相对捕获起点，共享同一 t0）。
- 退出：duration 到 → exit 0；Ctrl+C → flush 残行保存 → exit 130；参数校验失败 → exit 2；串口全开失败 → exit 1。
- **不自动 commit**：每个任务结束不执行 `git commit`，由用户决定何时提交。
- 每次代码变更后运行 code-simplifier 技能做简化（保持功能）。
- 任务执行完更新 `AGENTS.md` 手动抓 log 段落为引用本脚本。
- 测试运行方式：`cd wireless/bs21/tools && python3 -m pytest test_capture_uart.py -v`。
- 真实板冒烟在主工作区（有 `.env`）进行，worktree 内只做 socat 虚拟串口集成测试。

---

### Task 1: 参数解析、校验与输出路径（纯逻辑）

**Files:**
- Create: `wireless/bs21/tools/capture_uart.py`
- Test: `wireless/bs21/tools/test_capture_uart.py`

**Interfaces:**
- Produces:
  - `parse_args(argv=None) -> argparse.Namespace`，字段：`board_a`、`board_b`、`rst_a`、`rst_b`、`reset_delay`(float)、`duration`(int)、`odir`(str)、`ts`(bool)、`no_echo`(bool)。非法参数 `argparse.ArgumentParser.error()`（exit 2）。
  - `make_output_path(odir, board, start_dt) -> str`，如 `/tmp/board_a_20260827_103000.log`。

- [ ] **Step 1: 写失败测试**

```python
import datetime
import pytest

import capture_uart as cu


def test_at_least_one_board_required():
    with pytest.raises(SystemExit):
        cu.parse_args([])


def test_both_boards_allowed():
    a = cu.parse_args(["--board-a", "--board-b"])
    assert a.board_a and a.board_b


def test_rst_requires_matching_board():
    with pytest.raises(SystemExit):
        cu.parse_args(["--board-a", "--rst-b"])
    with pytest.raises(SystemExit):
        cu.parse_args(["--board-b", "--rst-a"])


def test_defaults():
    a = cu.parse_args(["--board-a"])
    assert a.reset_delay == 1.0
    assert a.duration == 60
    assert a.odir == "/tmp"
    assert not a.ts and not a.no_echo


def test_make_output_path():
    dt = datetime.datetime(2026, 8, 27, 10, 30, 0)
    assert cu.make_output_path("/tmp", "board_a", dt) == "/tmp/board_a_20260827_103000.log"
    assert cu.make_output_path("/tmp", "board_b", dt) == "/tmp/board_b_20260827_103000.log"
```

- [ ] **Step 2: 运行测试确认失败**

Run: `cd wireless/bs21/tools && python3 -m pytest test_capture_uart.py -v`
Expected: FAIL（`ModuleNotFoundError: No module named 'capture_uart'`）

- [ ] **Step 3: 实现脚本骨架（本任务只需 parse_args / make_output_path + 模块头）**

```python
#!/usr/bin/env python3
"""capture_uart: capture BS21 board serial output with optional delayed reset.

Usage:
    python3 wireless/bs21/tools/capture_uart.py --board-a --board-b --rst-a --duration 60 --odir /tmp

Board/serial mapping is read from the project .env file:
    BS21_BOARD_A_PORT / BS21_BOARD_A_RST_PORT / BS21_BOARD_A_RST_PIN
    BS21_BOARD_B_PORT / BS21_BOARD_B_RST_PORT / BS21_BOARD_B_RST_PIN
"""

import argparse
import os
from datetime import datetime

BAUD = 115200
DEFAULT_RESET_DELAY = 1.0
DEFAULT_DURATION = 60
DEFAULT_ODIR = "/tmp"
RESET_PULSE_MS = 2000


def parse_args(argv=None):
    p = argparse.ArgumentParser(description="Capture BS21 board serial output")
    p.add_argument("--board-a", action="store_true", help="capture board_a")
    p.add_argument("--board-b", action="store_true", help="capture board_b")
    p.add_argument("--rst-a", action="store_true", help="reset board_a after connect")
    p.add_argument("--rst-b", action="store_true", help="reset board_b after connect")
    p.add_argument("--reset-delay", type=float, default=DEFAULT_RESET_DELAY,
                   help="seconds to wait before reset (default: %(default)s)")
    p.add_argument("--duration", type=int, default=DEFAULT_DURATION,
                   help="capture duration in seconds (default: %(default)s)")
    p.add_argument("--odir", default=DEFAULT_ODIR, help="output directory (default: %(default)s)")
    p.add_argument("--ts", action="store_true", help="prefix each line with [+sec.msec]")
    p.add_argument("--no-echo", action="store_true", help="disable live terminal echo")
    args = p.parse_args(argv)
    if not (args.board_a or args.board_b):
        p.error("must select at least one board: --board-a and/or --board-b")
    if args.rst_a and not args.board_a:
        p.error("--rst-a requires --board-a")
    if args.rst_b and not args.board_b:
        p.error("--rst-b requires --board-b")
    return args


def make_output_path(odir, board, start_dt):
    return os.path.join(odir, "%s_%s.log" % (board, start_dt.strftime("%Y%m%d_%H%M%S")))
```

- [ ] **Step 4: 运行测试确认通过**

Run: `cd wireless/bs21/tools && python3 -m pytest test_capture_uart.py -v`
Expected: 6 passed

---

### Task 2: 时间戳行缓存 `LineTimestamping`

**Files:**
- Modify: `wireless/bs21/tools/capture_uart.py`
- Modify: `wireless/bs21/tools/test_capture_uart.py`

**Interfaces:**
- Consumes: 无（纯逻辑）
- Produces:
  - `class LineTimestamping`: `__init__(self, t0)`（`t0` 为 `time.monotonic()` 起点）；`feed(self, data) -> list[tuple[float, bytes]]`（按 `\n` 分界的完整行 + 自 `t0` 的 elapsed 秒）；`flush(self) -> tuple[float, bytes] | None`（剩余残行）。
  - `ts_prefix(elapsed) -> bytes`，如 `b"[+1.023] "`。

- [ ] **Step 1: 写失败测试**

```python
import capture_uart as cu


def test_ts_prefix_format():
    assert cu.ts_prefix(1.023) == b"[+1.023] "
    assert cu.ts_prefix(0.0) == b"[+0.000] "
    assert cu.ts_prefix(65.5) == b"[+65.500] "


def test_feed_emits_complete_lines():
    ts = cu.LineTimestamping(0.0)
    out = ts.feed(b"boot.\nFlashboot Init!\n")
    assert len(out) == 2
    assert out[0][1] == b"boot.\n"
    assert out[1][1] == b"Flashboot Init!\n"


def test_feed_buffers_partial_line():
    ts = cu.LineTimestamping(0.0)
    assert ts.feed(b"boot.") == []
    assert ts.feed(b"\nrest") == [(pytest.approx(0.0), b"boot.\n")]
    assert ts.flush() is not None
    assert ts.flush()[1] == b"rest"
```

（Step 1 末尾补一行 `import pytest` 到文件顶部。）

- [ ] **Step 2: 运行测试确认失败**

Run: `cd wireless/bs21/tools && python3 -m pytest test_capture_uart.py -v`
Expected: FAIL（`AttributeError: module 'capture_uart' has no attribute 'ts_prefix'`）

- [ ] **Step 3: 实现**

```python
import time


def ts_prefix(elapsed):
    s, ms = divmod(int(elapsed * 1000), 1000)
    return ("[+%d.%03d] " % (s, ms)).encode()


class LineTimestamping:
    """Split raw bytes into lines on \\n, tagging each with elapsed seconds."""

    def __init__(self, t0):
        self.t0 = t0
        self.buf = bytearray()

    def feed(self, data):
        self.buf.extend(data)
        out = []
        while True:
            idx = self.buf.find(b"\n")
            if idx < 0:
                break
            line = bytes(self.buf[: idx + 1])
            del self.buf[: idx + 1]
            out.append((time.monotonic() - self.t0, line))
        return out

    def flush(self):
        if not self.buf:
            return None
        tail = bytes(self.buf)
        self.buf.clear()
        return time.monotonic() - self.t0, tail
```

- [ ] **Step 4: 运行测试确认通过**

Run: `cd wireless/bs21/tools && python3 -m pytest test_capture_uart.py -v`
Expected: 10 passed

---

### Task 3: 捕获主循环 `capture`（select + 回显 + 写文件 + duration/Ctrl+C 退出）

**Files:**
- Modify: `wireless/bs21/tools/capture_uart.py`
- Modify: `wireless/bs21/tools/test_capture_uart.py`

**Interfaces:**
- Consumes: `LineTimestamping`、`ts_prefix`
- Produces:
  - `capture(streams, outs, duration, ts, echo, reset_plan=None, delay=DEFAULT_RESET_DELAY) -> tuple[dict[str, int], bool]`
    - `streams`: `dict[serial-like, board_name]`；`outs`: `dict[board_name, file_obj]`
    - 执行顺序：记 `t0` → 若 `reset_plan` 非空则 `sleep(delay)` 后按序执行 → 主循环 select/read/echo/write
    - 返回 `(totals, interrupted)`，`totals[b]` 为字节数，`interrupted` 为 Ctrl+C 是否发生
    - Ctrl+C（KeyboardInterrupt）在循环内捕获，flush 残行后返回，`interrupted=True`
    - 正常到 `duration` 结束同样 flush 残行，`interrupted=False`
- Produces（本任务暂不实现复位本体，见 Task 4）: `reset_plan` 由 Task 4 提供；Task 3 测试用空 `reset_plan`。

- [ ] **Step 1: 写失败测试**

```python
import os
import subprocess
import time

import pytest
import serial

import capture_uart as cu


class _FakeSerial:
    pass


def test_capture_ctrl_c_saves_partial_line(monkeypatch, tmp_path):
    def boom(*a, **k):
        raise KeyboardInterrupt

    monkeypatch.setattr(cu.select, "select", boom)
    streams = {_FakeSerial(): "board_a"}
    out = tmp_path / "a.log"
    with open(out, "wb") as f:
        totals, interrupted = cu.capture(streams, {"board_a": f}, 10, True, False)
    assert interrupted
    assert totals["board_a"] == 0


@pytest.fixture
def socat_pair(tmp_path):
    a = str(tmp_path / "tty_a")
    b = str(tmp_path / "tty_b")
    proc = subprocess.Popen(["socat", "pty,raw,echo=0,link=%s" % a,
                             "pty,raw,echo=0,link=%s" % b])
    for _ in range(50):
        if os.path.exists(a) and os.path.exists(b):
            break
        time.sleep(0.05)
    try:
        yield a, b
    finally:
        proc.terminate()
        proc.wait()


def test_capture_socat_raw_duration_exit(tmp_path, socat_pair):
    a, b = socat_pair
    with serial.Serial(b, 115200, timeout=0.1) as sb:
        sb.write(b"hello world\n")
        with serial.Serial(a, 115200, timeout=0.05) as sa:
            out = tmp_path / "a.log"
            with open(out, "wb") as f:
                totals, interrupted = cu.capture({sa: "board_a"}, {"board_a": f},
                                                 0.8, False, False)
    assert not interrupted
    assert out.read_bytes() == b"hello world\n"
    assert totals["board_a"] == len(b"hello world\n")


def test_capture_socat_ts_prefixes(tmp_path, socat_pair):
    a, b = socat_pair
    with serial.Serial(b, 115200, timeout=0.1) as sb:
        sb.write(b"boot.\n")
        with serial.Serial(a, 115200, timeout=0.05) as sa:
            out = tmp_path / "a.log"
            with open(out, "wb") as f:
                cu.capture({sa: "board_a"}, {"board_a": f}, 0.8, True, False)
    data = out.read_bytes()
    assert data.startswith(b"[+0.")
    assert data.endswith(b"boot.\n")


def test_capture_socat_two_boards(tmp_path, socat_pair):
    a, b = socat_pair
    with serial.Serial(b, 115200, timeout=0.1) as sb:
        sb.write(b"from B\n")
        with serial.Serial(a, 115200, timeout=0.05) as sa:
            outa = tmp_path / "a.log"
            outb = tmp_path / "b.log"
            with open(outa, "wb") as fa, open(outb, "wb") as fb:
                cu.capture({sa: "board_a", sb: "board_b"},
                           {"board_a": fa, "board_b": fb}, 0.8, False, False)
    assert outa.read_bytes() == b"from B\n"
    assert outb.read_bytes() == b"from B\n"
```

（注意：上面 `socat_pair` 中同一 `serial.Serial(b)` 同时被 capture 读取与测试写入是串行使用——capture 在 `with` 内才读。为保证两板测试真实，`test_capture_socat_two_boards` 中 board_a 与 board_b 用两个独立串口对象指向同一 socat 对，验证 select 多路复用不同对象都能收到数据。写入发生在 capture 开启之前，数据滞留 pty buffer 中。）

- [ ] **Step 2: 运行测试确认失败**

Run: `cd wireless/bs21/tools && python3 -m pytest test_capture_uart.py -v`
Expected: FAIL（`AttributeError: module 'capture_uart' has no attribute 'capture'`）

- [ ] **Step 3: 实现**

```python
import select


def capture(streams, outs, duration, ts, echo, reset_plan=None, delay=DEFAULT_RESET_DELAY):
    t0 = time.monotonic()
    if reset_plan:
        time.sleep(max(0.0, delay))
        for fn in reset_plan:
            fn()
    stamps = {b: LineTimestamping(t0) for b in outs}
    totals = {b: 0 for b in outs}
    end = t0 + duration
    interrupted = False
    try:
        while time.monotonic() < end:
            r, _, _ = select.select(list(streams), [], [], 0.05)
            for s in r:
                board = streams[s]
                try:
                    data = s.read(4096)
                except OSError:
                    continue
                if not data:
                    continue
                if echo:
                    sys.stdout.buffer.write(data)
                    sys.stdout.buffer.flush()
                if ts:
                    for elapsed, line in stamps[board].feed(data):
                        outs[board].write(ts_prefix(elapsed) + line)
                else:
                    outs[board].write(data)
                totals[board] += len(data)
    except KeyboardInterrupt:
        interrupted = True
    for b in outs:
        tail = stamps[b].flush()
        if ts and tail:
            outs[b].write(ts_prefix(tail[0]) + tail[1])
        outs[b].flush()
    return totals, interrupted
```

（需在文件顶部补 `import sys`。）

- [ ] **Step 4: 运行测试确认通过**

Run: `cd wireless/bs21/tools && python3 -m pytest test_capture_uart.py -v`
Expected: 14 passed

---

### Task 4: 复位逻辑 `pulse_reset` / `reset_plan` 与延迟复位集成

**Files:**
- Modify: `wireless/bs21/tools/capture_uart.py`
- Modify: `wireless/bs21/tools/test_capture_uart.py`

**Interfaces:**
- Consumes: `capture` 的 `reset_plan`/`delay` 参数
- Produces:
  - `pulse_reset(board, env) -> None`：读 `BS21_BOARD_<X>_RST_PORT`/`_PIN`，`subprocess.run(["uart-gpio", "pulse", port, "A", pin, "0", "2000"], check=True)`；缺配置抛 `RuntimeError`。
  - `reset_plan(args, env) -> list[callable]`：按 A→B 顺序，含 `--rst-a`/`--rst-b` 对应闭包。
  - `load_env(env_path) -> dict`：与 burn.py 相同的 .env 解析（默认路径项目根 `.env`）。

- [ ] **Step 1: 写失败测试**

```python
import subprocess

import capture_uart as cu

ENV_FULL = {
    "BS21_BOARD_A_PORT": "/dev/ttyA",
    "BS21_BOARD_A_RST_PORT": "/dev/ttyA_rst",
    "BS21_BOARD_A_RST_PIN": "8",
    "BS21_BOARD_B_PORT": "/dev/ttyB",
    "BS21_BOARD_B_RST_PORT": "/dev/ttyB_rst",
    "BS21_BOARD_B_RST_PIN": "11",
}


def test_reset_plan_order_and_args(monkeypatch):
    calls = []
    monkeypatch.setattr(cu, "pulse_reset",
                        lambda board, env: calls.append((board, env)))
    args = cu.parse_args(["--board-a", "--board-b", "--rst-a", "--rst-b"])
    plan = cu.reset_plan(args, ENV_FULL)
    assert len(plan) == 2
    for fn in plan:
        fn()
    assert [c[0] for c in calls] == ["board_a", "board_b"]


def test_reset_plan_empty_without_flags():
    args = cu.parse_args(["--board-a", "--board-b"])
    assert cu.reset_plan(args, ENV_FULL) == []


def test_pulse_reset_missing_config_raises(monkeypatch):
    monkeypatch.setattr(cu.subprocess, "run", lambda *a, **k: None)
    with pytest.raises(RuntimeError):
        cu.pulse_reset("board_a", {})


def test_pulse_reset_cmd():
    seen = []
    import types
    fake = types.SimpleNamespace()
    seen.append(fake)

    def fake_run(cmd, check=True):
        seen.append(cmd)
        return fake

    monkeypatch.setattr(cu.subprocess, "run", fake_run)
    cu.pulse_reset("board_b", ENV_FULL)
    assert seen[-1] == ["uart-gpio", "pulse", "/dev/ttyB_rst", "A", "11", "0", "2000"]


def test_load_env_reads_pairs(tmp_path):
    (tmp_path / ".env").write_text(
        "# comment\nBS21_BOARD_A_PORT=/dev/ttyA\nEMPTY=\n\nUNUSED=x\n")
    env = cu.load_env(str(tmp_path / ".env"))
    assert env["BS21_BOARD_A_PORT"] == "/dev/ttyA"
    assert env.get("EMPTY") == ""
    assert env.get("UNUSED") == "x"
```

（Step 1 中 `test_pulse_reset_cmd` 使用 `monkeypatch` fixture，需确认文件顶部已 `import pytest`。）

- [ ] **Step 2: 运行测试确认失败**

Run: `cd wireless/bs21/tools && python3 -m pytest test_capture_uart.py -v`
Expected: FAIL（`AttributeError: module 'capture_uart' has no attribute 'pulse_reset'`）

- [ ] **Step 3: 实现**

```python
import subprocess


def load_env(env_path):
    env = {}
    with open(env_path, encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#") or "=" not in line:
                continue
            k, v = line.split("=", 1)
            env[k.strip()] = v.strip()
    return env


def pulse_reset(board, env):
    key = board.upper()
    port = env.get("BS21_BOARD_%s_RST_PORT" % key)
    pin = env.get("BS21_BOARD_%s_RST_PIN" % key)
    if not (port and pin):
        raise RuntimeError("missing BS21_BOARD_%s_RST_PORT/PIN in .env" % key)
    subprocess.run(["uart-gpio", "pulse", port, "A", pin, "0", str(RESET_PULSE_MS)],
                   check=True)


def reset_plan(args, env):
    plan = []
    for board, flag in (("board_a", args.rst_a), ("board_b", args.rst_b)):
        if flag:
            plan.append(lambda board=board: pulse_reset(board, env))
    return plan
```

- [ ] **Step 4: 运行测试确认通过**

Run: `cd wireless/bs21/tools && python3 -m pytest test_capture_uart.py -v`
Expected: 19 passed

---

### Task 5: `main()` 组装 + 端到端集成测试 + 文档同步

**Files:**
- Modify: `wireless/bs21/tools/capture_uart.py`
- Modify: `wireless/bs21/tools/test_capture_uart.py`
- Modify: `AGENTS.md`（手动抓 log 段落改为引用本脚本）

**Interfaces:**
- Consumes: 上述全部符号；`open_streams(ports) -> dict[serial, board]`（本任务新增）
- Produces:
  - `open_streams(ports) -> dict[serial, board]`：逐个 `serial.Serial(port, BAUD, timeout=0.05)`，失败打印 `[WARN] <board> open failed ...` 到 stderr 并跳过。
  - `main(argv=None, env=None) -> int`：返回 0（成功）/ 1（串口全开失败）/ 130（Ctrl+C）/ 2（参数错误，argparse 内部）。

- [ ] **Step 1: 写失败测试**

```python
def test_open_streams_skips_failures(tmp_path):
    ports = {"board_a": str(tmp_path / "nope"), "board_b": "/dev/null"}
    streams = cu.open_streams(ports)
    assert "board_a" not in [b for b in streams.values()]


def test_main_end_to_end(tmp_path, socat_pair, capsys):
    a, b = socat_pair
    env = dict(ENV_FULL)
    env["BS21_BOARD_A_PORT"] = a
    rc = cu.main(["--board-a", "--duration", "1", "--odir", str(tmp_path), "--no-echo"],
                 env=env)
    assert rc == 0
    files = [str(p) for p in tmp_path.iterdir() if p.name.endswith(".log")]
    assert len(files) == 1


def test_main_missing_env_var(tmp_path, capsys):
    rc = cu.main(["--board-a", "--odir", str(tmp_path)], env={})
    assert rc == 1
    assert "BS21_BOARD_A_PORT" in capsys.readouterr().out
```

- [ ] **Step 2: 运行测试确认失败**

Run: `cd wireless/bs21/tools && python3 -m pytest test_capture_uart.py -v`
Expected: FAIL（`AttributeError: module 'capture_uart' has no attribute 'main'`）

- [ ] **Step 3: 实现**

```python
import sys

from datetime import datetime


def open_streams(ports):
    streams = {}
    for board, port in ports.items():
        try:
            s = serial.Serial(port, BAUD, timeout=0.05)
            s.reset_input_buffer()
            streams[s] = board
        except Exception as e:
            print("[WARN] %s open failed at %s: %s" % (board, port, e), file=sys.stderr)
    return streams


def project_root():
    return os.path.dirname(os.path.dirname(os.path.dirname(
        os.path.dirname(os.path.abspath(__file__)))))


def main(argv=None, env=None):
    args = parse_args(argv)
    if env is None:
        env_path = os.path.join(project_root(), ".env")
        if not os.path.isfile(env_path):
            print("[ERROR] .env not found at project root; see .env.example")
            return 1
        env = load_env(env_path)
    boards = [b for b in ("board_a", "board_b") if getattr(args, b)]
    ports = {}
    for b in boards:
        port = env.get("BS21_BOARD_%s_PORT" % b.upper())
        if not port:
            print("[ERROR] missing BS21_BOARD_%s_PORT in .env" % b.upper())
            return 1
        ports[b] = port
    streams = open_streams(ports)
    if not streams:
        print("[ERROR] no board serial port opened")
        return 1
    os.makedirs(args.odir, exist_ok=True)
    start = datetime.now()
    outs = {}
    for s, b in streams.items():
        path = make_output_path(args.odir, b, start)
        outs[b] = open(path, "wb")
        print("[capture] %s -> %s" % (b, path))
    try:
        totals, interrupted = capture(streams, outs, args.duration, args.ts,
                                      not args.no_echo,
                                      reset_plan(args, env), args.reset_delay)
    finally:
        for f in outs.values():
            f.close()
        for s in streams:
            s.close()
    for b, total in totals.items():
        print("[capture] %s: %d bytes in %ds" % (b, total, args.duration))
    return 130 if interrupted else 0


if __name__ == "__main__":
    sys.exit(main())
```

（顶部 import 需合并为：`import argparse, os, select, serial, subprocess, sys, time` 与 `from datetime import datetime`。`project_root()` 相对路径：`tools/` → `bs21/` → `wireless/` → 项目根。）

- [ ] **Step 4: 运行全量测试确认通过**

Run: `cd wireless/bs21/tools && python3 -m pytest test_capture_uart.py -v`
Expected: 22 passed

- [ ] **Step 5: 真实板冒烟（主工作区执行）**

在主工作区（有 `.env`）连接 board_a 模块串口，执行：

```bash
python3 wireless/bs21/tools/capture_uart.py --board-a --rst-a --duration 15 --odir /tmp --ts
```

Expected: 终端回显一轮从 `boot.` → `Flashboot Init!` → `app:` 的启动日志；15 秒后退出码 0，`/tmp/board_a_*.log` 每行带 `[+秒.毫秒]` 前缀。

- [ ] **Step 6: 更新 AGENTS.md**

将 AGENTS.md 中「抓取从 reset 起的完整 log」手动流程（`stty`+`cat`+`uart-gpio pulse` 段落）替换为对 `capture_uart.py` 的引用与用法示例：

```markdown
抓取从 reset 起的完整 log（推荐用脚本，自动连串口+延迟复位+落盘+时间戳）：
python3 wireless/bs21/tools/capture_uart.py --board-a --rst-a --duration 60 --odir /tmp --ts
# board_a/board_b 可选，至少选一个；--rst-a/--rst-b 对已选板复位；Ctrl+C 优雅保存
```

- [ ] **Step 7: 运行 code-simplifier 并复查测试**

Run code-simplifier 技能对 `capture_uart.py` 做简化，然后：

Run: `cd wireless/bs21/tools && python3 -m pytest test_capture_uart.py -v`
Expected: 22 passed（简化不破坏功能）

---

## Self-Review 记录

**1. Spec coverage：**
- CLI 全参数 → Task 1（parse_args）+ Task 5（main）
- 至少选一块板 / 复位仅对已选板 → Task 1 校验
- 延迟复位 A→B → Task 4 + capture 内 delay
- raw + 可选时间戳行前缀 → Task 2/3
- duration 到 exit 0 / Ctrl+C exit 130 → Task 3 capture 返回 + Task 5 main 返回
- 串口打开失败跳过 → Task 5 open_streams
- 文件命名 → Task 1 make_output_path
- 测试（socat 虚拟对 + 真实板冒烟）→ Task 3/5

**2. Placeholder scan：** 无 TBD/TODO；所有 step 含实际代码。

**3. Type consistency：** `capture` 签名在 Task 3 定义，Task 5 main 调用一致；`reset_plan` 返回 list[callable]，Task 3 执行一致；`LineTimestamping.feed/flush` 返回类型一致；`make_output_path` 在 Task 1 定义、Task 5 使用一致。