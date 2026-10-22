#!/usr/bin/env python3
"""Read the archive's description for each entry from the saved Ares Addons page (_index.html)
and write it as description.txt inside each .antaresplugin converted from that entry.

usage: archive_descriptions.py ARCHIVE_PAGE CONVERTED_DIR [--dry-run]
"""
import html
import json
import os
import re
import sys
import urllib.parse
import zipfile


def descriptions(page):
    """{archive path ("Scenarios/22_Aaaagh.hqx"): (title, description)}"""
    s = open(page, encoding='utf-8', errors='replace').read()
    out = {}
    for m in re.finditer(r'<a id="[^"]*" href="([^"]+)">(.*?)</a>.*?<div class="desc">(.*?)</div>',
                         s, re.S):
        href, title, desc = m.groups()
        desc = re.sub(r'<br\s*/?>', '\n', desc, flags=re.I)
        desc = html.unescape(re.sub(r'<[^>]+>', '', desc))
        desc = '\n'.join(l.rstrip() for l in desc.replace('\r', '').split('\n'))
        desc = re.sub(r'\n{3,}', '\n\n', desc).strip()
        out[entry(urllib.parse.unquote(href))] = (html.unescape(title).strip(), desc)
    return out


def entry(archive):
    """"Scenarios/22_" for "Scenarios/22_Aaaagh.hqx"; the log and page spell the rest differently."""
    return re.match(r'[^/]+/\d+_', archive).group(0)


def converted(converted_dir):
    """{archive path: [plugin file names]} from conversion-log.tsv."""
    out = {}
    with open(os.path.join(converted_dir, 'conversion-log.tsv'), encoding='utf-8') as f:
        next(f)
        for line in f:
            archive, _, result, detail = (line.rstrip('\n').split('\t') + [''] * 4)[:4]
            m = re.search(r'-> (.+?\.antaresplugin)', detail)
            if result == 'ok' and m:
                out.setdefault(entry(archive), []).append(m.group(1))
    return out


def title(path):
    with zipfile.ZipFile(path) as z:
        m = re.search(r'^title:\s*"(.*)"', z.read('info.pn').decode('utf-8'), re.M)
    return m.group(1) if m else ''


def official(converted_dir, plugins):
    """{entry: [Official/... plugin paths]}: arescentral's own conversions, matched on title to
    the plugin converted from the same archive entry."""
    by_title = {}
    for e, files in plugins.items():
        for f in files:
            by_title.setdefault(title(os.path.join(converted_dir, f)), e)
    out = {}
    root = os.path.join(converted_dir, 'Official')
    for d in sorted(os.listdir(root)) if os.path.isdir(root) else []:
        for f in os.listdir(os.path.join(root, d)):
            if f.endswith('.antaresplugin'):
                path = os.path.join('Official', d, f)
                e = by_title.get(title(os.path.join(converted_dir, path)))
                if e:
                    out.setdefault(e, []).append(path)
                else:
                    print('  no archive entry for', path)
    return out


def write(path, text):
    with zipfile.ZipFile(path) as z:
        if 'description.txt' in z.namelist() and z.read('description.txt').decode('utf-8') == text:
            return False
        items = [(i, z.read(i.filename)) for i in z.infolist() if i.filename != 'description.txt']
    tmp = path + '.tmp'
    with zipfile.ZipFile(tmp, 'w', zipfile.ZIP_DEFLATED) as z:
        for i, data in items:
            z.writestr(i, data)
        z.writestr('description.txt', text)
    os.replace(tmp, path)
    return True


def main():
    page, converted_dir = sys.argv[1:3]
    dry = '--dry-run' in sys.argv
    descs = descriptions(page)
    plugins = converted(converted_dir)
    for archive, files in official(converted_dir, plugins).items():
        plugins.setdefault(archive, []).extend(files)
    report = {}
    for archive, files in sorted(plugins.items()):
        title, desc = descs.get(archive, ('', ''))
        for f in files:
            report[f] = {'archive': archive, 'description': desc}
            if desc and not dry:
                write(os.path.join(converted_dir, f), desc + '\n')
    missing = sorted(f for f, r in report.items() if not r['description'])
    print('%d plugins, %d with a description' % (len(report), len(report) - len(missing)))
    for f in missing:
        print('  no description:', f, '(%s)' % report[f]['archive'])
    with open(os.path.join(converted_dir, 'descriptions.json'), 'w', encoding='utf-8') as f:
        json.dump(report, f, indent=1, ensure_ascii=False)


main()
