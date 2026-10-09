import test from 'node:test';
import assert from 'node:assert/strict';
import {validateManifest,sha256,verifyFiles} from '../src/manifest.js';
import {checkDevice,writeApplication} from '../src/flasher.js';
const digest='0'.repeat(64);
function fixture(){return {schema:1,name:'Folio',board:'RDP-G01-W',chip:'esp32s3',flash_size:0x1000000,
  version:'0.1.0',source:'https://github.com/mrrobot97/Folio',profile:'production',layout:'factory-v1',web_flash:true,
  write_policy:'app-only-same-bootloader-and-layout',files:[
    {role:'bootloader',file:'bootloader.bin',offset:0,size:100,sha256:digest},
    {role:'partition-table',file:'partition-table.bin',offset:0x8000,size:0xc00,sha256:digest},
    {role:'app',file:'app.bin',offset:0x10000,size:1000,sha256:digest}]};}
test('valid policy manifest admitted',()=>assert.equal(validateManifest(fixture()).version,'0.1.0'));
for(const [name,change] of [
  ['board',m=>m.board='ESP32'],['chip',m=>m.chip='esp32c3'],['schema',m=>m.schema=2],
  ['version',m=>m.version='1.0.0-rc'],['source',m=>m.source='https://attacker.invalid'],
  ['layout',m=>m.layout='unknown'],['unsafe policy',m=>m.write_policy='erase-all'],
  ['path traversal',m=>m.files[0].file='../app.bin'],['data write',m=>m.files[2].offset=0x410000],
  ['oversize',m=>m.files[2].size=0x400001],['duplicate role',m=>m.files[2]=m.files[0]],
  ['corrupt digest',m=>m.files[2].sha256='bad'],['compile-only flashing',m=>m.profile='compile-only']
])test(`reject ${name}`,()=>{const m=fixture();change(m);assert.throws(()=>validateManifest(m));});
test('missing files fail closed',async()=>assert.rejects(verifyFiles(fixture(),[])));
test('corrupt file hash fails',async()=>{const m=fixture();const f={name:'bootloader.bin',size:100,arrayBuffer:async()=>new Uint8Array(100).buffer};await assert.rejects(verifyFiles(m,[f]),/完整性/);});
test('wrong chip never reads flash',async()=>{let reads=0;await assert.rejects(checkDevice({chip:{CHIP_NAME:'ESP32'},readFlash:()=>reads++},fixture()));assert.equal(reads,0);});
test('compile-only never talks to device',async()=>{const m=fixture();m.profile='compile-only';m.web_flash=false;await assert.rejects(checkDevice({},m),/不能网页刷写/);});
async function deviceFixture(){
  const m=fixture(),bytes=new Map();
  for(const f of m.files){const data=new Uint8Array(f.size).fill(f.role==='app'?7:3);f.sha256=await sha256(data);bytes.set(f.role,data);}
  let writes=0;
  const current=new Uint8Array(288);current[0]=0xe9;current.set(new TextEncoder().encode('0.0.9'),48);
  const loader={chip:{CHIP_NAME:'ESP32-S3'},detectFlashSize:async()=> '16MB',
    getSecurityInfo:async()=>({chipId:9,flashCryptCnt:0,parsedFlags:{SECURE_BOOT_EN:false,SECURE_DOWNLOAD_ENABLE:false}}),
    readFlash:async(offset,size)=>{if(offset===0x10000 && size===288)return current;const f=m.files.find(f=>f.offset===offset);return bytes.get(f.role).slice(0,size);},
    writeFlash:async(options)=>{writes++;assert.equal(options.eraseAll,false);assert.equal(options.fileArray.length,1);assert.equal(options.fileArray[0].address,0x10000);}};
  return {m,bytes,loader,writes:()=>writes};
}
test('mismatched layout never writes',async()=>{const t=await deviceFixture();t.loader.readFlash=async()=>new Uint8Array(0xc00);await assert.rejects(writeApplication(t.loader,t.m,t.bytes,()=>{}));assert.equal(t.writes(),0);});
test('secure boot never writes',async()=>{const t=await deviceFixture();t.loader.getSecurityInfo=async()=>({chipId:9,flashCryptCnt:0,parsedFlags:{SECURE_BOOT_EN:true}});await assert.rejects(writeApplication(t.loader,t.m,t.bytes,()=>{}));assert.equal(t.writes(),0);});
test('app-only write with readback verification',async()=>{const t=await deviceFixture();await writeApplication(t.loader,t.m,t.bytes,()=>{});assert.equal(t.writes(),1);});
test('bad readback is not success',async()=>{const t=await deviceFixture();const read=t.loader.readFlash;t.loader.readFlash=async(offset,size)=>offset===0x10000 && size!==288?new Uint8Array(size):read(offset,size);await assert.rejects(writeApplication(t.loader,t.m,t.bytes,()=>{}),/回读校验失败/);});

test('device version downgrade is refused',async()=>{const t=await deviceFixture();const read=t.loader.readFlash;t.loader.readFlash=async(offset,size)=>{if(offset===0x10000 && size===288){const b=new Uint8Array(288);b[0]=0xe9;b.set(new TextEncoder().encode('9.0.0'),48);return b;}return read(offset,size);};await assert.rejects(writeApplication(t.loader,t.m,t.bytes,()=>{}),/降级/);assert.equal(t.writes(),0);});
