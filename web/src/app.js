import { validateManifest, verifyFiles, sha256 } from './manifest.js';
import { checkDevice, writeApplication } from './flasher.js';
const $ = id => document.getElementById(id);
let manifest, bytes, loader, transport, driver, busy = false, generation = 0, written = false;
const supported = isSecureContext && 'serial' in navigator && !!crypto.subtle;
const show = (text,error=false) => { $('message').textContent=text;$('message').classList.toggle('error',error); };
const log = text => { $('log').textContent += text+'\n'; };
function controls() {
  const ready=!!manifest?.web_flash && !!bytes && !!driver && supported;
  $('connect').disabled=busy || !!loader || !ready || !$('board-confirm').checked || !$('power-confirm').checked;
  $('disconnect').disabled=busy || !loader;
  $('write-confirm').disabled=busy || !loader || written;
  $('flash').disabled=busy || !loader || written || !$('write-confirm').checked;
  $('reboot').disabled=busy || !loader || !written;
  for(const id of ['files','board-confirm','power-confirm']) $(id).disabled=busy || !!loader;
}
$('browser-state').textContent=supported?'浏览器支持 Web Serial。连接前请关闭占用串口的软件。':'当前浏览器或连接不支持 Web Serial。请使用桌面 Chrome / Edge 和 HTTPS 或 localhost。';
try {
  const response=await fetch('./driver-status.json');
  if(!response.ok) throw Error('build required');
  const status=await response.json();
  if(status.available && status.version==='0.7.0') {
    driver=await import('../vendor/esptool-js.js');
    $('driver-state').textContent='已加载随站点打包的 esptool-js 0.7.0。';
  } else $('driver-state').textContent='本地刷写驱动尚未安装，当前仅可校验发布包。';
} catch { $('driver-state').textContent='刷写驱动不可用，连接与写入已禁用。请先完成站点构建。'; }
controls();
$('files').addEventListener('change', async event=>{
  const current=++generation;
  manifest=bytes=null;written=false;$('write-confirm').checked=false;$('package-detail').hidden=true;
  $('ready-state').textContent='校验中';controls();
  try {
    const files=Array.from(event.target.files);
    const found=files.filter(f=>f.name==='manifest.json');
    if(found.length!==1 || found[0].size>32768) throw Error('请选择一个有效的 manifest.json（不超过 32 KiB）。');
    const candidate=validateManifest(JSON.parse(await found[0].text()));
    const verified=await verifyFiles(candidate,files);
    const hash=await sha256(new Uint8Array(await found[0].arrayBuffer()));
    if(current!==generation) return;
    manifest=candidate;bytes=verified;
    $('package-state').textContent=`Folio ${manifest.version} · 文件完整性校验通过。`;
    $('package-detail').hidden=false;
    $('package-detail').textContent=`板型 ${manifest.board} · ${manifest.layout} · ${manifest.profile}\n清单 SHA-256：${hash}`;
    $('ready-state').textContent=manifest.web_flash?'发布包已校验':'仅供检查';
    show(manifest.web_flash?'文件已校验。请核对来源、板型和电源，再连接设备。':'此包为编译测试或 OTA 双槽布局；网页写入已禁用。');
  } catch(error) {
    if(current!==generation) return;
    $('package-state').textContent='发布包未通过校验，不可连接或写入。';
    $('ready-state').textContent='校验失败';show(error.message,true);
  }
  controls();
});
for(const id of ['board-confirm','power-confirm','write-confirm']) $(id).addEventListener('change',controls);
async function disconnect() {
  try { if(transport) await transport.disconnect(); }
  finally {loader=transport=null;$('write-confirm').checked=false;controls();}
}
$('connect').addEventListener('click',async()=>{
  if($('connect').disabled) return;
  busy=true;controls();
  try {
    const port=await navigator.serial.requestPort({filters:[{usbVendorId:0x303a,usbProductId:0x1001}]});
    transport=new driver.Transport(port);
    loader=new driver.ESPLoader({transport,baudrate:115200,terminal:{clean:()=>{},writeLine:log,write:log}});
    show('正在连接；设备可能重启进入下载模式。尚未写入 Flash。');
    await loader.main();
    await checkDevice(loader,manifest);
    $('device-state').textContent='芯片、容量、安全状态、现有引导与分区均匹配。仅将更新应用区。';
    show('设备校验通过。勾选写入确认后，才能开始更新。');
  } catch(error) {show(error.message,true);await disconnect();}
  finally {busy=false;controls();}
});
$('disconnect').addEventListener('click',async()=>{
  busy=true;controls();
  try {await disconnect();show('已断开。设备可能仍停留在下载模式；可按设备恢复说明重新启动。');}
  catch(error){show(error.message,true);}finally{busy=false;controls();}
});
$('flash').addEventListener('click',async()=>{
  if($('flash').disabled) return;
  busy=true;controls();
  show('正在写入应用，随后回读校验。请保持页面和 USB 连接。');
  try {
    await writeApplication(loader,manifest,bytes,(n,total)=>{$('progress').value=n*100/total;});
    written=true;$('device-state').textContent='应用写入与 SHA-256 回读校验通过；尚未验证真机启动。';
    show('写入完成。点击“重启设备”后，请检查屏幕、触摸、书库和阅读进度。');
  } catch(error) {show(error.message,true);log('写入未确认成功。请保留数据，按项目串口恢复流程处理。');}
  finally {busy=false;controls();}
});
$('reboot').addEventListener('click',async()=>{
  if($('reboot').disabled) return;
  busy=true;controls();
  try {await loader.after('hard_reset');await disconnect();show('已发送重启指令。请在设备上完成启动与数据检查。');}
  catch(error){show(error.message,true);}finally{busy=false;controls();}
});
window.addEventListener('beforeunload',event=>{if(busy){event.preventDefault();event.returnValue='';}});
