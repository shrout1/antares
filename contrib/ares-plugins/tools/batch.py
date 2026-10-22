#!/usr/bin/env python3
"""Batch-convert every Ares plugin in the archive folders with ares2antares.

usage: batch.py CONVERTER_DIR SRC_ROOT OUT_DIR WORK_DIR
"""
import hashlib
import os
import re
import shutil
import subprocess
import sys

CONV, SRC, OUT, WORK = (os.path.abspath(a) for a in sys.argv[1:5])
sys.path.insert(0, CONV)
from ares2antares import sources  # noqa: E402

ARCHIVE_RE = re.compile(r'\.(sit|sea|hqx|bin|cpt|zip|tgz|gz|tar)$', re.I)
README_RE = re.compile(r'read ?me|about|info|instructions', re.I)
OPTS = ['--same-race-hue', 'red', '--match-sprite-rotations']


def unar(path, dest):
    os.makedirs(dest, exist_ok=True)
    r = subprocess.run(['unar', '-q', '-forks', 'visible', '-o', dest, path],
                       capture_output=True, text=True)
    return r.returncode == 0


def macbinary_data(path):
    """Data fork of a MacBinary file, or None."""
    d = open(path, 'rb').read(128)
    if len(d) < 128 or d[0] != 0 or d[74] != 0 or not 1 <= d[1] <= 63:
        return None
    dlen = int.from_bytes(d[83:87], 'big')
    data = open(path, 'rb').read()[128:128 + dlen]
    return data if len(data) == dlen and dlen else None


def expand(path, dest, depth=0):
    """Unpack path into dest, then unpack any archives found inside (4 levels).

    Archives are recognised by content, not name: plenty are nested with no
    extension, or wrapped in MacBinary that unar won't open.
    """
    if not unar(path, dest):
        data = macbinary_data(path)
        if data is None:
            return False
        inner = path + '.data'
        open(inner, 'wb').write(data)
        if not unar(inner, dest):
            return False
    if depth >= 4:
        return True
    for dp, dn, fn in os.walk(dest):
        dn[:] = [d for d in dn if not d.endswith('.x')]
        for f in fn:
            p = os.path.join(dp, f)
            if f.endswith(('.rsrc', '.data')) or os.path.exists(p + '.x'):
                continue
            if not expand(p, p + '.x', depth + 1):
                shutil.rmtree(p + '.x', ignore_errors=True)
    return True


def readme_for(fork_path, root):
    """A plain-text ReadMe next to the plugin (or anywhere in the archive)."""
    cands = []
    for dp, dn, fn in os.walk(root):
        for f in fn:
            p = os.path.join(dp, f)
            if f.endswith('.rsrc') or not README_RE.search(f) or os.path.getsize(p) > 64000:
                continue
            d = open(p, 'rb').read()
            if d and sum(c < 9 or 13 < c < 32 for c in d[:2000]) == 0:
                cands.append((os.path.dirname(p) != os.path.dirname(fork_path), p))
    return sorted(cands)[0][1] if cands else None


def text_file(src, dst):
    d = open(src, 'rb').read().replace(b'\r\n', b'\n').replace(b'\r', b'\n')
    open(dst, 'w', encoding='utf-8').write(d.decode('mac_roman'))


def main():
    os.makedirs(os.path.join(OUT, 'notes'), exist_ok=True)
    log = open(os.path.join(OUT, 'conversion-log.tsv'), 'w', encoding='utf-8')
    log.write('archive\tplugin file\tresult\tdetail\n')
    seen = {}
    for folder in ('Scenarios', 'Utilities'):
        for name in sorted(os.listdir(os.path.join(SRC, folder)),
                           key=lambda n: int(n.split('_')[0]) if n[0].isdigit() else 0):
            arch = os.path.join(SRC, folder, name)
            num = name.split('_')[0]
            dest = os.path.join(WORK, num)
            shutil.rmtree(dest, ignore_errors=True)
            os.makedirs(dest)
            expand(arch, os.path.join(dest, 'x'))
            try:
                found = [p for p, _ in sources.find_forks(dest)]
                if not found:
                    # a bare MacBinary/raw fork that unar didn't unpack
                    shutil.copy(arch, os.path.join(dest, 'orig'))
                    found = [p for p, _ in sources.find_forks(os.path.join(dest, 'orig'))]
            except Exception as e:
                found = []
                log.write('%s/%s\t\terror\tscan failed: %s\n' % (folder, name, e))
            if not found:
                log.write('%s/%s\t\tno plugin\tno Ares scenario data inside\n' % (folder, name))
                log.flush()
                print('-- %s: no plugin' % name, flush=True)
                continue
            for i, p in enumerate(found):
                tmpout = os.path.join(dest, 'out%d.antaresplugin' % i)
                cmd = [sys.executable, os.path.join(CONV, 'ares2antares.py'), p,
                       '-o', tmpout, '--notes', tmpout + '.notes'] + OPTS
                rm = readme_for(p, dest)
                if rm:
                    text_file(rm, tmpout + '.about')
                    cmd += ['--about', tmpout + '.about']
                r = subprocess.run(cmd, capture_output=True, text=True, timeout=600)
                rel = os.path.relpath(p, dest)
                if r.returncode or not os.path.exists(tmpout):
                    err = (r.stderr.strip() or r.stdout.strip()).splitlines()
                    log.write('%s/%s\t%s\tFAILED\t%s\n' % (folder, name, rel, err[-1] if err else '?'))
                    print('!! %s [%s]: FAILED %s' % (name, rel, err[-1] if err else ''), flush=True)
                    continue
                m = re.search(r'converted .* -> .*', r.stdout)
                # name from the plugin title the converter derived
                title = re.search(r'^title:\s*"(.*)"', subprocess.run(
                    ['unzip', '-p', tmpout, 'info.pn'], capture_output=True, text=True).stdout, re.M)
                if not title or not title.group(1).strip():
                    # blank in the plugin (The Iron Fist's is " "): title it after the archive
                    t = re.sub(r'(\.(sit|sea|hqx|bin|cpt|zip|tgz))+$', '', name.split('_', 1)[1], flags=re.I)
                    t = t.replace('_', ' ').strip()
                    subprocess.run(cmd + ['--title', t], capture_output=True, text=True, timeout=600)
                    title = re.search(r'^title:\s*"(.*)"', subprocess.run(
                        ['unzip', '-p', tmpout, 'info.pn'], capture_output=True, text=True).stdout, re.M)
                slug = re.sub(r'[^A-Za-z0-9]+', '-', title.group(1) if title else rel).strip('-') or num
                digest = hashlib.sha1(subprocess.run(
                    ['unzip', '-p', tmpout], capture_output=True).stdout).hexdigest()
                if digest in seen:
                    log.write('%s/%s\t%s\tduplicate\tsame as %s\n' % (folder, name, rel, seen[digest]))
                    print('== %s [%s]: duplicate of %s' % (name, rel, seen[digest]), flush=True)
                    continue
                final = slug + '.antaresplugin'
                if os.path.exists(os.path.join(OUT, final)) or slug == 'Ares':
                    # titles get reused (template plugins never renamed, and "Ares" is the built-in
                    # game's); use the plugin file's name
                    stem = os.path.basename(rel)
                    stem = stem[:-5] if stem.endswith('.rsrc') else stem
                    stem = re.sub(r'[^A-Za-z0-9]+', '-', stem).strip('-') or num
                    if stem == slug:
                        stem = '#' + num
                    final = '%s (%s).antaresplugin' % (slug, stem)
                    if os.path.exists(os.path.join(OUT, final)):
                        final = '%s (%s-%s).antaresplugin' % (slug, stem, num)
                seen[digest] = final
                shutil.copy(tmpout, os.path.join(OUT, final))
                if final[len(slug):].startswith(' ('):
                    # Antares keys saved progress by sha1(title) unless info.pn says otherwise
                    tag = final[len(slug) + 2:-len(').antaresplugin')]
                    subprocess.run([sys.executable, os.path.join(os.path.dirname(__file__),
                                    'set_identifier.py'), os.path.join(OUT, final), tag], check=True)
                shutil.copy(tmpout + '.notes', os.path.join(OUT, 'notes', final[:-14] + '.txt'))
                summary = m.group(0).split(' -> ')[0] if m else 'ok'
                extra = [l.strip() for l in r.stdout.splitlines()[1:]]
                log.write('%s/%s\t%s\tok\t%s -> %s%s%s\n' % (
                    folder, name, rel, summary, final,
                    '; ' + '; '.join(extra) if extra else '', '; about from ' + os.path.basename(rm) if rm else ''))
                print('ok %s [%s] -> %s' % (name, rel, final), flush=True)
            log.flush()


if __name__ == '__main__':
    main()
