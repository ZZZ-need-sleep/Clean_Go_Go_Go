import importlib.util
import os
import subprocess
import time

spec = importlib.util.spec_from_file_location("uip", r"tools\ui_probe.py")
uip = importlib.util.module_from_spec(spec)
spec.loader.exec_module(uip)

os.makedirs(r"build\emptydata", exist_ok=True)
p = subprocess.Popen(
    [r"dist\Genshin.exe", "--data-dir", r"build\emptydata"],
    stdin=subprocess.DEVNULL,
    stdout=subprocess.DEVNULL,
    stderr=subprocess.DEVNULL,
)
print("pid", p.pid, flush=True)
dlg = uip.find_dialog(None, 15)
print("nodata", hex(dlg) if dlg else None, repr(uip.window_text(dlg)) if dlg else "", flush=True)
if dlg:
    uip.click_button(dlg, 6)  # Yes
scan_dlg = uip.find_dialog("选择扫描方式", 10)
print("scan dialog", hex(scan_dlg) if scan_dlg else None, flush=True)
if scan_dlg:
    for line in uip.dump_tree(scan_dlg):
        print(line)
    uip.capture(scan_dlg, r"build\ui_scanmode.png")
    uip.click_button(scan_dlg, 2)  # Cancel
main = uip.find_main(10)
print("main", hex(main) if main else None, flush=True)
if main:
    time.sleep(0.5)
    uip.user32.PostMessageW(main, 0x0010, 0, 0)
    time.sleep(0.5)
if p.poll() is None:
    p.terminate()
print("exit", p.wait(timeout=10), flush=True)
