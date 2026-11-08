import importlib.util
import os
import subprocess
import time

spec = importlib.util.spec_from_file_location("uip", r"tools\ui_probe.py")
uip = importlib.util.module_from_spec(spec)
spec.loader.exec_module(uip)

p = subprocess.Popen(
    [r"dist\Genshin.exe"],
    stdin=subprocess.DEVNULL,
    stdout=subprocess.DEVNULL,
    stderr=subprocess.DEVNULL,
)
print("pid", p.pid, flush=True)
dlg = uip.find_dialog(None, 15)
print("dialog", hex(dlg) if dlg else None, flush=True)
if dlg:
    print("title", repr(uip.window_text(dlg)), flush=True)
    print("children", [repr(uip.window_text(c)) for c in uip.enum_windows(dlg)], flush=True)
    uip.click_button(dlg, 7)  # No
main = uip.find_main(15)
print("main", hex(main) if main else None, flush=True)
if main:
    time.sleep(0.5)
    uip.user32.PostMessageW(main, 0x0010, 0, 0)
    time.sleep(0.5)
if p.poll() is None:
    p.terminate()
print("exit", p.wait(timeout=10), flush=True)

appdata = os.path.join(os.environ.get("APPDATA", ""), "文件清理")
print("appdata", appdata, os.path.isdir(appdata), flush=True)
if os.path.isdir(appdata):
    for name in os.listdir(appdata):
        path = os.path.join(appdata, name)
        print("FILE", name, os.path.getsize(path), flush=True)
