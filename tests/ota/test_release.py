"""Synthetic binary fixtures exist only in TemporaryDirectory, never release artifacts."""
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('release',ROOT/'scripts/release.py')
release=importlib.util.module_from_spec(spec)
spec.loader.exec_module(release)

def partition_image(rows):
    data=b''.join(struct.pack('<HBBII16sI',0x50aa,t,s,a,n,label.encode(),0) for t,s,a,n,label in rows)
    data+=b'\xeb\xeb'+b'\xff'*14+hashlib.md5(data).digest()
    return data+b'\xff'*(0xc00-len(data))

def app_image():
    header=bytearray(24);header[0]=0xe9;header[1]=1;header[23]=1
    struct.pack_into('<H',header,12,9)
    desc=bytearray(256);struct.pack_into('<I',desc,0,0xabcd5432)
    desc[16:21]=b'0.1.0';desc[48:57]=b'Read_Pico';desc[112:116]=b'v6.1'
    data=header+struct.pack('<II',0x3c000020,len(desc))+desc
    checksum=0xef
    for b in desc:checksum^=b
    data+=b'\0'*(15-len(data)%16)+bytes([checksum])
    return bytes(data)+hashlib.sha256(data).digest()

class ReleaseTest(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory();self.addCleanup(self.tmp.cleanup)
        self.build=Path(self.tmp.name)/'build';self.build.mkdir()
        (self.build/'config').mkdir()
        (self.build/'config/sdkconfig.json').write_text(json.dumps({'ESPTOOLPY_FLASHFREQ_120M':True,'SPIRAM_SPEED_120M':True}))
        self.output=Path(self.tmp.name)/'release'
        self.args={'extra_esptool_args':{'chip':'esp32s3'},'flash_files':{}}
        for role,offset,data in [('bootloader','0x0',b'boot-test-only'),('partition-table','0x8000',partition_image(release.FACTORY)),('app','0x10000',app_image())]:
            filename=role+'.bin';(self.build/filename).write_bytes(data)
            self.args[role]={'file':filename,'offset':offset};self.args['flash_files'][offset]=filename
        self.save()
    def save(self):(self.build/'flasher_args.json').write_text(json.dumps(self.args))
    def package(self,profile='production'):return release.package(self.build,self.output,'0.1.0',profile)
    def test_valid_local_package(self):
        m=self.package();self.assertTrue(m['web_flash']);self.assertFalse(m['publication_ready'])
        self.assertEqual(len(m['files']),3);self.assertFalse((self.output/'ota.txt').exists())
    def test_browser_accepts_packaged_files(self):
        self.package()
        module=(ROOT/'web/src/manifest.js').as_uri()
        script="""import {readFile} from 'node:fs/promises';
const {verifyFiles}=await import(process.argv[1]);
const root=process.argv[2],m=JSON.parse(await readFile(root+'/manifest.json','utf8'));
const files=await Promise.all(m.files.map(async f=>{const b=await readFile(root+'/'+f.file);return {name:f.file,size:b.length,arrayBuffer:async()=>b.buffer.slice(b.byteOffset,b.byteOffset+b.byteLength)};}));
await verifyFiles(m,files);"""
        subprocess.run(['node','--input-type=module','--eval',script,module,str(self.output)],check=True)
    def test_compile_only_cannot_flash(self):self.assertFalse(self.package('compile-only')['web_flash'])
    def test_does_not_overwrite_output(self):
        self.package()
        with self.assertRaises(ValueError):self.package()
    def test_wrong_version(self):
        with self.assertRaises(ValueError):release.package(self.build,self.output,'0.2.0','production')
    def test_chip_mismatch(self):
        self.args['extra_esptool_args']['chip']='esp32';self.save()
        with self.assertRaises(ValueError):self.package()
    def test_malicious_path(self):
        self.args['app']['file']='../app.bin';self.save()
        with self.assertRaises(ValueError):self.package()
    def test_offset_mismatch(self):
        self.args['app']['offset']='0x20000';self.save()
        with self.assertRaises(ValueError):self.package()
    def test_oversized_image(self):
        (self.build/'app.bin').write_bytes(b'x'*(release.SLOT_SIZE+1))
        with self.assertRaises(ValueError):self.package()
    def test_corrupt_image(self):
        p=self.build/'app.bin';b=bytearray(p.read_bytes());b[200]^=1;p.write_bytes(b)
        with self.assertRaises(ValueError):self.package()
    def test_corrupt_partition(self):
        p=self.build/'partition-table.bin';b=bytearray(p.read_bytes());b[20]^=1;p.write_bytes(b)
        with self.assertRaises(ValueError):self.package()
    def test_unknown_partition(self):
        rows=release.FACTORY.copy();rows[-1]=(1,0x81,0x420000,0x500000,'storage')
        (self.build/'partition-table.bin').write_bytes(partition_image(rows))
        with self.assertRaises(ValueError):self.package()
    def test_ota_requires_rollback(self):
        (self.build/'partition-table.bin').write_bytes(partition_image(release.OTA))
        with self.assertRaises(ValueError):self.package()
    def test_ota_package_separate_manifest(self):
        (self.build/'partition-table.bin').write_bytes(partition_image(release.OTA))
        (self.build/'config/sdkconfig.json').write_text(json.dumps({'ESPTOOLPY_FLASHFREQ_120M':True,'SPIRAM_SPEED_120M':True,'BOOTLOADER_APP_ROLLBACK_ENABLE':True}))
        m=self.package();self.assertFalse(m['web_flash']);self.assertTrue((self.output/'ota.txt').exists())
    def test_security_config_rejected(self):
        (self.build/'config/sdkconfig.json').write_text(json.dumps({'BOOTLOADER_APP_ANTI_ROLLBACK':True}))
        with self.assertRaises(ValueError):self.package('compile-only')
    def test_aligned_ota_layout(self):self.assertEqual(release.partitions(partition_image(release.OTA)),'folio-ota-v1')

if __name__=='__main__':unittest.main()
