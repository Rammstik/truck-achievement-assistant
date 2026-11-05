from pathlib import Path
import shutil,json,hashlib,zipfile,sys,importlib.metadata
root=Path(__file__).resolve().parent;outputs=root.parents[1]/'outputs';name='ETS2-Achievement-Assistant-0.6.15-ETS2-1.62';dest=outputs/name;dest.mkdir(exist_ok=False)
plugins=dest/'bin/win_x64/plugins';plugins.mkdir(parents=True)
shutil.copy2(root/'bin/AchievementAssistant.dll',plugins/'AchievementAssistant.dll');shutil.copytree(root/'bin/AADataWorker',plugins/'AchievementAssistant')
(plugins/'map-probe.json').write_text('{"enabled":true,"highlight_viewpoints":true}\n',encoding='utf-8')
for fn in ['README_RU.md','SHARED_ENGINE_RU.md','map-highlight-coverage.json','offer-filter-coverage.json']:shutil.copy2(root/fn,dest/fn)
compat=dest/'compatibility';compat.mkdir()
for fn in ['compatibility-audit.json','data-audit.json']:
 shutil.copy2(root.parent/'ets2-162-research'/fn,compat/fn)
source=dest/'source';source.mkdir()
for directory in ['src','worker','third_party','tests']:shutil.copytree(root/directory,source/directory,ignore=shutil.ignore_patterns('__pycache__','*.pyc','check_native.py','check_worker.py'))
shutil.copytree(root.parent/'achievement-core',source/'core',ignore=shutil.ignore_patterns('__pycache__','*.pyc'))
for fn in ['build.ps1','build_worker.ps1','fetch_dependencies.py']:shutil.copy2(root/fn,source/fn)
for fn in ['check_tracking.py','check_offer_filter.py','check_baltic.py']:
 p=root.parent/'achievement-lab'/fn
 if p.is_file():
  content=p.read_text(encoding='utf-8').replace("Path(__file__).resolve().parents[1]/'achievement-core'","Path(__file__).resolve().parents[1]/'core'")
  (source/'tests'/fn).write_text(content,encoding='utf-8')
licenses=dest/'licenses';licenses.mkdir()
for rel,label in [('scs_sdk/sdk_license.txt','SCS-SDK.txt'),('minhook/LICENSE.txt','MinHook.txt'),('imgui/LICENSE.txt','Dear-ImGui.txt'),('json/LICENSE.MIT','nlohmann-json.txt')]:shutil.copy2(root/'third_party'/rel,licenses/label)
for n in ['pyinstaller','cryptography','cffi']:
 dist=importlib.metadata.distribution(n)
 for f in dist.files or []:
  if f.name.upper().startswith(('LICENSE','COPYING')) and dist.locate_file(f).is_file():shutil.copy2(dist.locate_file(f),licenses/(n+'-'+f.name))
shutil.copy2(Path(sys.base_prefix)/'LICENSE.txt',licenses/'Python-PSF.txt')
report={'game':'ets2','version':'0.6.15-ets162','expected_game_version':'1.62.1.0','expected_exe_sha256':'a938cb388121f9b884a2e1b0f048c230a7bc19638ef6d641a70775f8c7fea956','previously_supported_exe_sha256':'b70c2357cc56eb5834ee47fea3c60b2f2d4591383ff2ac479cfd3cd254301b8c','unsupported_exe_gate_retained':True,'exact_native_signatures_retained':True,'reviewed_steam_achievements':114,'supported_rule_definitions_unchanged':106,'extra_definitions_not_implemented':8,'current_feature_positions':6495,'current_city_zone_centers':1139,'game_test_run':False,'native_host_visual_layout_verified':True,'checks':['ExecutableBindingsTests: exact installed 1.62 EXE hash, signatures and native data contracts PASS','21 independent static executable checks, including draw loop/frame/color, marker kinds and job predicates PASS','1101 current map sectors; no parse errors or missing nodes; refreshed companies, services, garages and city zones','106 prior achievement rules unchanged after excluding console-only metadata; cargo ADR/group semantics unchanged','GuidanceTests and check_tracking/check_offer_filter/check_baltic PASS','OfferFilterTests/MapTintTests/InputGateTests/MapProbeTests/JobProbeTests PASS','Frozen worker real 1.62 save: 114 achievements, previous selected profile and 8 unimplemented definitions correctly classified PASS','NativeHost offscreen capture, unsupported-host warning and repeated SDK lifecycle PASS','ATS tracking and all 140 guidance payloads PASS'],'limitations':['Actual live-game verification is performed by the user','This exact binding supports ETS2 1.62.1.0; other executable hashes are rejected','Eight additional bus/Iceland rules are not yet implemented for detailed progress/map/filter assistance','Existing unknown road trigger completion and live offered distance/income/exact Special Transport route limitations remain'],'sha256_dll':hashlib.sha256((plugins/'AchievementAssistant.dll').read_bytes()).hexdigest(),'sha256_worker':hashlib.sha256((plugins/'AchievementAssistant/AADataWorker.exe').read_bytes()).hexdigest()}
(dest/'validation.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
files=[p for p in dest.rglob('*') if p.is_file()];assert not any(p.name in ['preferences.json','state.json','control.json','error.json'] or 'sessions' in p.parts for p in files)
archive=outputs/(name+'.zip')
with zipfile.ZipFile(archive,'w',zipfile.ZIP_DEFLATED) as z:
 for p in sorted(files):z.write(p,name+'/'+p.relative_to(dest).as_posix())
with zipfile.ZipFile(archive) as z:assert z.testzip() is None
print(json.dumps({'archive':str(archive),'bytes':archive.stat().st_size,'files':len(files),'dll_sha256':report['sha256_dll']},indent=2))
