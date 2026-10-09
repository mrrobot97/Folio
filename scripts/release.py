#!/usr/bin/env python3
"""Build a local release directory from real IDF flasher_args.json; never flash or publish."""
import argparse
import hashlib
import json
import re
import shutil
import struct
from pathlib import Path

BOARD = 'RDP-G01-W'
FLASH_SIZE = 16 * 1024 * 1024
SLOT_SIZE = 4 * 1024 * 1024
FACTORY = [(1,2,0x9000,0x5000,'nvs'), (1,1,0xE000,0x1000,'phy_init'),
           (0,0,0x10000,SLOT_SIZE,'factory'), (1,0x81,0x410000,0x500000,'storage')]
OTA = [(1,2,0x9000,0x5000,'nvs'), (1,1,0xE000,0x1000,'phy_init'),
       (0,0x10,0x10000,SLOT_SIZE,'ota_0'), (1,0x81,0x410000,0x500000,'storage'),
       (0,0x11,0x910000,SLOT_SIZE,'ota_1'), (1,0,0xD10000,0x2000,'otadata')]


def sha(data):
    return hashlib.sha256(data).hexdigest()


def partitions(data):
    rows = []
    verified = False
    for offset in range(0, len(data), 32):
        row = data[offset:offset+32]
        if len(row) != 32:
            raise ValueError('Truncated partition table')
        magic = struct.unpack_from('<H', row)[0]
        if magic == 0xEBEB:
            if row[:16] != b'\xeb\xeb' + b'\xff'*14 or row[16:] != hashlib.md5(data[:offset]).digest():
                raise ValueError('Partition table MD5 mismatch')
            verified = True
            if any(x != 255 for x in data[offset+32:]):
                raise ValueError('Unexpected bytes after partition checksum')
            break
        if magic != 0x50AA:
            raise ValueError('Missing/invalid partition checksum')
        _, typ, sub, address, size, label, flags = struct.unpack('<HBBII16sI', row)
        if flags or not size or address % (0x10000 if typ == 0 else 0x1000):
            raise ValueError('Unsupported encrypted/unaligned partition')
        if address + size > FLASH_SIZE or any(address < p[2]+p[3] and address+size > p[2] for p in rows):
            raise ValueError('Overlapping or out-of-flash partition')
        rows.append((typ, sub, address, size, label.split(b'\0')[0].decode('ascii')))
    if not verified:
        raise ValueError('Partition table has no MD5 entry')
    if rows == FACTORY:
        return 'factory-v1'
    if rows == OTA:
        return 'folio-ota-v1'
    raise ValueError('Unknown layout; migration must be reviewed explicitly')


def app_description(data):
    if len(data) < 288 or data[0] != 0xE9 or not 1 <= data[1] <= 16:
        raise ValueError('Not an ESP application image')
    if struct.unpack_from('<H', data, 12)[0] != 9:
        raise ValueError('Application chip is not ESP32-S3')
    cursor, checksum = 24, 0xEF
    for _ in range(data[1]):
        if cursor+8 > len(data):
            raise ValueError('Truncated image segment header')
        size = struct.unpack_from('<I', data, cursor+4)[0]
        cursor += 8
        if cursor+size > len(data):
            raise ValueError('Truncated image segment')
        for value in data[cursor:cursor+size]:
            checksum ^= value
        cursor += size
    checksum_at = (cursor // 16)*16 + 15
    if checksum_at >= len(data) or data[checksum_at] != checksum:
        raise ValueError('ESP image checksum mismatch')
    end = checksum_at+1
    if data[23] != 1 or data[end:end+32] != hashlib.sha256(data[:end]).digest():
        raise ValueError('ESP image appended SHA256 missing/mismatched')
    if len(data) != end+32:
        raise ValueError('Signed/padded images need an explicitly reviewed release path')
    if struct.unpack_from('<I', data, 32)[0] != 0xABCD5432:
        raise ValueError('Application descriptor missing')
    def field(offset, size):
        value = data[offset:offset+size]
        if b'\0' not in value:
            raise ValueError('Unterminated app descriptor')
        return value.split(b'\0')[0].decode('ascii')
    return {'version': field(48,32), 'project': field(80,32), 'idf': field(144,32)}


def package(build, output, version, profile):
    if not re.fullmatch(r'(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)', version) or any(int(p)>65535 for p in version.split('.')):
        raise ValueError('Version must be canonical x.y.z, each <=65535')
    args = json.loads((build/'flasher_args.json').read_text())
    if args.get('extra_esptool_args', {}).get('chip') != 'esp32s3':
        raise ValueError('flasher_args must target esp32s3')
    files, blobs = [], {}
    for role in ('bootloader','partition-table','app'):
        info = args.get(role)
        if not isinstance(info, dict) or 'file' not in info or 'offset' not in info:
            raise ValueError('Missing flasher_args role: '+role)
        path = (build / info['file']).resolve()
        if not path.is_relative_to(build.resolve()):
            raise ValueError('Input path escapes build directory')
        offset = int(str(info['offset']), 0)
        if args.get('flash_files', {}).get(str(info['offset'])) != info['file']:
            raise ValueError('Role disagrees with flash_files')
        data = path.read_bytes()
        if not data:
            raise ValueError('Empty image: '+role)
        name = role+'.bin'
        blobs[name] = data
        files.append({'role':role,'file':name,'offset':offset,'size':len(data),'sha256':sha(data)})
    layout = partitions(blobs['partition-table.bin'])
    offsets = {f['role']:f['offset'] for f in files}
    if offsets['partition-table'] != 0x8000 or offsets['bootloader'] != 0 or offsets['app'] != 0x10000:
        raise ValueError('Unsupported boot/partition/app offsets; derived offsets require review')
    if len(blobs['bootloader.bin']) > 0x8000 or len(blobs['partition-table.bin']) != 0xC00:
        raise ValueError('Invalid bootloader or partition table size')
    app = blobs['app.bin']
    if len(app) > SLOT_SIZE:
        raise ValueError('Application exceeds 4 MiB slot')
    desc = app_description(app)
    if desc['version'] != version:
        raise ValueError('Requested version differs from embedded application version')
    if desc['project'] not in ('Read_Pico','Folio') or not re.match(r'^v6\.1(?:[.-]|$)', desc['idf']):
        raise ValueError('Unexpected project/IDF version')
    config_path = build/'config'/'sdkconfig.json'
    config = json.loads(config_path.read_text())
    if profile == 'production' and (not config.get('ESPTOOLPY_FLASHFREQ_120M') or not config.get('SPIRAM_SPEED_120M')):
        raise ValueError('Production packaging requires the official 120 MHz board timing')
    rollback = config.get('BOOTLOADER_APP_ROLLBACK_ENABLE') is True
    if config.get('BOOTLOADER_APP_ANTI_ROLLBACK') or config.get('SECURE_BOOT') or config.get('SECURE_FLASH_ENC_ENABLED'):
        raise ValueError('Security/eFuse configurations need separate review; not handled by this packager')
    if layout == 'folio-ota-v1' and not rollback:
        raise ValueError('OTA image requires bootloader rollback enabled')
    manifest = {'schema':1,'name':'Folio','board':BOARD,'chip':'esp32s3','version':version,
        'project':desc['project'],'idf':desc['idf'],'layout':layout,'flash_size':FLASH_SIZE,
        'profile':profile,'rollback':rollback,'files':files,
        'publication_ready':False,'distribution_status':'local-only',
        'publication_blockers':['Explicit publication approval','Hardware acceptance evidence',
            'Complete license notices and corresponding source','LGPL relinking objects/build materials'],
        'web_flash':profile == 'production' and layout == 'factory-v1',
        'write_policy':'app-only-same-bootloader-and-layout',
        'source':'https://github.com/mrrobot97/Folio',
        'data_preserved':['nvs','phy_init','storage'],
        'notice':'仅同分区应用更新。首次安装/分区迁移需单独确认；未做真机验收不等于可发布。'}
    if output.exists():
        raise ValueError('Output already exists; choose a new directory')
    output.mkdir(parents=True)
    try:
        for name, data in blobs.items():
            (output/name).write_bytes(data)
        (output/'manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2)+'\n')
        if layout == 'folio-ota-v1' and profile == 'production':
            (output/'ota.txt').write_text('\n'.join(['FOLIO-OTA-1',BOARD,'esp32s3',layout,version,
                                                    desc['project'],str(len(app)),sha(app),'']))
        (output/'SHA256SUMS').write_text(''.join(f'{sha(p.read_bytes())}  {p.name}\n'
            for p in sorted(output.iterdir()) if p.is_file()))
    except Exception:
        shutil.rmtree(output)
        raise
    return manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--version',required=True)
    parser.add_argument('--profile',choices=['production','compile-only'],default='compile-only')
    args = parser.parse_args()
    try:
        result = package(args.build,args.output,args.version,args.profile)
    except (ValueError,OSError,KeyError,TypeError,json.JSONDecodeError) as error:
        parser.exit(1, f'Refused: {error}\n')
    print(f"Created local {result['profile']} package: {args.output}; web flash={result['web_flash']}")

if __name__ == '__main__':
    main()
