# SPDX-License-Identifier: GPL-3.0-only
# Copyright (c) 2026 Autonomy®

"""The Python mirror of plugin_runtime_args_t matches the C struct.

Python plugins read the struct through ctypes (shared/plugin_runtime_args.py),
so a field appended in C without the same field in Python, or with another
width, moves every later offset and a plugin calls through a wrong pointer.
This compiles plugin_types.h and compares sizeof and each appended field's
offset with ctypes, and the chunk struct of debug_read_text with its mirror.
"""

import ctypes
import shutil
import subprocess
import sys
from pathlib import Path

import pytest

_REPO = Path(__file__).resolve().parents[3]
_SRC = _REPO / "core" / "src"
sys.path.insert(0, str(_SRC / "drivers" / "plugins" / "python"))

from shared.plugin_runtime_args import PlcTextChunk, PluginRuntimeArgs  # noqa: E402

_CC = shutil.which("cc") or shutil.which("gcc") or shutil.which("clang")

_FIELDS = ("debug_write", "get_plc_state", "debug_read_text", "debug_write_text")

_PROGRAM = """
#include <stddef.h>
#include <stdio.h>
#include "drivers/plugin_types.h"
int main(void) {
    printf("size %zu\\n", sizeof(plugin_runtime_args_t));
    printf("debug_write %zu\\n", offsetof(plugin_runtime_args_t, debug_write));
    printf("get_plc_state %zu\\n", offsetof(plugin_runtime_args_t, get_plc_state));
    printf("debug_read_text %zu\\n", offsetof(plugin_runtime_args_t, debug_read_text));
    printf("debug_write_text %zu\\n", offsetof(plugin_runtime_args_t, debug_write_text));
    printf("chunk %zu %zu %zu %zu\\n", sizeof(plc_text_chunk_t), offsetof(plc_text_chunk_t, cap),
           offsetof(plc_text_chunk_t, len), offsetof(plc_text_chunk_t, count));
    return 0;
}
"""


@pytest.mark.skipif(_CC is None, reason="no C compiler on PATH")
def test_python_mirror_matches_the_c_struct(tmp_path):
    src = tmp_path / "layout.c"
    src.write_text(_PROGRAM, encoding="utf-8")
    exe = tmp_path / "layout"
    build = subprocess.run(
        [_CC, "-std=gnu11", "-I", str(_SRC), "-I", str(_SRC / "plc_app"), str(src), "-o", str(exe)],
        capture_output=True,
        text=True,
        check=False,
    )
    assert build.returncode == 0, build.stderr
    out = subprocess.run([str(exe)], capture_output=True, text=True, check=True).stdout
    c = {line.split()[0]: line.split()[1:] for line in out.splitlines()}

    assert int(c["size"][0]) == ctypes.sizeof(PluginRuntimeArgs)
    for name in _FIELDS:
        assert int(c[name][0]) == getattr(PluginRuntimeArgs, name).offset, name
    size, cap, length, count = (int(v) for v in c["chunk"])
    assert size == ctypes.sizeof(PlcTextChunk)
    assert (cap, length, count) == (PlcTextChunk.cap.offset, PlcTextChunk.len.offset,
                                    PlcTextChunk.count.offset)
