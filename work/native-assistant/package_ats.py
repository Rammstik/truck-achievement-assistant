from pathlib import Path
import json,hashlib,zipfile,shutil,sys,importlib.metadata
root=Path(__file__).resolve().parent;output=root.parents[1]/'outputs';name='ATS-Achievement-Assistant-0.1.0';dest=output/name;dest.mkdir(exist_ok=False)
plugins=dest/'bin/win_x64/plugins';plugins.mkdir(parents=True)
shutil.copy2(root/'bin-ats/AchievementAssistant.dll',plugins/'AchievementAssistant.dll');shutil.copytree(root/'bin-ats/AchievementAssistant',plugins/'AchievementAssistant')
(plugins/'map-probe.json').write_text('{"enabled":true,"highlight_viewpoints":true}\n',encoding='utf-8')
for original,target in [('README_ATS_RU.md','README_RU.md'),('SHARED_ENGINE_RU.md','SHARED_ENGINE_RU.md'),('ats-coverage.json','ats-coverage.json'),('ats-executable.json','ats-executable.json')]:shutil.copy2(root/original,dest/target)
source=dest/'source';source.mkdir()
for directory in ['src','worker','third_party','tests']:
 shutil.copytree(root/directory,source/directory,ignore=shutil.ignore_patterns('__pycache__','*.pyc','check_native.py','check_worker.py'))
shutil.copytree(root.parent/'achievement-core',source/'core',ignore=shutil.ignore_patterns('__pycache__','*.pyc','check_native.py','check_worker.py'))
for f in ['build.ps1','build_worker.ps1','fetch_dependencies.py','SHARED_ENGINE_RU.md']:shutil.copy2(root/f,source/f)
licenses=dest/'licenses';licenses.mkdir()
for rel,label in [('scs_sdk/sdk_license.txt','SCS-SDK.txt'),('minhook/LICENSE.txt','MinHook.txt'),('imgui/LICENSE.txt','Dear-ImGui.txt'),('json/LICENSE.MIT','nlohmann-json.txt')]:shutil.copy2(root/'third_party'/rel,licenses/label)
for n in ['pyinstaller','cryptography','cffi']:
 dist=importlib.metadata.distribution(n)
 for file in dist.files or []:
  if file.name.upper().startswith(('LICENSE','COPYING')) and dist.locate_file(file).is_file():shutil.copy2(dist.locate_file(file),licenses/(n+'-'+file.name))
shutil.copy2(Path(sys.base_prefix)/'LICENSE.txt',licenses/'Python-PSF.txt')
report={'game':'ats','version':'0.1.0','game_version':'1.61.3.18','shared_ets2_build':'0.6.13-common','expected_exe_sha256':json.loads((root/'ats-executable.json').read_text())['sha256'],'catalogue_goals':140,'delivery_filter_goals':55,'optional_city_filter_goals':21,'viewpoint_groups':13,'road_trigger_candidate_groups':23,'game_test_run':False,'visual_layout_verified_in_native_host':True,'verification':['ATSCoreTests all 140 plans PASS','ATSMapTintTests target mapping, red ASM/alpha, removal/isolation PASS','ATS/ETS JobProbeTests ordinary/Special Transport, refresh, native eligibility PASS','ATS/ETS MapProbeTests synthetic hook ABI, bounds and unknown executable rejection PASS','ATS/ETS InputGateTests PASS','ETS OfferFilterTests/MapTintTests regression PASS','check_tracking.py/check_offer_filter.py/check_baltic.py ETS PASS','check_ats_tracking.py PASS','frozen worker ATS 140 achievements + real profile details PASS','frozen worker ETS 114 achievements + unchanged previous profile ID PASS','ATS NativeHost offscreen capture + wrong-game rejection + SDK init/shutdown repeated PASS'],'limitations':['Live hooks and each ATS market need in-game verification','Profile progress updates after game saves; Steam is account-wide','Unknown road trigger completion; academy/car detailed history unavailable','Live offered distance/income and exact Special Transport route ID unavailable; perf_job/st_all_routes unsupported'],'sha256_dll':hashlib.sha256((plugins/'AchievementAssistant.dll').read_bytes()).hexdigest(),'sha256_worker':hashlib.sha256((plugins/'AchievementAssistant/AADataWorker.exe').read_bytes()).hexdigest()}
(dest/'validation.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
files=[p for p in dest.rglob('*') if p.is_file()];assert not any(p.name in ('state.json','preferences.json','control.json') or 'sessions' in p.parts for p in files)
archive=output/(name+'.zip')
with zipfile.ZipFile(archive,'w',zipfile.ZIP_DEFLATED) as z:
 for f in sorted(files):z.write(f,name+'/'+f.relative_to(dest).as_posix())
with zipfile.ZipFile(archive) as z:assert z.testzip() is None
print(json.dumps({'archive':str(archive),'bytes':archive.stat().st_size,'files':len(files),'dll_sha256':report['sha256_dll']},indent=2))
