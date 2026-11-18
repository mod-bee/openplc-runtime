# SPDX-License-Identifier: GPL-3.0-only
# Copyright (c) 2026 Autonomy®

"""The FC 0x4F feature check: ``STRUCPP_SHIM_HAS_DEBUG_TEXT``.

``strucpp_debug_read_text`` exists only in STruC++ headers that define
``STRUCPP_SHIM_HAS_DEBUG_TEXT``. The shim (``runtime_v4_entry.cpp``) is compiled
against the headers inside each upload, so:

1. every use of the new names in the shim stays behind that macro, or an
   upload from an older editor stops building;
2. behind it, the shim holds STruC++'s chunk struct and function signature to
   the runtime's C mirror (``core/src/plc_app/debug_text.h``): a header with
   the contract's shape compiles, a different layout does not;
3. the runtime looks the symbol up as optional, so a program without it still
   loads and FC 0x4F answers 0x83 (tests/host/test_debug_text.c).
"""

import re
import shutil
import subprocess
from pathlib import Path

import pytest

_REPO = Path(__file__).resolve().parents[3]
_SHIM = _REPO / "core" / "strucpp_runtime" / "runtime_v4_entry.cpp"
_IMAGE_TABLES = _REPO / "core" / "src" / "plc_app" / "image_tables.cpp"
_MACRO = "STRUCPP_SHIM_HAS_DEBUG_TEXT"
_NAMES = ("strucpp_debug_read_text", "strucpp_text_chunk")

_CXX = shutil.which("g++") or shutil.which("c++") or shutil.which("clang++")

# Interface 1 of the contract, as a debug_dispatch.hpp stand-in. `cap_type`
# lets a test break the layout.
_CONTRACT_HEADER = """
#pragma once
#include <cstdint>
namespace strucpp {{ namespace debug {{
enum TypeTag : uint8_t {{ TAG_STRING = 19, TAG_WSTRING = 20 }};
}} }}
extern "C" {{
typedef struct {{
    uint8_t  status;
    uint8_t  tag;
    uint8_t  flags;
    {cap_type} cap;
    uint16_t len;
    uint16_t count;
}} strucpp_text_chunk;
uint8_t strucpp_debug_read_text(uint8_t arr, uint16_t elem, uint16_t offset, uint16_t max,
                                uint8_t* data, uint16_t data_cap, strucpp_text_chunk* info);
}}
#define {macro} 1
"""

_OLD_HEADER = """
#pragma once
#include <cstdint>
namespace strucpp { namespace debug {
enum TypeTag : uint8_t { TAG_STRING = 19, TAG_WSTRING = 20 };
} }
"""


def _gate_block(source: str) -> str:
    """The shim's `#ifdef STRUCPP_SHIM_HAS_DEBUG_TEXT` ... `#endif` block."""
    lines = source.splitlines()
    out: list[str] = []
    depth = 0
    for line in lines:
        stripped = line.strip()
        if depth == 0:
            if re.match(r"#\s*ifdef\s+" + _MACRO + r"\b", stripped):
                depth = 1
                out.append(line)
            continue
        out.append(line)
        if re.match(r"#\s*if", stripped):
            depth += 1
        elif re.match(r"#\s*endif", stripped):
            depth -= 1
            if depth == 0:
                break
    assert out and depth == 0, f"no complete #ifdef {_MACRO} block in the shim"
    return "\n".join(out)


def _gated_lines(source: str) -> set[int]:
    covered: set[int] = set()
    depth = 0
    for lineno, line in enumerate(source.splitlines(), start=1):
        stripped = line.strip()
        if depth == 0:
            if re.match(r"#\s*ifdef\s+" + _MACRO + r"\b", stripped):
                depth = 1
                covered.add(lineno)
            continue
        covered.add(lineno)
        if re.match(r"#\s*if", stripped):
            depth += 1
        elif re.match(r"#\s*endif", stripped):
            depth -= 1
    return covered


def _compile(tmp_path: Path, header: str) -> subprocess.CompletedProcess:
    (tmp_path / "debug_dispatch.hpp").write_text(header, encoding="utf-8")
    tu = tmp_path / "gate.cpp"
    tu.write_text(
        '#include "debug_dispatch.hpp"\n#include <cstddef>\n'
        + _gate_block(_SHIM.read_text(encoding="utf-8"))
        + "\n",
        encoding="utf-8",
    )
    # The block includes "../src/plc_app/debug_text.h" relative to the shim.
    return subprocess.run(
        [_CXX, "-std=c++17", "-fsyntax-only", "-I", str(tmp_path),
         "-I", str(_SHIM.parent), str(tu)],
        capture_output=True,
        text=True,
        check=False,
    )


@pytest.mark.parametrize("name", _NAMES)
def test_every_use_in_the_shim_is_behind_the_gate(name):
    source = _SHIM.read_text(encoding="utf-8")
    covered = _gated_lines(source)
    assert covered, f"the shim has no #ifdef {_MACRO} block"
    for lineno, line in enumerate(source.splitlines(), start=1):
        code = line.split("//", 1)[0]
        if name in code:
            assert lineno in covered, (
                f"{_SHIM.name}:{lineno} uses {name} outside #ifdef {_MACRO}: "
                "an upload from an older editor would not build"
            )


@pytest.mark.skipif(_CXX is None, reason="no C++ compiler on PATH")
def test_the_contract_header_compiles(tmp_path):
    result = _compile(tmp_path, _CONTRACT_HEADER.format(cap_type="uint16_t", macro=_MACRO))
    assert result.returncode == 0, result.stderr


@pytest.mark.skipif(_CXX is None, reason="no C++ compiler on PATH")
def test_a_different_chunk_layout_is_refused(tmp_path):
    result = _compile(tmp_path, _CONTRACT_HEADER.format(cap_type="uint8_t", macro=_MACRO))
    assert result.returncode != 0
    assert "offset" in result.stderr or "size" in result.stderr


@pytest.mark.skipif(_CXX is None, reason="no C++ compiler on PATH")
def test_older_headers_skip_the_block(tmp_path):
    result = _compile(tmp_path, _OLD_HEADER)
    assert result.returncode == 0, result.stderr


def test_the_runtime_resolves_the_symbol_as_optional():
    source = _IMAGE_TABLES.read_text(encoding="utf-8")
    assert re.search(r'resolve\(pm,\s*"strucpp_debug_read_text",\s*false\)', source), (
        "strucpp_debug_read_text must be optional: a program built before it still loads"
    )
