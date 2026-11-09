from pathlib import Path
import hashlib,json,shutil,gzip,datetime
ROOT=Path('/tmp/present-five-board-20261109')
DELIVERY=Path('/home/xuxin/xuxin/esp-dev-kits-gitlab-MR/internal/customer/xiaobai/deliveries/present-adapter-five-board-20261109')
EVIDENCE=DELIVERY/'evidence'
BASES={b:ROOT for b in ['s3-rgb','p4-dsi','box3-spi']};BASES.update({'s31-rgb':ROOT/'rgb-retry','s31-qspi':ROOT/'te-retry'})
validation=json.loads((DELIVERY/'validation.json').read_text());assert validation['all_restored_verified']
def copy(src,dst):
 assert src.is_file(),src
 dst=EVIDENCE/dst;dst.parent.mkdir(parents=True,exist_ok=True)
 if src.suffix=='.log':
  with dst.with_suffix(dst.suffix+'.gz').open('wb') as f:
   with gzip.GzipFile(filename='',mode='wb',fileobj=f,mtime=0) as g:g.write(src.read_bytes())
 else:shutil.copy2(src,dst)
for b,base in BASES.items():
 restore=json.loads((base/b/'backup/restored.json').read_text());assert restore['verified_full_flash']
 for impl in ['present','adapter']:
  path=base/b/'benchmark'/impl
  for name in ['manifest.json','results.json','bench_cases.h','sdkconfig','live.log','build.log']:copy(path/name,Path(b)/impl/name)
 for name in ['manifest.json','restored.json','restore.log','restore-verify.log']:copy(base/b/'backup'/name,Path(b)/'restore'/name)
 # Retain each board's final attempt; the unfiltered initial controller log
 # below also preserves the two pre-flash transport retries.
 for name in ['events.jsonl','stage.json']:
  src=base/name;dst=EVIDENCE/b/name
  if not src.exists():continue
  dst.parent.mkdir(parents=True,exist_ok=True)
  if name=='stage.json':dst.write_text(json.dumps({b:json.loads(src.read_text())[b]},indent=2)+'\n')
  else:dst.write_text(''.join(line+'\n' for line in src.read_text().splitlines() if json.loads(line).get('board')==b))
 if b!='s31-qspi':
  for name in ['comparison.csv','comparison.md']:copy(base/b/'benchmark'/name,Path(b)/name)
for name in ['revision.json','completion-consolidated.json','matrix-plan.json','pinned-sources.json','source-validation.json','policy-validation.json','matrix-policy.patch','runner-policy-tests.log','host.log','capture-guard.log','s3-original-boot.log','controller.log','ram_stub_runner.py','te-retry-controller.log','rgb-retry-controller.log']:
 copy(ROOT/name,Path('run')/name)
copy(ROOT/'s31-qspi-benchmark-present-backup.log',Path('run/initial-te-rom-read-failed.log'))
copy(ROOT/'s31-rgb-benchmark-present-backup.log',Path('run/initial-rgb-rom-read-stopped.log'))
for path in ['/tmp/run_present_five_board_20261109.py','/tmp/report_present_five_board_20261109.py','/tmp/archive_present_five_board_20261109.py','/tmp/check_present_five_board_browser.mjs','/tmp/validate_present_five_board_files.py','/tmp/present_five_board_capture_guard.py']:
 copy(Path(path),Path('scripts')/Path(path).name)
dev=ROOT/'source/components/display/tools/esp_display_present/dev'
for directory in ['boards','test_apps/benchmark/main','test_apps/benchmark/components/hw_init','test_apps/boards/main','tools']:
 for src in (dev/directory).rglob('*'):
  if src.is_file() and src.suffix in ['.c','.h','.txt','.json','.defaults','.py'] or src.is_file() and src.name.endswith('.defaults'):
   copy(src,Path('test-inputs')/src.relative_to(dev))
# Production inputs are immutable archived HEAD sources; source fingerprints retained.
(EVIDENCE/'SHA256SUMS').write_text(''.join(f'{hashlib.sha256(p.read_bytes()).hexdigest()}  {p.relative_to(EVIDENCE)}\n' for p in sorted(EVIDENCE.rglob('*')) if p.is_file() and p.name!='SHA256SUMS'))
(DELIVERY/'SHA256SUMS').write_text(''.join(f'{hashlib.sha256(p.read_bytes()).hexdigest()}  {p.relative_to(DELIVERY)}\n' for p in sorted(DELIVERY.rglob('*')) if p.is_file() and p!=DELIVERY/'SHA256SUMS'))
assert not any(p.suffix in ['.bin','.elf','.o','.a'] for p in DELIVERY.rglob('*'))
print('Evidence archived:',sum(p.is_file() for p in EVIDENCE.rglob('*')))
