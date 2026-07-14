#!/usr/bin/env python3
from __future__ import annotations

import hashlib
import importlib.util
import pathlib
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
COMPILER = ROOT / "tools" / "zenov_baremetal_bootstrap.py"
spec = importlib.util.spec_from_file_location("zenov_baremetal_bootstrap", COMPILER)
assert spec and spec.loader
module = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = module
spec.loader.exec_module(module)


def compile_text(source: str) -> str:
    calls = module.Parser(module.tokenize(source)).parse()
    program = module.validate(calls)
    return module.compile_program(program, hashlib.sha256(source.encode()).hexdigest(), "test.zv")


def expect_error(source: str, needle: str) -> None:
    try:
        compile_text(source)
    except module.CompileError as exc:
        assert needle in str(exc), (needle, str(exc))
    else:
        raise AssertionError(f"expected CompileError containing {needle!r}")


def main() -> int:
    source = '''
fn main() {
    console_clear();
    say "ZenovOS";
    shell_command("help", "Commands");
    shell_builtin("halt", 3);
    shell_run();
}
'''
    first = compile_text(source)
    second = compile_text(source)
    assert first == second
    assert "boot_marker:" in first
    assert "command_table:" in first
    assert "call shell_main" in first

    with tempfile.TemporaryDirectory() as tmp:
        src = pathlib.Path(tmp) / "kernel.zv"
        out = pathlib.Path(tmp) / "kernel.asm"
        src.write_text(source, encoding="utf-8")
        module.build(src, out)
        assert out.read_text(encoding="utf-8") == first.replace("source=test.zv", f"source={src.as_posix()}")

    expect_error('fn main(){ shell_run(); }', "at least one")
    expect_error('fn main(){ shell_command("help", "a"); shell_command("help", "b"); shell_run(); }', "duplicate")
    expect_error('fn main(){ unknown(); shell_command("help", "a"); shell_run(); }', "unsupported")
    expect_error('fn main(){ say "Привет"; shell_command("help", "a"); shell_run(); }', "ASCII")
    expect_error('fn main(){ shell_builtin("halt", 9); shell_run(); }', "builtin id")
    print("stage0 compiler tests: OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
