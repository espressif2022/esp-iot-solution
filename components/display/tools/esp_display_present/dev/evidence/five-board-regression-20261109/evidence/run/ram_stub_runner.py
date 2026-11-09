"""Transport-only retry: current esptool 5.4 supports the S31 RAM stub.
No firmware, IDF, test case, or runtime component changes.
"""
import importlib.util
from pathlib import Path
runner=Path('/tmp/present-five-board-20261109/source/components/display/tools/esp_display_present/dev/tools/run.py')
spec=importlib.util.spec_from_file_location('original_runner',runner)
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
original=m.esptool
def esptool(*args,**kw):
 return [s for s in original(*args,**kw) if s!='--no-stub']
m.esptool=esptool
m.main()
