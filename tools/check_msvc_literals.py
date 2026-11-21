#!/usr/bin/env python3
"""MSVC string-literal size check (error C2026 "string too big" / C1091).

MSVC rejects a single string literal piece longer than ~16380 bytes, and a concatenation of adjacent
pieces longer than 65535 bytes. Clang and GCC accept both, so the macOS / Linux / MinGW builds never
catch it - the embedded shader sources (src/render3d/*_msl.hpp) broke the MSVC build that way. Long
raw strings are split at a line start with `)MSL" R"MSL(` (see terrain_v3_msl.hpp).

    python3 tools/check_msvc_literals.py [ROOT...]     (default: src tests)

Exit 1 if any piece is over PIECE_LIMIT or any concatenation over TOTAL_LIMIT (both with a margin).
Run by ctest (msvc_literal_check) when Python 3 is found.
"""
import os
import re
import sys

PIECE_LIMIT = 16000   # MSVC: 16380 bytes per piece; keep a margin
TOTAL_LIMIT = 65000   # MSVC: 65535 bytes for adjacent pieces concatenated
EXTS = ('.c', '.cc', '.cpp', '.cxx', '.h', '.hh', '.hpp', '.hxx', '.inc', '.inl', '.mm')

_simple_escape = re.compile(r'\\(x[0-9a-fA-F]+|[0-7]{1,3}|u[0-9a-fA-F]{4}|U[0-9a-fA-F]{8}|.)', re.S)


def _escaped_len(body):
    """Approximate byte length of an ordinary literal's contents (escapes count as one byte;
    \\u / \\U as their UTF-8 length)."""
    n = 0
    pos = 0
    for m in _simple_escape.finditer(body):
        n += len(body[pos:m.start()].encode('utf-8'))
        e = m.group(1)
        if e[0] == 'u' or e[0] == 'U':
            n += len(chr(int(e[1:], 16)).encode('utf-8', 'surrogatepass'))
        else:
            n += 1
        pos = m.end()
    return n + len(body[pos:].encode('utf-8'))


def scan(text):
    """Yield (line, [piece lengths]) for each run of adjacent string literals."""
    i, n, line = 0, len(text), 1
    run, run_line, last_end = [], 0, -1
    while i < n:
        c = text[i]
        if c == '\n':
            line += 1
            i += 1
            continue
        if text.startswith('//', i):
            j = text.find('\n', i)
            i = n if j < 0 else j
            continue
        if text.startswith('/*', i):
            j = text.find('*/', i + 2)
            j = n if j < 0 else j + 2
            line += text.count('\n', i, j)
            i = j
            continue
        if c.isspace():
            i += 1
            continue
        # string literal with optional encoding prefix (u8, u, U, L) and R for raw
        m = re.match(r'(u8|u|U|L)?(R)?"', text[i:i + 4])
        if m and (i == 0 or not (text[i - 1].isalnum() or text[i - 1] == '_')):
            start = i
            start_line = line
            if m.group(2):  # raw string R"delim( ... )delim"
                k = i + m.end()
                p = text.find('(', k)
                delim = text[k:p]
                end = text.find(')' + delim + '"', p + 1)
                if end < 0:
                    raise ValueError(f'unterminated raw string at line {line}')
                length = len(text[p + 1:end].encode('utf-8'))
                i = end + len(delim) + 2
            else:
                k = i + m.end()
                while text[k] != '"':
                    k += 2 if text[k] == '\\' else 1
                length = _escaped_len(text[i + m.end():k])
                i = k + 1
            line += text.count('\n', start, i)
            if run and last_end >= 0 and text[last_end:start].strip() == '' or \
                    run and _only_comments(text[last_end:start]):
                run.append(length)
            else:
                if run:
                    yield run_line, run
                run, run_line = [length], start_line
            last_end = i
            continue
        if c == "'":  # character literal (skip digit separators: 1'000)
            if i > 0 and text[i - 1].isalnum() and text[i - 1].isdigit():
                i += 1
                continue
            k = i + 1
            while k < n and text[k] != "'":
                if text[k] == '\n':
                    break
                k += 2 if text[k] == '\\' else 1
            i = k + 1
            continue
        # an all-caps macro between pieces (R"(...)" D2K_MSL_POINT_LIGHTS R"(...)") usually expands to
        # more literals: keep the run going (its own text is checked where it is defined)
        mm = re.match(r'[A-Z][A-Z0-9_]*\b', text[i:i + 128])
        if run and mm and re.match(r'\s*(u8|u|U|L)?R?"', text[i + mm.end():i + mm.end() + 8]):
            i += mm.end()
            last_end = i
            continue
        # any other token ends a run of adjacent literals
        if run:
            yield run_line, run
            run = []
        last_end = -1
        i += 1
    if run:
        yield run_line, run


def _only_comments(s):
    s = re.sub(r'//[^\n]*', '', s)
    s = re.sub(r'/\*.*?\*/', '', s, flags=re.S)
    return s.strip() == ''


def main(argv):
    roots = argv or ['src', 'tests']
    here = os.path.dirname(os.path.abspath(__file__))
    repo = os.path.dirname(here)
    bad = 0
    biggest = (0, '')
    for root in roots:
        root = root if os.path.isabs(root) else os.path.join(repo, root)
        for dirpath, _, files in os.walk(root):
            for f in sorted(files):
                if not f.endswith(EXTS):
                    continue
                path = os.path.join(dirpath, f)
                with open(path, encoding='utf-8', errors='surrogateescape') as fh:
                    text = fh.read()
                if '"' not in text:
                    continue
                rel = os.path.relpath(path, repo)
                for ln, pieces in scan(text):
                    for idx, p in enumerate(pieces):
                        if p > biggest[0]:
                            biggest = (p, f'{rel}:{ln} piece {idx + 1}')
                        if p > PIECE_LIMIT:
                            print(f'{rel}:{ln}: string literal piece {idx + 1} is {p} bytes '
                                  f'(MSVC C2026 limit ~16380; keep under {PIECE_LIMIT}: split it)')
                            bad += 1
                    total = sum(pieces)
                    if total > TOTAL_LIMIT:
                        print(f'{rel}:{ln}: adjacent string literals total {total} bytes '
                              f'(MSVC limit 65535; keep under {TOTAL_LIMIT}: use several variables)')
                        bad += 1
    if bad:
        print(f'{bad} MSVC string-literal problem(s)')
        return 1
    print(f'MSVC string literals ok (largest piece {biggest[0]} bytes: {biggest[1]})')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
