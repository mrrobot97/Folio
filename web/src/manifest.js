// 发布数据视为不可信，白名单检查再读取。/ Treat release input as untrusted; validate before reading.
export const BOARD = 'RDP-G01-W';
export const FLASH_SIZE = 0x1000000;
export function validateManifest(m) {
  const fail = message => {throw Error(message);};
  if (!m || m.schema !== 1 || m.name !== 'Folio') fail('不支持的清单格式。');
  if (m.board !== BOARD || m.chip !== 'esp32s3' || m.flash_size !== FLASH_SIZE) fail('发布包的板型或容量不匹配。');
  if (!/^(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)$/.test(m.version) || m.version.split('.').some(p=>Number(p)>65535)) fail('版本号无效。');
  if (m.source !== 'https://github.com/mrrobot97/Folio') fail('发布包来源字段不匹配；请从项目发布页取得文件。');
  if (!['factory-v1','folio-ota-v1'].includes(m.layout)) fail('未知分区布局。');
  if (!['production','compile-only'].includes(m.profile)) fail('缺少构建用途。');
  if (m.write_policy !== 'app-only-same-bootloader-and-layout') fail('不支持此写入策略。');
  if (m.web_flash !== (m.profile==='production' && m.layout==='factory-v1')) fail('刷写标志与构建用途不一致。');
  if (!Array.isArray(m.files) || m.files.length !== 3) fail('发布包必须包含引导、分区和应用三项。');
  const expected = {bootloader:[0,0x8000], 'partition-table':[0x8000,0xC00],app:[0x10000,0x400000]};
  const seen = new Set();
  for (const file of m.files) {
    const rule = expected[file.role];
    if (!rule || seen.has(file.role) || file.file !== file.role+'.bin') fail('重复或未知的文件角色。');
    seen.add(file.role);
    if (!Number.isSafeInteger(file.offset) || file.offset !== rule[0] || !Number.isSafeInteger(file.size) || file.size < 1 || file.size > rule[1]) fail('发布包地址或容量超出允许范围。');
    if (file.role === 'partition-table' && file.size !== 0xC00) fail('分区表长度无效。');
    if (!/^[a-f0-9]{64}$/.test(file.sha256)) fail('SHA-256 格式无效。');
  }
  return m;
}
export async function sha256(bytes) {
  const result = await crypto.subtle.digest('SHA-256',bytes);
  return Array.from(new Uint8Array(result),b=>b.toString(16).padStart(2,'0')).join('');
}
export async function verifyFiles(manifest, files) {
  validateManifest(manifest);
  const bytes = new Map();
  const byName = new Map();
  for (const file of files) {
    if (byName.has(file.name)) throw Error('存在重名文件，请只选择一个发布目录中的文件。');
    byName.set(file.name,file);
  }
  for (const entry of manifest.files) {
    const file = byName.get(entry.file);
    if (!file || file.size !== entry.size) throw Error(`${entry.file} 缺失或长度不匹配。`);
    const data = new Uint8Array(await file.arrayBuffer());
    if (await sha256(data) !== entry.sha256) throw Error(`${entry.file} 完整性校验失败。`);
    bytes.set(entry.role,data);
  }
  // 独立验证应用芯片、嵌入版本和分区结构，不只依赖 JSON 中的标签。
  // Independently inspect image chip/version and partition structure, not only JSON labels.
  const app = bytes.get('app');
  const view = new DataView(app.buffer,app.byteOffset,app.byteLength);
  if (app.length < 288 || app[0] !== 0xe9 || view.getUint16(12,true)!==9 || view.getUint32(32,true)!==0xabcd5432) throw Error('应用不是有效的 ESP32-S3 镜像。');
  const version = new TextDecoder().decode(app.slice(48,80)).split('\0')[0];
  if (version !== manifest.version) throw Error('镜像内版本与清单不一致。');
  if (app[1]<1 || app[1]>16 || app[23]!==1) throw Error('镜像段数或摘要标志无效。');
  let cursor=24, checksum=0xef;
  for(let segment=0;segment<app[1];segment++) {
    if(cursor+8>app.length) throw Error('镜像段头被截断。');
    const size=view.getUint32(cursor+4,true);cursor+=8;
    if(size>app.length-cursor) throw Error('镜像段被截断。');
    for(let i=cursor;i<cursor+size;i++) checksum^=app[i];
    cursor+=size;
  }
  const checksumAt=Math.floor(cursor/16)*16+15;
  if(checksumAt>=app.length || app[checksumAt]!==checksum || checksumAt+33!==app.length)
    throw Error('镜像校验或长度无效；签名镜像需单独审核。');
  const expected=Array.from(app.slice(checksumAt+1),b=>b.toString(16).padStart(2,'0')).join('');
  if(await sha256(app.slice(0,checksumAt+1))!==expected) throw Error('镜像内嵌 SHA-256 校验失败。');
  const table = bytes.get('partition-table');
  const partitions = [];
  for (let i=0; i<table.length; i+=32) {
    const row = new DataView(table.buffer,table.byteOffset+i,32);
    if (row.getUint16(0,true)===0xebeb) break;
    if (row.getUint16(0,true)!==0x50aa) throw Error('分区表记录无效。');
    partitions.push([row.getUint8(2),row.getUint8(3),row.getUint32(4,true),row.getUint32(8,true),
      new TextDecoder().decode(table.slice(i+12,i+28)).split('\0')[0],row.getUint32(28,true)]);
  }
  const common = [[1,2,0x9000,0x5000,'nvs',0],[1,1,0xe000,0x1000,'phy_init',0]];
  const factory = [...common,[0,0,0x10000,0x400000,'factory',0],[1,0x81,0x410000,0x500000,'storage',0]];
  const ota = [...common,[0,0x10,0x10000,0x400000,'ota_0',0],[1,0x81,0x410000,0x500000,'storage',0],[0,0x11,0x910000,0x400000,'ota_1',0],[1,0,0xd10000,0x2000,'otadata',0]];
  if (JSON.stringify(partitions)!==JSON.stringify(manifest.layout==='factory-v1'?factory:ota)) throw Error('分区表内容与清单布局不一致。');
  return bytes;
}
