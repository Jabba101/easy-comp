#!/usr/bin/env python3
"""Writes the preset tables in both plugins' Presets.h from the tuned .tsv files."""
import re
from pathlib import Path

ROOT = Path('/home/claude/EasyPlugins/Plugins')
HERE = Path(__file__).parent


def rows(tsv, fields):
    out = []
    for line in (HERE / tsv).read_text().splitlines():
        if not line.strip() or line.lstrip().startswith('#'):
            continue
        f = [x.strip() for x in line.split('|')]
        assert len(f) == fields, line
        out.append(f)
    return out


def num(v, width):
    return f'{float(v):.1f}f'.rjust(width)


def cstr(s):
    assert '"' not in s and '\\' not in s, s
    return f'"{s}"'


def wrap_tip(tip, indent=6, width=124):
    # One string literal per line if it's long, split at spaces
    words, lines, cur = tip.split(' '), [], ''
    for w in words:
        if cur and len(cur) + 1 + len(w) > width - indent - 3:
            lines.append(cur + ' ')
            cur = w
        else:
            cur = (cur + ' ' + w) if cur else w
    lines.append(cur)
    return '\n'.join(' ' * indent + cstr(l) for l in lines)


def header_for(first_column, columns, last=''):
    """A comment line whose labels end where each number ends."""
    line = '    //  category, name'.ljust(first_column)
    for label, width in columns:
        line += label.rjust(width + (0 if line.endswith(' ') or len(line) == first_column else 2)) + '  '
    return (line + last).rstrip()


def table(entries, header, line_for):
    parts, current = [header], None
    for f in entries:
        if f[0] != current:
            current = f[0]
            if current:
                parts.append(f'\n    // ---- {current} ' + '-' * max(4, 70 - len(current)))
            else:
                parts.append('')
        parts.append(line_for(f))
    return '\n'.join(parts) + '\n'


def comp_line(f):
    category, name, thr, ratio, att, rel, mk, mix, low, reacts, style, low_end, _target, _materials, tip = f
    assert reacts in ('peaks', 'average') and style in ('modern', 'vintage') and low_end in ('normal', 'thrust'), f
    head = f'    {{ {cstr(category)}, {cstr(name)},'
    return ((head.ljust(52) if len(head) < 52 else head + ' ') + f'{num(thr, 6)}, {num(ratio, 5)}, {num(att, 5)}, {num(rel, 7)}, {num(mk, 6)}, '
            f'{num(mix, 6)}, {num(low, 6)}, {(reacts + ",").ljust(8)} {(style + ",").ljust(8)} {low_end},\n' + wrap_tip(tip) + ' },')


def transient_line(f):
    category, name, punch, sustain, output, mix, _materials, tip = f
    head = f'    {{ {cstr(category)}, {cstr(name)},'
    return (head.ljust(62) + f'{num(punch, 6)}, {num(sustain, 6)}, {num(output, 5)}, {num(mix, 6)},\n'
            + wrap_tip(tip) + ' },')


def replace_table(path, array_name, body):
    text = path.read_text()
    pattern = re.compile(r'(inline const \w+ ' + array_name + r'\[\] =\n\{\n)(.*?)(\n\};\n)', re.S)
    assert pattern.search(text), path
    path.write_text(pattern.sub(lambda m: m.group(1) + body.rstrip('\n') + m.group(3), text, count=1))


comp = rows('presets_comp.tsv', 15)
names = [f[1] for f in comp]
assert len(names) == len(set(names)), 'duplicate Easy Comp preset names'
replace_table(ROOT / 'EasyComp/Presets.h', 'compressorPresets',
              table(comp, header_for(52, [('thresh', 6), ('ratio', 5), ('attack', 5), ('release', 7), ('make-up', 6), ('mix', 6), ('ignore', 6)],
                                     'reacts   style    low end'),
                    comp_line))

trans = rows('presets_transient.tsv', 8)
names = [f[1] for f in trans]
assert len(names) == len(set(names)), 'duplicate Easy Transient preset names'
replace_table(ROOT / 'EasyTransient/Presets.h', 'transientPresets',
              table(trans, header_for(62, [('punch', 6), ('sustain', 6), ('output', 5), ('mix', 6)]), transient_line))



def ds_line(f):
    category, name, frm, to, thr, rng_, mode, _materials, tip = f
    head = f'    {{ {cstr(category)}, {cstr(name)},'
    turns = {'band': 'harshBand', 'whole': 'wholeSound'}[mode]
    return (head.ljust(55) + f'{num(frm, 8)}, {num(to, 8)}, {num(thr, 6)}, {num(rng_, 5)}, {turns},\n'
            + wrap_tip(tip) + ' },')


ds = rows('presets_ds.tsv', 9)
names = [f[1] for f in ds]
assert len(names) == len(set(names)), 'duplicate Easy DS preset names'
replace_table(ROOT / 'EasyDS/Presets.h', 'dsPresets',
              table(ds, header_for(55, [('from', 8), ('to', 8), ('thresh', 6), ('range', 5)], 'turns down'), ds_line))

print(f'Easy Comp: {len(comp)} presets, Easy Transient: {len(trans)} presets, Easy DS: {len(ds)} presets')
