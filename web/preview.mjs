import { createServer } from 'node:http';
import { readFile, stat } from 'node:fs/promises';
import { fileURLToPath } from 'node:url';
import path from 'node:path';
const root = path.join(path.dirname(fileURLToPath(import.meta.url)),'dist');
const port = Number(process.env.FOLIO_PREVIEW_PORT || 4173);
const mime = {'.html':'text/html; charset=utf-8','.js':'text/javascript; charset=utf-8','.css':'text/css; charset=utf-8','.json':'application/json','.png':'image/png'};
createServer(async (req,res) => {
  try {
    const pathname = decodeURIComponent(new URL(req.url,'http://127.0.0.1').pathname);
    let file = path.resolve(root,'.'+pathname);
    if (!file.startsWith(root+path.sep) && file !== root) throw Error('Path');
    if ((await stat(file)).isDirectory()) file = path.join(file,'index.html');
    const body = await readFile(file);
    res.writeHead(200, {'Content-Type':mime[path.extname(file)] || 'application/octet-stream','Cache-Control':'no-store','X-Content-Type-Options':'nosniff'});
    res.end(body);
  } catch { res.writeHead(404);res.end('Not found'); }
}).listen(port,'127.0.0.1',() => console.log(`Folio preview http://127.0.0.1:${port}`));
