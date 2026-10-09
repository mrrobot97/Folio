"""Extract exact production reader adapters/rendering for the host integration test.
中文：平台/书源使用fixture，真实绘制与事务适配不复制改写。
English: Fixture platform/book inputs; production rendering/transaction adapters are not rewritten.
"""
import pathlib,re,sys
source=pathlib.Path('firmware/main/apps/app_book.c').read_text()
names=['format_body_rect','body_rect','draw_formatted_reader','folio_reader_format_media_lost','folio_reader_format_retain','folio_reader_format_resume_resources','folio_reader_format_capture','folio_reader_format_abort','folio_reader_format_prepare','folio_reader_format_prepare_error','folio_reader_format_commit','folio_reader_format_return']
def extract(name):
    found=re.search(r'^(?:static )?(?:EpdRect|bool|void|const char\*)\s+'+name+r'\([^\n]*\)\s*\{',source,re.M)
    assert found,name
    pos=found.end();depth=1;mode=None
    while depth:
        ch=source[pos];nxt=source[pos:pos+2]
        if mode=='line':
            if ch=='\n':mode=None
        elif mode=='block':
            if nxt=='*/':mode=None;pos+=1
        elif mode in ('"',"'"):
            if ch=='\\':pos+=1
            elif ch==mode:mode=None
        elif nxt=='//':mode='line';pos+=1
        elif nxt=='/*':mode='block';pos+=1
        elif ch in ('"',"'"):mode=ch
        elif ch=='{':depth+=1
        elif ch=='}':depth-=1
        pos+=1
    return source[found.start():pos]
pathlib.Path(sys.argv[1]).write_text('\n\n'.join(map(extract,names)))
