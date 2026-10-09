"""Install the fixed official Qt SDK used by the custom Windows client.

Run: uv run --no-project --with py7zr python scripts/custom/install-qt.py
Archive hashes are checked against Qt's HTTPS repository before extraction.
"""
import hashlib
from pathlib import Path
import urllib.request
import xml.etree.ElementTree as ET
import py7zr

ROOT = Path('D:/data/tools/Qt')
BASE = 'https://download.qt.io/online/qtsdkrepository/windows_x86/desktop/qt6_6111/qt6_6111_msvc2022_64/'
PACKAGE = 'qt.qt6.6111.win64_msvc2022_64'
metadata = ET.fromstring(urllib.request.urlopen(BASE + 'Updates.xml').read())
entry = next(p for p in metadata.findall('PackageUpdate') if p.findtext('Name') == PACKAGE)
version = entry.findtext('Version')
cache = ROOT / 'archives'
cache.mkdir(parents=True, exist_ok=True)
for archive in entry.findtext('DownloadableArchives').split(','):
    archive = archive.strip()
    if not archive.startswith(('qtbase-', 'qtsvg-', 'qtdeclarative-', 'qttools-', 'qttranslations-')):
        continue
    name = version + archive
    url = BASE + PACKAGE + '/' + name
    expected = urllib.request.urlopen(url + '.sha1').read().decode().split()[0]
    path = cache / name
    if not path.exists() or hashlib.sha1(path.read_bytes()).hexdigest() != expected:
        print('Downloading', archive, flush=True)
        urllib.request.urlretrieve(url, path)
    if hashlib.sha1(path.read_bytes()).hexdigest() != expected:
        raise RuntimeError('Qt archive hash mismatch: ' + name)
    print('Extracting', archive, flush=True)
    with py7zr.SevenZipFile(path) as sdk:
        sdk.extractall(ROOT)
print('Qt SDK installed:', ROOT, flush=True)
