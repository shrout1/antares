#!/usr/bin/env python3
"""Give a plugin its own progress identifier.

Antares keys saved progress by info.pn's `identifier`, defaulting to sha1(title), so plugins
that share a title share progress. This writes identifier = sha1("<title>|<tag>").

usage: set_identifier.py PLUGIN.antaresplugin TAG
"""
import hashlib
import os
import re
import sys
import tempfile
import zipfile


def main(path, tag):
    with zipfile.ZipFile(path) as z:
        items = [(i, z.read(i.filename)) for i in z.infolist()]
    out = []
    for info, data in items:
        if info.filename == 'info.pn':
            text = data.decode('utf-8')
            title = re.search(r'^title:\s*"(.*)"', text, re.M).group(1)
            ident = hashlib.sha1(('%s|%s' % (title, tag)).encode('utf-8')).hexdigest()
            text = re.sub(r'^identifier:.*\n', '', text, flags=re.M)
            text = re.sub(r'^(format:.*\n)', r'\1identifier:    "%s"\n' % ident, text, count=1, flags=re.M)
            data = text.encode('utf-8')
            print('%s: identifier %s' % (os.path.basename(path), ident))
        out.append((info, data))
    fd, tmp = tempfile.mkstemp(dir=os.path.dirname(os.path.abspath(path)), suffix='.tmp')
    os.close(fd)
    with zipfile.ZipFile(tmp, 'w', zipfile.ZIP_DEFLATED) as z:
        for info, data in out:
            z.writestr(info, data, zipfile.ZIP_DEFLATED)
    os.replace(tmp, path)


if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2])
