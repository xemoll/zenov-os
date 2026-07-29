#!/usr/bin/env python3
from __future__ import annotations

from pathlib import Path
import re
import subprocess

ROOT = Path.cwd()


def load(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def save(path: str, text: str) -> None:
    (ROOT / path).write_text(text, encoding="utf-8")


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{label}: expected one match, found {count}")
    return text.replace(old, new, 1)


def function_span(text: str, marker: str) -> tuple[int, int]:
    start = text.find(marker)
    if start < 0:
        raise RuntimeError(f"function marker not found: {marker}")
    brace = text.find("{", start)
    if brace < 0:
        raise RuntimeError(f"function opening brace not found: {marker}")
    depth = 0
    for index in range(brace, len(text)):
        char = text[index]
        if char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                return start, index + 1
    raise RuntimeError(f"function closing brace not found: {marker}")


def replace_function(path: str, marker: str, replacement: str) -> None:
    text = load(path)
    start, end = function_span(text, marker)
    save(path, text[:start] + replacement.rstrip() + text[end:])


def edit_function(path: str, marker: str, editor) -> None:
    text = load(path)
    start, end = function_span(text, marker)
    original = text[start:end]
    updated = editor(original)
    if updated == original:
        raise RuntimeError(f"{path}:{marker}: editor made no change")
    save(path, text[:start] + updated + text[end:])


save("kernel/parts/security_policy_format.inc", Path("/tmp/security_policy_format.inc").read_text(encoding="utf-8"))
save("tests/security_policy_format_test.cpp", Path("/tmp/security_policy_format_test.cpp").read_text(encoding="utf-8"))

kernel = load("kernel/kernel.cpp")
kernel = replace_once(
    kernel,
    '#include "parts/security_audit.inc"\n#include "parts/zgdb_policy.inc"',
    '#include "parts/security_audit.inc"\n#include "parts/security_policy_format.inc"\n#include "parts/zgdb_policy.inc"',
    "kernel include order",
)
save("kernel/kernel.cpp", kernel)

parse_replacement = '''bool parse_version(const uint8_t* data, uint32_t size, uint32_t& output) {
    return security_policy_format::parse_nonzero_decimal_u32(data, size, output);
}'''
for policy in (
    "kernel/parts/zgdb_policy.inc",
    "kernel/parts/zcap_policy.inc",
    "kernel/parts/zmid_policy.inc",
    "kernel/parts/zrwp_policy.inc",
    "kernel/parts/zvrt_policy.inc",
):
    replace_function(policy, "uint32_t parse_version(", parse_replacement)

replace_function(
    "kernel/parts/zcap_policy.inc",
    "bool canonical_string(",
    '''bool canonical_string(const char* value, uint32_t capacity, bool allow_empty) {
    return security_policy_format::canonical_absolute_path(value, capacity, allow_empty);
}''',
)
replace_function(
    "kernel/parts/zrwp_policy.inc",
    "bool canonical_path(",
    '''bool canonical_path(const Record& record) {
    return record.path_length && record.path_length < sizeof(record.path) &&
        static_cast<uint32_t>(string_length(record.path)) == record.path_length &&
        security_policy_format::canonical_absolute_path(record.path, sizeof(record.path), false);
}''',
)
replace_function(
    "kernel/parts/zvrt_policy.inc",
    "bool canonical_path(",
    '''bool canonical_path(const char* value, uint32_t capacity) {
    return security_policy_format::canonical_absolute_path(value, capacity, false);
}''',
)

for path in ("tools/zrwp_verify.cpp", "tools/zvrt_verify.cpp"):
    text = load(path)
    anchor = '#include "zenov_audit_format.hpp"\n'
    text = replace_once(text, anchor, anchor + '#include "../kernel/parts/security_policy_format.inc"\n', path + " helper include")
    save(path, text)
replace_function(
    "tools/zrwp_verify.cpp",
    "bool canonical_path(",
    '''bool canonical_path(const Record& record) {
    return record.path_length && record.path_length < sizeof(record.path) &&
        std::strlen(record.path) == record.path_length &&
        security_policy_format::canonical_absolute_path(record.path, sizeof(record.path), false);
}''',
)
replace_function(
    "tools/zvrt_verify.cpp",
    "bool canonical_path(",
    '''bool canonical_path(const char* value, std::size_t capacity) {
    if (capacity > 0xFFFFFFFFU) return false;
    return security_policy_format::canonical_absolute_path(
        value, static_cast<unsigned int>(capacity), false);
}''',
)


def harden_init(path: str, marker: str, version_path: str, variable: str, failure_marker: str) -> None:
    def editor(body: str) -> str:
        pattern = re.compile(
            rf'''uint8_t version_buffer\[16\](?:\{{\}})?;?\s*uint32_t version_size = 0(?:U)?;\s*'''
            rf'''{re.escape(variable)} = storage::read_file\("{re.escape(version_path)}", version_buffer, sizeof\(version_buffer\), version_size\)\s*'''
            rf'''\? parse_version\(version_buffer, version_size\) : 0(?:U)?;''',
            re.S,
        )
        replacement = (
            'uint8_t version_buffer[16]{}; uint32_t version_size = 0U;\n'
            f'    if (!storage::read_file("{version_path}", version_buffer, sizeof(version_buffer), version_size) ||\n'
            f'        !parse_version(version_buffer, version_size, {variable})) {{\n'
            f'        serial::line("{failure_marker}"); ready = false; return false;\n'
            '    }'
        )
        updated, count = pattern.subn(replacement, body, count=1)
        if count != 1:
            raise RuntimeError(f"{path}:{marker}: version-state assignment match count={count}")
        return updated
    edit_function(path, marker, editor)


harden_init("kernel/parts/zgdb_policy.inc", "bool init(", "/security/zenovguard.version", "persistent_policy_version", "ZGDB_INIT_FAILED reason=version-state")
harden_init("kernel/parts/zcap_policy.inc", "bool init(", "/security/syscall-capabilities.version", "persistent_policy_version", "ZCAP_INIT_FAILED reason=version-state")
harden_init("kernel/parts/zmid_policy.inc", "bool init(", "/security/zenovguard-intelligence.version", "persistent_database_version", "ZMID_INIT_FAILED reason=version-state")
harden_init("kernel/parts/zrwp_policy.inc", "bool init(", "/security/ransomware-policy.version", "persistent_policy_version", "ZRWP_INIT_FAILED reason=version-state")
harden_init("kernel/parts/zvrt_policy.inc", "bool init(", "/security/verified-reads.version", "persistent_manifest_version", "ZVRT_INIT_FAILED reason=version-state")


def harden_update(path: str, marker: str, active_expr: str, prefix: str) -> None:
    def editor(body: str) -> str:
        anchor = "Validation validation{};"
        if anchor not in body:
            raise RuntimeError(f"{path}: missing validation anchor")
        insertion = (
            anchor + " uint32_t next_version = 0U;\n"
            f"    if (!security_policy_format::next_version({active_expr}, next_version)) {{\n"
            f"        serial::line(\"{prefix}_SEQUENCE_REJECTED reason=version-exhausted\"); return false;\n"
            "    }"
        )
        body = body.replace(anchor, insertion, 1)
        old = active_expr + " + 1U"
        if old not in body:
            raise RuntimeError(f"{path}: missing next-version expression")
        return body.replace(old, "next_version")
    edit_function(path, marker, editor)


harden_update("kernel/parts/zgdb_policy.inc", "bool update(", "active_policy_version", "ZGDB")
harden_update("kernel/parts/zcap_policy.inc", "bool update(", "active_policy_version", "ZCAP")
harden_update("kernel/parts/zmid_policy.inc", "bool update(", "active_database_version", "ZMID")
harden_update("kernel/parts/zrwp_policy.inc", "bool update(", "active.version", "ZRWP")

makefile = load("Makefile")
makefile = replace_once(
    makefile,
    '$(BUILD)/zrwp-verify: tools/zrwp_verify.cpp tools/zenov_audit_format.hpp security/zrwp_crypto_material.hpp | $(BUILD)\n\t$(HOST_CXX) $(HOST_FLAGS) tools/zrwp_verify.cpp -o $@\n',
    '$(BUILD)/zrwp-verify: tools/zrwp_verify.cpp tools/zenov_audit_format.hpp security/zrwp_crypto_material.hpp kernel/parts/security_policy_format.inc | $(BUILD)\n\t$(HOST_CXX) $(HOST_FLAGS) tools/zrwp_verify.cpp -o $@\n\n'
    '$(BUILD)/security-policy-format-test: tests/security_policy_format_test.cpp kernel/parts/security_policy_format.inc | $(BUILD)\n'
    '\t$(HOST_CXX) $(HOST_FLAGS) tests/security_policy_format_test.cpp -o $@\n',
    "Makefile policy test target",
)
makefile = replace_once(
    makefile,
    '$(BUILD)/zvrt-verify: tools/zvrt_verify.cpp tools/zenov_audit_format.hpp security/zvrt_crypto_material.hpp | $(BUILD)',
    '$(BUILD)/zvrt-verify: tools/zvrt_verify.cpp tools/zenov_audit_format.hpp security/zvrt_crypto_material.hpp kernel/parts/security_policy_format.inc | $(BUILD)',
    "Makefile ZVRT helper dependency",
)
index = makefile.find('check: $(BUILD)/zenov-stage0 ')
if index < 0:
    raise RuntimeError("Makefile check target not found")
line_end = makefile.find('\n', index)
check_line = makefile[index:line_end]
if '$(BUILD)/security-policy-format-test' in check_line:
    raise RuntimeError("policy format test already in check target")
makefile = makefile[:index] + check_line.replace('check: ', 'check: $(BUILD)/security-policy-format-test ', 1) + makefile[line_end:]
makefile = replace_once(
    makefile,
    '\t$(BUILD)/zenov-stage0 --self-test\n',
    '\t$(BUILD)/security-policy-format-test\n\t$(BUILD)/zenov-stage0 --self-test\n',
    "Makefile policy test invocation",
)
save("Makefile", makefile)

workflow = load(".github/workflows/zvrt.yml")
workflow = replace_once(
    workflow,
    'run: sudo apt-get update && sudo apt-get install -y build-essential binutils openssl qemu-system-x86',
    'run: sudo apt-get update && sudo apt-get install -y build-essential binutils clang openssl qemu-system-x86',
    "ZVRT workflow clang install",
)
anchor = '''      - name: Run strict host and freestanding checks
        run: make clean check
'''
sanitizer_step = '''      - name: Validate policy-state parser with Clang sanitizers
        shell: bash
        run: |
          set -euo pipefail
          mkdir -p build/sanitized
          clang++ -std=c++17 -O1 -g -Wall -Wextra -Werror -Wpedantic \\
            -fsanitize=address,undefined,unsigned-integer-overflow,implicit-integer-conversion \\
            -fno-sanitize-recover=all -fno-omit-frame-pointer \\
            tests/security_policy_format_test.cpp -o build/sanitized/security-policy-format-test
          ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \\
          UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \\
            build/sanitized/security-policy-format-test | tee build/security-policy-format-test.log
          grep -Fq 'SECURITY_POLICY_FORMAT_TEST_OK decimal=10 path=11 version-wrap=blocked' build/security-policy-format-test.log

'''
workflow = replace_once(workflow, anchor, sanitizer_step + anchor, "ZVRT workflow sanitizer step")
contract_anchor = "          grep -Fq 'namespace zvrt {' kernel/parts/zvrt_policy.inc\n"
workflow = replace_once(
    workflow,
    contract_anchor,
    "          grep -Fq 'namespace security_policy_format {' kernel/parts/security_policy_format.inc\n"
    "          grep -Fq 'parse_nonzero_decimal_u32' kernel/parts/security_policy_format.inc\n"
    "          grep -Fq 'reason=version-state' kernel/parts/zvrt_policy.inc\n"
    + contract_anchor,
    "ZVRT workflow source contract",
)
workflow = replace_once(
    workflow,
    '            build/qemu/zvrt/\n',
    '            build/qemu/zvrt/\n            build/security-policy-format-test.log\n            build/sanitized/security-policy-format-test\n            kernel/parts/security_policy_format.inc\n            tests/security_policy_format_test.cpp\n',
    "ZVRT workflow evidence",
)
save(".github/workflows/zvrt.yml", workflow)

doc = load("docs/VERIFIED_READS_0.1.1.md")
appendix = '''\n## Policy-state parser hardening\n\nAll version-state files now use one strict freestanding decimal parser. The format is a non-zero canonical decimal followed by exactly one newline. Overflow, leading zeroes, missing newlines, trailing bytes and `UINT32_MAX -> 0` update wrap are rejected fail-closed. Signed ZRWP and ZVRT paths also use one canonical absolute-path validator that rejects control bytes, backslashes, duplicate separators and `.` or `..` components, including terminal components.\n\nThis strengthens live-storage corruption handling. It does not create TPM/NVRAM anti-rollback: a full offline replacement of the complete data image remains outside the 0.1.1 trust boundary.\n'''
if "## Policy-state parser hardening" in doc:
    raise RuntimeError("documentation section already exists")
save("docs/VERIFIED_READS_0.1.1.md", doc.rstrip() + "\n" + appendix)

expected = {
    ".github/workflows/zvrt.yml",
    "Makefile",
    "docs/VERIFIED_READS_0.1.1.md",
    "kernel/kernel.cpp",
    "kernel/parts/security_policy_format.inc",
    "kernel/parts/zgdb_policy.inc",
    "kernel/parts/zcap_policy.inc",
    "kernel/parts/zmid_policy.inc",
    "kernel/parts/zrwp_policy.inc",
    "kernel/parts/zvrt_policy.inc",
    "tests/security_policy_format_test.cpp",
    "tools/zrwp_verify.cpp",
    "tools/zvrt_verify.cpp",
}
changed = {line.strip() for line in subprocess.check_output(["git", "diff", "--name-only"], text=True).splitlines() if line.strip()}
if changed != expected:
    raise RuntimeError(f"production scope mismatch: expected={sorted(expected)} actual={sorted(changed)}")
print("POLICY_STATE_HARDENING_APPLIED files=13 decimal=fail-closed paths=canonical next-version=checked")
