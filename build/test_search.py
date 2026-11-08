import importlib.util
import subprocess
import time
from ctypes import wintypes

spec = importlib.util.spec_from_file_location("uip", r"tools\ui_probe.py")
uip = importlib.util.module_from_spec(spec)
spec.loader.exec_module(uip)

p = subprocess.Popen(
    [r"dist\Genshin.exe", "--data-dir", r"build\batch_data"],
    stdin=subprocess.DEVNULL,
    stdout=subprocess.DEVNULL,
    stderr=subprocess.DEVNULL,
)
print("pid", p.pid, flush=True)
dlg = uip.find_dialog(None, 15)
if dlg:
    uip.click_button(dlg, 6)
main = uip.find_main(15)
print("main", hex(main) if main else None, flush=True)
if main:
    time.sleep(1)
    edit = None
    for child in uip.enum_windows(main):
        if uip.class_name(child) == "Edit":
            edit = child
            break
    print("edit", hex(edit) if edit else None, flush=True)
    if edit:
        uip.user32.SendMessageW(edit, 0x000C, 0, "readme")  # WM_SETTEXT
        uip.user32.PostMessageW(main, 0x0111, (0x0300 << 16) | 2006, edit)
        time.sleep(0.8)
        uip.capture(main, r"build\ui_search.png")
    # Office tab click
    uip.user32.SendMessageW(edit, 0x000C, 0, "")
    uip.user32.PostMessageW(main, 0x0111, (0x0300 << 16) | 2006, edit)
    time.sleep(0.8)
    uip.user32.PostMessageW(main, 0x0201, 1, (196 << 16) | 30)
    time.sleep(0.8)
    uip.capture(main, r"build\ui_office.png")
    uip.user32.PostMessageW(main, 0x0010, 0, 0)
    time.sleep(0.5)
if p.poll() is None:
    p.terminate()
print("exit", p.wait(timeout=10), flush=True)
