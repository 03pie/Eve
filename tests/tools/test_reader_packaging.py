import copy
import hashlib
import io
import json
from pathlib import Path
import shutil
import stat
import subprocess
import sys
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
import package_reader as reader


class ReaderPackagingTests(unittest.TestCase):
    def test_fixed_deployment_layout(self):
        names={'Qt5Core.dll','Qt5Gui.dll','Qt5Widgets.dll','qt.conf',*reader.common.CRT_FILES,
               *('plugins/'+n for n in reader.common.QT_PLUGINS),'licenses/source.txt'}
        payload={n:{} for n in names}
        self.assertEqual(reader.deployment_members(payload),names)
        payload.pop('plugins/platforms/qwindows.dll')
        with self.assertRaises(ValueError):reader.deployment_members(payload)

    def test_frozen_source_rejections(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary); host = root/'host'; plugin = root/'plugin'; host.mkdir(); plugin.mkdir()
            def entry(path, data):
                path.write_bytes(data)
                return {'bytes':len(data),'sha256':hashlib.sha256(data).hexdigest()}
            hostfiles = {n:entry(host/n,b'fixture') for n in reader.HOST_FILES}
            dll = entry(plugin/'reader.dll', b'fixture DLL')
            module = {'moduleId':'org.example.reader','plugins':['reader'],'moduleVersion':'1.1.0',
                      'library':'reader.dll','sha256':dll['sha256'],'loader':'qt-ui','unloadPolicy':'resident'}
            manifest = entry(plugin/'module.json',json.dumps(module).encode())
            plan = {'schema':'cordis.reader.package-inputs/1','rootFreezeApproved':True,
                    'hostRoot':str(host),'hostFiles':hostfiles,'pluginRoot':str(plugin),
                    'pluginFiles':{'module.json':manifest,'reader.dll':dll}}
            sources, actual = reader.select_sources(plan)
            self.assertEqual(len(sources),9);self.assertEqual(actual,module)
            bad=copy.deepcopy(plan);bad['hostFiles'].pop(next(iter(hostfiles)))
            with self.assertRaises(ValueError):reader.select_sources(bad)
            bad=copy.deepcopy(plan);bad['rootFreezeApproved']=False
            with self.assertRaises(ValueError):reader.select_sources(bad)
            (plugin/'reader.dll').write_bytes(b'changed')
            with self.assertRaises(ValueError):reader.select_sources(plan)

    def test_zip_paths_and_links(self):
        for name,linked in [('Root/../escape',False),('Root/NUL.txt',False),('Root/link',True)]:
            stream=io.BytesIO()
            with zipfile.ZipFile(stream,'w') as z:
                entry=zipfile.ZipInfo(name)
                if linked:entry.external_attr=(stat.S_IFLNK|0o777)<<16
                z.writestr(entry,b'data')
            stream.seek(0)
            with zipfile.ZipFile(stream) as z,self.assertRaises(ValueError):reader.checked_zip(z)
        stream=io.BytesIO()
        with zipfile.ZipFile(stream,'w') as z:
            z.writestr('Root/A',b'a');z.writestr('Root/a',b'b')
        stream.seek(0)
        with zipfile.ZipFile(stream) as z,self.assertRaises(ValueError):reader.checked_zip(z)

    @unittest.skipUnless(sys.platform=='win32','Windows PowerShell fixture')
    def test_stock_powershell_verifier(self):
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary)
            script=root/'verify-reader.ps1';shutil.copyfile(ROOT/'delivery/windows/reader/verify-reader.ps1',script)
            payload=root/'plugins/current';payload.mkdir(parents=True)
            file=payload/'unicode.txt';file.write_text('真实内容𠮷',encoding='utf-8')
            paths=[script,file]
            files=[{'path':p.relative_to(root).as_posix(),'bytes':p.stat().st_size,
                    'sha256':hashlib.sha256(p.read_bytes()).hexdigest()} for p in paths]
            manifest=root/'reader-manifest.json'
            manifest.write_text(json.dumps({'schema':'cordis.reader.package/1','files':files}),encoding='utf-8')
            sha=hashlib.sha256(manifest.read_bytes()).hexdigest();(root/'SHA256SUMS.txt').write_text('fixture')
            args=['powershell.exe','-NoProfile','-File',str(script),'-ExpectedManifestSha256',sha]
            result=subprocess.run(args,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
            self.assertEqual(result.returncode,0,result.stderr.decode(errors='replace'))
            self.assertTrue(json.loads(result.stdout)['passed'])
            file.write_bytes(b'tampered')
            self.assertNotEqual(subprocess.run(args,stdout=subprocess.PIPE,stderr=subprocess.PIPE).returncode,0)


if __name__=='__main__':
    unittest.main()
