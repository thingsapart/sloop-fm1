#!/usr/bin/env python3
"""Read-only upstream dependencies; checked generated overlays, pinned by content."""
from pathlib import Path
import hashlib,json,os,shutil,subprocess,sys
PORT=Path(__file__).resolve().parents[1]
SLOOP=Path(os.environ.get('SLOOP_SOURCE',PORT.parents[1])).expanduser().resolve()
KW=Path(os.environ.get('KLICKWERK_SOURCE',SLOOP.parents[1]/'klickwerk2')).expanduser().resolve()
OUT=PORT/'build'
OUT.mkdir(exist_ok=True)
lockpath=PORT/'sources.lock.json'
def fingerprint(root,paths):
    return {str(p.relative_to(root)):hashlib.sha256(p.read_bytes()).hexdigest()
            for path in paths for p in ([root/path] if (root/path).is_file() else sorted((root/path).rglob('*')))
            if p.is_file() and '__pycache__' not in p.parts}
inputs={'sloop':fingerprint(SLOOP,['firmware/src','firmware/hal','tools','assets']),
        'klickwerk':fingerprint(KW,['firmware/p4/src','firmware/p4/components/lvgl','firmware/p4/tools/patch_uac.py',
          'firmware/p4/tools/patch_lvgl_yield.py','firmware/p4/tools/patch_usb_descriptors.py',
          'firmware/display/contrib/lvgl_ui/lvgl','firmware/shared/ring_buffer.h','firmware/p4/dependencies.lock'])}
if '--update-lock' in sys.argv:
    for key,root in [('sloop',SLOOP),('klickwerk',KW)]:
        inputs[key+'_commit']=subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip()
    lockpath.write_text(json.dumps(inputs,indent=2)+'\n')
elif not lockpath.exists():raise SystemExit('Missing sources.lock.json; review sources then run prepare.py --update-lock')
else:
    lock=json.loads(lockpath.read_text())
    for key in ('sloop','klickwerk'):
        if inputs[key]!=lock[key]:raise SystemExit(key+' inputs changed. Review the overlay, update the lock and run tests before building.')
gen=OUT/'gen';gen.mkdir(exist_ok=True)
# Cache by source content, never by directory timestamp.
digest=hashlib.sha256(json.dumps(inputs['sloop'],sort_keys=True).encode()+Path(__file__).read_bytes()+str(sys.version_info[:2]).encode()).hexdigest()
if not (OUT/'assets.digest').exists() or (OUT/'assets.digest').read_text()!=digest:
    # Upstream generators cache beside their script. Stage them so the dependency
    # checkout remains read-only, including its build directory.
    workspace=OUT/'asset_source'
    if workspace.exists():shutil.rmtree(workspace)
    workspace.mkdir()
    for folder in ('tools','assets'):
        shutil.copytree(SLOOP/folder,workspace/folder,ignore=shutil.ignore_patterns('__pycache__'))
    generator_env=dict(os.environ,FELUCCA_SLICE='0')
    for name,header in [('font','felucca_font'),('icons','felucca_icons'),('tables','felucca_tables'),
                        ('samples','felucca_samples'),('drumkits','felucca_drumkits'),('fm6_patches','felucca_fm6'),('logo','sloop_logo')]:
        subprocess.run([sys.executable,str(workspace/'tools'/('gen_'+name+'.py')),str(gen/(header+'.h'))],cwd=workspace,env=generator_env,check=True)
    target=gen/'sloop_firmware'
    if target.exists():shutil.rmtree(target)
    shutil.copytree(SLOOP/'firmware/src',target)
    hal=gen/'hal'
    if hal.exists():shutil.rmtree(hal)
    shutil.copytree(SLOOP/'firmware/hal',hal)
    replacements={
      'eng_sample.c': [('base = (uint32_t)(uintptr_t)(smp_user_xip(k) + SMP_USER_DATA) - (uint32_t)(uintptr_t)SMP_DATA;',
                        'base = 0x80000000u | (k << 24);'),
                       ('SMP_DATA[z->off + (pos >> 1)]','host_sample_byte(z->off + (pos >> 1))')],
      'eng_grain.c':[('SMP_DATA[z->off + (pos >> 1)]','host_sample_byte(z->off + (pos >> 1))')],
      # Read-only observation before lazy rendering checks. Musical behaviour is unchanged.
      'ui_draw.c':[('    char l[8], v[8], u[8], key[32];',
                    '    host_knob_info(c, label, val, unit);\n    char l[8], v[8], u[8], key[32];')],
      'ui_studio.c':[('    sig = studio_hash(sig, ui.msg_t ? ui.msg : "");',
                      '    for (unsigned h = 0; h < 4; ++h) host_knob_info(h, lab[h], val[h], "");\n    sig = studio_hash(sig, ui.msg_t ? ui.msg : "");')]}
    for name,edits in replacements.items():
        p=target/name;s=p.read_text()
        for old,new in edits:
            if s.count(old)!=1:raise SystemExit(name+': checked overlay no longer matches upstream')
            s=s.replace(old,new)
        p.write_text(s)
    # FM-1 pool sections are large zero-initialized state, not valid IDF placement.
    for p in target.glob('*.c'):
        s=p.read_text();p.write_text(s.replace('section(".pool")','section(".ext_ram.bss")'))
    (OUT/'assets.digest').write_text(digest)
(OUT/'paths.cmake').write_text('set(SLOOP_ROOT "'+str(SLOOP)+'")\nset(KW_ROOT "'+str(KW)+'")\n')
print('SLOOP and Klickwerk inputs verified; generated port overlay ready')
