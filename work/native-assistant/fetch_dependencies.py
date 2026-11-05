from pathlib import Path
from concurrent.futures import ThreadPoolExecutor
import urllib.request,hashlib,json
root=Path(__file__).parent/'third_party'
files={}
for name in ['LICENSE.txt','imconfig.h','imgui.h','imgui.cpp','imgui_draw.cpp','imgui_tables.cpp','imgui_widgets.cpp','imgui_internal.h','imstb_rectpack.h','imstb_textedit.h','imstb_truetype.h','backends/imgui_impl_dx11.cpp','backends/imgui_impl_dx11.h','backends/imgui_impl_win32.cpp','backends/imgui_impl_win32.h']:
    files['imgui/'+name]='https://raw.githubusercontent.com/ocornut/imgui/v1.91.9b/'+name
files['json/json.hpp']='https://raw.githubusercontent.com/nlohmann/json/v3.12.0/single_include/nlohmann/json.hpp'
files['json/LICENSE.MIT']='https://raw.githubusercontent.com/nlohmann/json/v3.12.0/LICENSE.MIT'
def fetch(item):
    name,url=item;data=urllib.request.urlopen(url,timeout=45).read()
    sha=hashlib.sha256(data).hexdigest()
    if name=='json/json.hpp' and sha!='aaf127c04cb31c406e5b04a63f1ae89369fccde6d8fa7cdda1ed4f32dfc5de63':raise ValueError('JSON header checksum mismatch')
    dest=root/name;dest.parent.mkdir(parents=True,exist_ok=True);dest.write_bytes(data)
    return {'file':name,'url':url,'sha256':sha}
with ThreadPoolExecutor(max_workers=6) as pool:report=list(pool.map(fetch,files.items()))
(root/'downloads.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
print('Downloaded and hashed',len(report),'files')
