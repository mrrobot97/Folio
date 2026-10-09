import { sha256, validateManifest } from './manifest.js';

export async function checkDevice(loader, manifest) {
  validateManifest(manifest);
  if (!manifest.web_flash) throw Error('此包不能网页刷写；OTA 分区迁移和编译测试包均被阻止。');
  if (loader.chip?.CHIP_NAME !== 'ESP32-S3') throw Error('连接的芯片不是 ESP32-S3。');
  if (await loader.detectFlashSize() !== '16MB') throw Error('设备 Flash 容量不匹配。');
  const security = await loader.getSecurityInfo();
  if (!security || security.chipId !== 9 || security.flashCryptCnt !== 0 ||
      security.parsedFlags?.SECURE_BOOT_EN !== false || security.parsedFlags?.SECURE_DOWNLOAD_ENABLE !== false) {
    throw Error('设备安全配置不可用于此流程；不会修改密钥、安全熔丝或加密状态。');
  }
  for (const role of ['partition-table','bootloader']) {
    const file = manifest.files.find(f=>f.role===role);
    const bytes = await loader.readFlash(file.offset,file.size);
    if (bytes.length !== file.size || await sha256(bytes) !== file.sha256) throw Error(`${role} 与设备不一致。需要单独审核首次安装或迁移，已阻止写入。`);
  }
  const app=manifest.files.find(f=>f.role==='app');
  const current=await loader.readFlash(app.offset,288);
  if(current.length!==288 || current[0]!==0xe9) throw Error('无法识别当前应用版本；请单独审核首次安装或恢复。');
  const version=new TextDecoder().decode(current.slice(48,80)).split('\0')[0];
  if(!/^(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)$/.test(version) || version.split('.').some(p=>Number(p)>65535))
    throw Error('当前应用没有可比较的版本号；请单独审核首次安装或恢复。');
  const before=version.split('.').map(Number),after=manifest.version.split('.').map(Number);
  for(let i=0;i<3;i++) {
    if(after[i]>before[i]) break;
    if(after[i]<before[i]) throw Error('目标版本低于设备当前版本，已阻止降级。');
  }
  return {version};
}

export async function writeApplication(loader, manifest, bytes, reportProgress) {
  // 写入前再次读取设备，防止连接后替换设备或清单。/ Recheck device immediately before writing.
  await checkDevice(loader,manifest);
  const entry = manifest.files.find(f=>f.role==='app');
  const data = bytes.get('app');
  if (!data || data.length !== entry.size || await sha256(data) !== entry.sha256) throw Error('写入前镜像复核失败。');
  await loader.writeFlash({fileArray:[{data,address:entry.offset}],flashSize:'keep',flashMode:'keep',
    flashFreq:'keep',eraseAll:false,compress:true,reportProgress:(_,written,total)=>reportProgress(written,total)});
  // 分块回读，避免 readFlash 内部逐包拼接引起大量内存复制。/ Chunked readback bounds readFlash copying.
  const readback = new Uint8Array(data.length);
  for (let offset=0;offset<data.length;offset+=65536) {
    const size=Math.min(65536,data.length-offset);
    const block=await loader.readFlash(entry.offset+offset,size);
    if (block.length!==size) throw Error('回读长度不匹配；请保留串口连接并执行恢复流程。');
    readback.set(block,offset);
  }
  if (await sha256(readback)!==entry.sha256) throw Error('写入回读校验失败；请保留串口连接并执行恢复流程。');
}
