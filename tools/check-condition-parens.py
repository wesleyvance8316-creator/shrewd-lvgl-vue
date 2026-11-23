#!/usr/bin/env python3
"""Coding rules for conditions:
- a conditional with two or more sub-conditions wraps EACH sub-condition
  in parentheses: if ((a) && (!b)); single-clause conditions stay bare;
- a ternary condition is parenthesized even when single-clause:
  (x) ? y : z;
- wrapped lines keep boolean operators at line start (clang-format's
  BreakBeforeBinaryOperators).

Heuristic checker for tools/check-style.sh: scans if/while/for
conditions, splits on top-level && / || and requires every operand of a
multi-operand expression to be fully parenthesized.
"""
import re
import sys
import glob


def split_top(expr):
    parts, depth, cur, i = [], 0, '', 0
    while i < len(expr):
        c = expr[i]
        if c == '(':
            depth += 1
        elif c == ')':
            depth -= 1
        if depth == 0 and expr.startswith('&&', i):
            parts.append(cur)
            cur = ''
            i += 2
            continue
        if depth == 0 and expr.startswith('||', i):
            parts.append(cur)
            cur = ''
            i += 2
            continue
        cur += c
        i += 1
    parts.append(cur)
    return [p.strip() for p in parts]


def wrapped(op):
    op = op.strip()
    if not (op.startswith('(') and op.endswith(')')):
        return False
    depth = 0
    for i, c in enumerate(op):
        if c == '(':
            depth += 1
        elif c == ')':
            depth -= 1
            if depth == 0 and i != len(op) - 1:
                return False
    return True



def clean(s):
    s = re.sub(r'/\*.*?\*/', ' ', s, flags=re.S)
    s = re.sub(r'//[^\n]*', ' ', s)
    s = re.sub(r"'(?:[^'\\]|\\.)'", '0', s)
    s = re.sub(r'"(?:[^"\\]|\\.)*"', '""', s)
    return s


def cond_before_q(stmt):
    """Top-level ternary condition before the first top-level '?', or None."""
    depth = 0
    qi = -1
    for i, c in enumerate(stmt):
        if c in '([':
            depth += 1
        elif c in ')]':
            depth -= 1
        elif c == '?' and depth == 0:
            qi = i
            break
    if qi < 0:
        return None
    d = 0
    j = qi - 1
    while j >= 0:
        c = stmt[j]
        if c == ')':
            d += 1
        elif c == '(':
            if d == 0:
                break
            d -= 1
        elif d == 0 and c in '=,;:{':
            break
        j -= 1
    cond = stmt[j + 1:qi].strip()
    cond = re.sub(r'^(?:return|else)\s+', '', cond)
    return cond


def main():
    violations = 0
    for f in sorted(glob.glob('library/**/*.c', recursive=True)
                    + glob.glob('tests/*.c')
                    + glob.glob('programs/*.c')
                    + glob.glob('tools/*.c')
                    + glob.glob('include/**/*.h', recursive=True)):
        lines = open(f, encoding='utf-8').read().split('\n')
        i = 0
        while i < len(lines):
            s = lines[i].strip()
            m = re.match(r'^(if|while|for)\s*\(', s)
            if not m:
                i += 1
                continue
            depth, expr, j, started = 0, '', i, False
            while j < len(lines) and j < i + 12:
                for ch in lines[j]:
                    if ch == '(':
                        depth += 1
                        if depth == 1:
                            started = True
                            continue
                    elif ch == ')':
                        depth -= 1
                        if depth == 0:
                            break
                    if started:
                        expr += ch
                if depth == 0:
                    break
                expr += ' '
                j += 1
            cond = expr.split(';')[1] if (m.group(1) == 'for'
                                           and expr.count(';') >= 1) else expr
            ops = split_top(cond)
            if len(ops) >= 2 and not all(wrapped(o) for o in ops):
                print(f"{f}:{i+1}: sub-conditions need parentheses: {s[:64]}")
                violations += 1
            i = j + 1
    for f in sorted(glob.glob('library/**/*.c', recursive=True)
                    + glob.glob('tests/*.c')
                    + glob.glob('programs/*.c')
                    + glob.glob('tools/*.c')
                    + glob.glob('include/**/*.h', recursive=True)):
        lines = clean(open(f, encoding='utf-8').read()).split('\n')
        stmt, start = '', 0
        for i, raw in enumerate(lines):
            line = raw.rstrip()
            if not stmt:
                start = i
            stmt += ' ' + line
            if line.endswith(';') or line.endswith('{') or line.endswith('}'):
                cond = cond_before_q(stmt)
                if cond and not wrapped(cond):
                    print(f"{f}:{start + 1}: ternary condition needs "
                          f"parentheses: {cond[:56]}")
                    violations += 1
                stmt = ''

    if violations:
        print(f"condition-parens: {violations} violation(s)")
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
