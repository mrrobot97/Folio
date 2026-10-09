import { mkdir, cp, readFile, writeFile, access, rm } from 'node:fs/promises';
import { fileURLToPath } from 'node:url';
import path from 'node:path';
const root = path.dirname(fileURLToPath(import.meta.url));
const output = path.join(root, 'dist');
await mkdir(output, { recursive: true });
await rm(path.join(output,'vendor'),{recursive:true,force:true});
for (const name of ['index.html', 'style.css', 'src', 'assets']) await cp(path.join(root,name),path.join(output,name),{recursive:true});
let driver = false;
const module = path.join(root,'node_modules/esptool-js');
try {
  await access(path.join(module,'bundle.js'));
  driver = true;
} catch (e) {
  if (e.code !== 'ENOENT') throw e;
}
if (driver) {
  const pkg = JSON.parse(await readFile(path.join(module,'package.json'),'utf8'));
  if (pkg.version !== '0.7.0') throw Error('Unexpected esptool-js version');
  await mkdir(path.join(output,'vendor'),{recursive:true});
  await cp(path.join(module,'bundle.js'),path.join(output,'vendor/esptool-js.js'));
  await cp(path.join(module,'LICENSE'),path.join(output,'vendor/esptool-js.LICENSE'));
  for (const [name,license] of [['atob-lite','LICENSE.md'],['pako','LICENSE']]) {
    await cp(path.join(root,'node_modules',name,license),path.join(output,`vendor/${name}.LICENSE`));
  }
}
await writeFile(path.join(output,'driver-status.json'),JSON.stringify({available:driver,version:'0.7.0'}));
await writeFile(path.join(output,'.nojekyll'),'');
console.log(`Built ${output}. Serial driver ${driver ? 'bundled' : 'not installed; connection disabled'}. No firmware bundled.`);
