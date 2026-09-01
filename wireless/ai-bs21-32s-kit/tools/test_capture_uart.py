import datetime
import os
import subprocess
import time
import uuid

import pytest
import serial

import capture_uart as cu

ENV_FULL = {
    "BOARD_A_PORT": "/dev/ttyA",
    "BOARD_A_RST_PORT": "/dev/ttyA_rst",
    "BOARD_A_RST_PIN": "8",
    "BOARD_B_PORT": "/dev/ttyB",
    "BOARD_B_RST_PORT": "/dev/ttyB_rst",
    "BOARD_B_RST_PIN": "11",
}


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


def _make_socat_pair(tmp_path):
    tag = uuid.uuid4().hex[:8]
    a = str(tmp_path / ("tty_a_" + tag))
    b = str(tmp_path / ("tty_b_" + tag))
    proc = subprocess.Popen(["socat", "pty,raw,echo=0,link=%s" % a,
                             "pty,raw,echo=0,link=%s" % b])
    for _ in range(50):
        if os.path.exists(a) and os.path.exists(b):
            break
        time.sleep(0.05)
    return a, b, proc


@pytest.fixture
def socat_pair(tmp_path):
    a, b, proc = _make_socat_pair(tmp_path)
    try:
        yield a, b
    finally:
        proc.terminate()
        proc.wait()


def test_capture_socat_raw_duration_exit(tmp_path, socat_pair):
    a, b = socat_pair
    with serial.Serial(a, 115200, timeout=0.05) as sa, \
         serial.Serial(b, 115200, timeout=0.1) as sb:
        sb.write(b"hello world\n")
        out = tmp_path / "a.log"
        with open(out, "wb") as f:
            totals, interrupted = cu.capture({sa: "board_a"}, {"board_a": f},
                                             0.8, False, False)
    assert not interrupted
    assert out.read_bytes() == b"hello world\n"
    assert totals["board_a"] == len(b"hello world\n")


def test_capture_socat_ts_prefixes(tmp_path, socat_pair):
    a, b = socat_pair
    with serial.Serial(a, 115200, timeout=0.05) as sa, \
         serial.Serial(b, 115200, timeout=0.1) as sb:
        sb.write(b"boot.\n")
        out = tmp_path / "a.log"
        with open(out, "wb") as f:
            cu.capture({sa: "board_a"}, {"board_a": f}, 0.8, True, False)
    data = out.read_bytes()
    assert data.startswith(b"[+0.")
    assert data.endswith(b"boot.\n")


def test_capture_socat_ts_flush_partial_line(tmp_path, socat_pair):
    a, b = socat_pair
    with serial.Serial(a, 115200, timeout=0.05) as sa, \
         serial.Serial(b, 115200, timeout=0.1) as sb:
        sb.write(b"half line")
        out = tmp_path / "a.log"
        with open(out, "wb") as f:
            cu.capture({sa: "board_a"}, {"board_a": f}, 0.8, True, False)
    data = out.read_bytes()
    assert data.startswith(b"[+0.")
    assert data.endswith(b"half line")


def test_capture_socat_two_boards(tmp_path):
    a1, b1, p1 = _make_socat_pair(tmp_path)
    a2, b2, p2 = _make_socat_pair(tmp_path)
    try:
        with serial.Serial(a1, 115200, timeout=0.05) as sa1, \
             serial.Serial(b1, 115200, timeout=0.1) as sb1, \
             serial.Serial(a2, 115200, timeout=0.05) as sa2, \
             serial.Serial(b2, 115200, timeout=0.1) as sb2:
            sb1.write(b"from B\n")
            sb2.write(b"from B\n")
            outa = tmp_path / "a.log"
            outb = tmp_path / "b.log"
            with open(outa, "wb") as fa, open(outb, "wb") as fb:
                cu.capture({sa1: "board_a", sa2: "board_b"},
                           {"board_a": fa, "board_b": fb}, 0.8, False, False)
        assert outa.read_bytes() == b"from B\n"
        assert outb.read_bytes() == b"from B\n"
    finally:
        p1.terminate()
        p1.wait()
        p2.terminate()
        p2.wait()


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
    ts = cu.LineTimestamping(time.monotonic())
    assert ts.feed(b"boot.") == []
    assert ts.feed(b"\nrest") == [(pytest.approx(0.0, abs=0.1), b"boot.\n")]
    tail = ts.flush()
    assert tail is not None
    assert tail[1] == b"rest"
    assert ts.flush() is None


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


def test_pulse_reset_cmd(monkeypatch):
    seen = []

    def fake_run(cmd, check=True):
        seen.append(cmd)

    monkeypatch.setattr(cu.subprocess, "run", fake_run)
    cu.pulse_reset("board_b", ENV_FULL)
    assert seen == [["uart-gpio", "pulse", "/dev/ttyB_rst", "A", "11", "0", "2000"]]


def test_load_env_reads_pairs(tmp_path):
    (tmp_path / ".env").write_text(
        "# comment\nBOARD_A_PORT=/dev/ttyA\nEMPTY=\n\nUNUSED=x\n")
    env = cu.load_env(str(tmp_path / ".env"))
    assert env["BOARD_A_PORT"] == "/dev/ttyA"
    assert env.get("EMPTY") == ""
    assert env.get("UNUSED") == "x"


def test_open_streams_skips_failures(tmp_path):
    ports = {"board_a": str(tmp_path / "nope"), "board_b": "/dev/null"}
    streams = cu.open_streams(ports)
    assert "board_a" not in [b for b in streams.values()]


def test_main_end_to_end(tmp_path, capsys):
    a, b, proc = _make_socat_pair(tmp_path)
    try:
        env = dict(ENV_FULL)
        env["BOARD_A_PORT"] = a
        rc = cu.main(["--board-a", "--duration", "1", "--odir", str(tmp_path),
                      "--no-echo"], env=env)
        assert rc == 0
        files = [str(p) for p in tmp_path.iterdir() if p.name.endswith(".log")]
        assert len(files) == 1
    finally:
        proc.terminate()
        proc.wait()


def test_main_missing_env_var(tmp_path, capsys):
    rc = cu.main(["--board-a", "--odir", str(tmp_path)], env={})
    assert rc == 1
    assert "BOARD_A_PORT" in capsys.readouterr().out
