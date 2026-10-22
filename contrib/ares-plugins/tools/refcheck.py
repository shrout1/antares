"""Report out-of-range initial references in every level of a plugin."""
import io, sys, zipfile
sys.path.insert(0, sys.argv[1])
from ares2antares import procyon
z = zipfile.ZipFile(sys.argv[2])
def walk(x, path, n, out):
    if isinstance(x, dict):
        for k, v in x.items():
            if k == 'initial':
                for i in (v if isinstance(v, list) else [v]):
                    if isinstance(i, int) and not 0 <= i < n:
                        out.append('%s.%s=%d' % (path, k, i))
            walk(v, path + '.' + str(k), n, out)
    elif isinstance(x, list):
        for j, v in enumerate(x):
            walk(v, '%s[%d]' % (path, j), n, out)
for name in sorted(z.namelist()):
    if not name.startswith('levels/'): continue
    lv = procyon.load(io.TextIOWrapper(z.open(name), encoding='utf-8'))
    out = []
    walk(lv, '', len(lv.get('initials', [])), out)
    if out:
        print('%s ch=%s initials=%d bad: %s' % (name, lv.get('chapter'), len(lv.get('initials', [])), ' '.join(out[:6])))
