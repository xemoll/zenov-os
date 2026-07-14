#!/usr/bin/env python3
"""Deterministic stage0 compiler for the first ZenovOS bare-metal slice.

The compiler intentionally supports a small, explicit subset of Zenov. It turns
`fn main()` calls into a 16-bit BIOS kernel while the production Zenov native
backend is being extended with a freestanding x86_64 target.
"""
from __future__ import annotations

import argparse
import dataclasses
import hashlib
import pathlib
import re
import sys
from typing import Iterable, Sequence


class CompileError(Exception):
    pass


@dataclasses.dataclass(frozen=True)
class Token:
    kind: str
    value: str
    line: int
    column: int


@dataclasses.dataclass(frozen=True)
class Call:
    name: str
    args: tuple[str | int, ...]
    line: int


@dataclasses.dataclass
class Program:
    instructions: list[Call]
    prompt: str = "zenov> "
    commands: list[tuple[str, str, int]] = dataclasses.field(default_factory=list)


_TOKEN_RE = re.compile(
    r"(?P<WS>[ \t\r\n]+)|"
    r"(?P<LINE_COMMENT>//[^\n]*|#[^\n]*)|"
    r"(?P<STRING>\"(?:\\.|[^\"\\])*\")|"
    r"(?P<NUMBER>0[xX][0-9A-Fa-f]+|[0-9]+)|"
    r"(?P<IDENT>[A-Za-z_][A-Za-z0-9_]*)|"
    r"(?P<PUNCT>[(){},;])|"
    r"(?P<MISMATCH>.)"
)


def tokenize(source: str) -> list[Token]:
    tokens: list[Token] = []
    line = 1
    column = 1
    for match in _TOKEN_RE.finditer(source):
        kind = match.lastgroup or "MISMATCH"
        raw = match.group(0)
        start_line, start_column = line, column
        line_breaks = raw.count("\n")
        if line_breaks:
            line += line_breaks
            column = len(raw.rsplit("\n", 1)[-1]) + 1
        else:
            column += len(raw)
        if kind in {"WS", "LINE_COMMENT"}:
            continue
        if kind == "MISMATCH":
            raise CompileError(
                f"{start_line}:{start_column}: unexpected character {raw!r}"
            )
        tokens.append(Token(kind, raw, start_line, start_column))
    tokens.append(Token("EOF", "", line, column))
    return tokens


def decode_string(token: Token) -> str:
    assert token.kind == "STRING"
    body = token.value[1:-1]
    try:
        decoded = bytes(body, "utf-8").decode("unicode_escape")
    except UnicodeDecodeError as exc:
        raise CompileError(f"{token.line}:{token.column}: invalid string escape") from exc
    try:
        decoded.encode("ascii")
    except UnicodeEncodeError as exc:
        raise CompileError(
            f"{token.line}:{token.column}: stage0 BIOS profile accepts ASCII strings only"
        ) from exc
    return decoded


class Parser:
    def __init__(self, tokens: Sequence[Token]):
        self.tokens = tokens
        self.index = 0

    @property
    def current(self) -> Token:
        return self.tokens[self.index]

    def advance(self) -> Token:
        token = self.current
        self.index += 1
        return token

    def expect(self, kind: str, value: str | None = None) -> Token:
        token = self.current
        if token.kind != kind or (value is not None and token.value != value):
            wanted = value if value is not None else kind
            raise CompileError(
                f"{token.line}:{token.column}: expected {wanted!r}, got {token.value!r}"
            )
        self.index += 1
        return token

    def parse(self) -> list[Call]:
        self.expect("IDENT", "fn")
        self.expect("IDENT", "main")
        self.expect("PUNCT", "(")
        self.expect("PUNCT", ")")
        self.expect("PUNCT", "{")
        calls: list[Call] = []
        while not (self.current.kind == "PUNCT" and self.current.value == "}"):
            if self.current.kind == "EOF":
                raise CompileError(
                    f"{self.current.line}:{self.current.column}: unterminated main function"
                )
            if self.current.kind == "IDENT" and self.current.value == "say":
                line = self.advance().line
                value = decode_string(self.expect("STRING"))
                self.expect("PUNCT", ";")
                calls.append(Call("console_println", (value,), line))
                continue
            calls.append(self.parse_call())
        self.expect("PUNCT", "}")
        self.expect("EOF")
        return calls

    def parse_call(self) -> Call:
        name = self.expect("IDENT")
        self.expect("PUNCT", "(")
        args: list[str | int] = []
        if not (self.current.kind == "PUNCT" and self.current.value == ")"):
            while True:
                token = self.current
                if token.kind == "STRING":
                    args.append(decode_string(self.advance()))
                elif token.kind == "NUMBER":
                    args.append(int(self.advance().value, 0))
                else:
                    raise CompileError(
                        f"{token.line}:{token.column}: arguments must be string or integer literals"
                    )
                if self.current.kind == "PUNCT" and self.current.value == ",":
                    self.advance()
                    continue
                break
        self.expect("PUNCT", ")")
        self.expect("PUNCT", ";")
        return Call(name.value, tuple(args), name.line)


_SIGNATURES: dict[str, tuple[type, ...]] = {
    "console_clear": (),
    "console_set_color": (int,),
    "console_print": (str,),
    "console_println": (str,),
    "shell_prompt": (str,),
    "shell_command": (str, str),
    "shell_builtin": (str, int),
    "shell_run": (),
    "halt": (),
}

_BUILTIN_IDS = {1, 2, 3}


def validate(calls: Iterable[Call]) -> Program:
    program = Program(instructions=[])
    seen_commands: set[str] = set()
    shell_runs = 0
    for call in calls:
        signature = _SIGNATURES.get(call.name)
        if signature is None:
            raise CompileError(f"line {call.line}: unsupported bare-metal call {call.name}()")
        if len(call.args) != len(signature):
            raise CompileError(
                f"line {call.line}: {call.name} expects {len(signature)} arguments, got {len(call.args)}"
            )
        for index, (arg, expected) in enumerate(zip(call.args, signature), start=1):
            if not isinstance(arg, expected):
                raise CompileError(
                    f"line {call.line}: argument {index} of {call.name} must be {expected.__name__}"
                )
        if call.name == "console_set_color":
            color = int(call.args[0])
            if not 0 <= color <= 0xFF:
                raise CompileError(f"line {call.line}: VGA color must be in 0..255")
        elif call.name == "shell_prompt":
            prompt = str(call.args[0])
            if not prompt or len(prompt) > 32:
                raise CompileError(f"line {call.line}: shell prompt length must be 1..32")
            program.prompt = prompt
        elif call.name in {"shell_command", "shell_builtin"}:
            name = str(call.args[0])
            if not re.fullmatch(r"[a-z][a-z0-9_-]{0,14}", name):
                raise CompileError(
                    f"line {call.line}: command name must match [a-z][a-z0-9_-]{{0,14}}"
                )
            if name in seen_commands:
                raise CompileError(f"line {call.line}: duplicate shell command {name!r}")
            seen_commands.add(name)
            if call.name == "shell_command":
                response = str(call.args[1])
                if len(response) > 255:
                    raise CompileError(f"line {call.line}: response exceeds 255 characters")
                program.commands.append((name, response, 0))
            else:
                builtin_id = int(call.args[1])
                if builtin_id not in _BUILTIN_IDS:
                    raise CompileError(
                        f"line {call.line}: shell builtin id must be 1(clear), 2(reboot), or 3(halt)"
                    )
                program.commands.append((name, "", builtin_id))
        elif call.name == "shell_run":
            shell_runs += 1
            program.instructions.append(call)
        else:
            program.instructions.append(call)
    if shell_runs != 1:
        raise CompileError(f"main must call shell_run() exactly once; found {shell_runs}")
    if not program.commands:
        raise CompileError("at least one shell_command() or shell_builtin() is required")
    return program


def asm_bytes(text: str) -> str:
    data = text.encode("ascii") + b"\0"
    return ", ".join(f"0x{byte:02x}" for byte in data)


def compile_program(program: Program, source_hash: str, source_name: str) -> str:
    string_labels: dict[str, str] = {}

    def intern(value: str, prefix: str = "str") -> str:
        if value not in string_labels:
            string_labels[value] = f"{prefix}_{len(string_labels):04d}"
        return string_labels[value]

    prompt_label = intern(program.prompt, "prompt")
    code: list[str] = []
    for call in program.instructions:
        if call.name == "console_clear":
            code.append("    call clear_screen")
        elif call.name == "console_set_color":
            code.append(f"    mov byte ptr [current_color], 0x{int(call.args[0]):02x}")
        elif call.name in {"console_print", "console_println"}:
            label = intern(str(call.args[0]))
            code.extend([f"    mov si, OFFSET FLAT:{label}", "    call print_string"])
            if call.name == "console_println":
                code.append("    call print_newline")
        elif call.name == "shell_run":
            code.extend([
                f"    mov ax, OFFSET FLAT:{prompt_label}",
                "    mov word ptr [shell_prompt_ptr], ax",
                "    call shell_main",
            ])
        elif call.name == "halt":
            code.append("    call halt_cpu")

    command_rows: list[str] = []
    for index, (name, response, builtin_id) in enumerate(program.commands):
        name_label = intern(name, f"cmd_name_{index}")
        response_label = "0" if builtin_id else intern(response, f"cmd_response_{index}")
        command_rows.append(
            f"    .word {name_label}, {response_label}\n    .byte {builtin_id}"
        )
    command_rows.append("    .word 0, 0\n    .byte 0")

    data_rows = [f"{label}: .byte {asm_bytes(value)}" for value, label in string_labels.items()]

    return f"""/* Generated by Zenov bare-metal stage0 compiler.
 * source={source_name}
 * sha256={source_hash}
 */
.intel_syntax noprefix
.code16
.section .text
.global _start

_start:
    cli
    xor ax, ax
    mov ss, ax
    mov sp, 0xfffe
    push cs
    pop ds
    push cs
    pop es
    sti
    call serial_init
    mov si, OFFSET FLAT:boot_marker
    call serial_print_string
{chr(10).join(code)}
    call halt_cpu

.include "runtime/kernel_runtime.inc"

boot_marker: .byte 0x5a, 0x45, 0x4e, 0x4f, 0x56, 0x4f, 0x53, 0x5f, 0x42, 0x4f, 0x4f, 0x54, 0x5f, 0x4f, 0x4b, 0x0d, 0x0a, 0

command_table:
{chr(10).join(command_rows)}

{chr(10).join(data_rows)}
"""


def build(input_path: pathlib.Path, output_path: pathlib.Path) -> None:
    source = input_path.read_text(encoding="utf-8")
    digest = hashlib.sha256(source.encode("utf-8")).hexdigest()
    calls = Parser(tokenize(source)).parse()
    program = validate(calls)
    assembly = compile_program(program, digest, input_path.as_posix())
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(assembly, encoding="utf-8", newline="\n")


def parse_args(argv: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Compile the first deterministic Zenov bare-metal subset to GNU as"
    )
    parser.add_argument("input", type=pathlib.Path, help="Zenov .zv kernel source")
    parser.add_argument("-o", "--output", required=True, type=pathlib.Path)
    return parser.parse_args(argv)


def main(argv: Sequence[str] | None = None) -> int:
    args = parse_args(sys.argv[1:] if argv is None else argv)
    try:
        build(args.input, args.output)
    except (OSError, CompileError) as exc:
        print(f"zenov-baremetal: error: {exc}", file=sys.stderr)
        return 1
    print(f"zenov-baremetal: wrote {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
