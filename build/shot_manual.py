import ctypes
import importlib.util
import subprocess
import time
from ctypes import wintypes

spec = importlib.util.spec_from_file_location("uip", r"tools\ui_probe.py")
uip = importlib.util.module_from_spec(spec)
spec.loader.exec_module(uip)

p = subprocess.Popen([r"dist\Genshin.exe", "--data-dir", r"build\scantest_v130"],
                     stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
dlg = uip.find_dialog(None, 15)
if dlg:
    uip.click_button(dlg, 6)
main = uip.find_main(15)
time.sleep(1)
edit = None
for child in uip.enum_windows(main):
    if uip.class_name(child) == "Edit":
        edit = child
        break
uip.user32.SendMessageW(edit, 0x000C, 0, "年度报告.docx")
uip.user32.PostMessageW(main, 0x0111, (0x0300 << 16) | 2006, edit)
time.sleep(1)
lst = None
for child in uip.enum_windows(main):
    if uip.class_name(child) == "XZQFileList":
        lst = child
        break
rc = wintypes.RECT()
uip.user32.GetClientRect(lst, ctypes.byref(rc))
uip.user32.PostMessageW(lst, 0x0201, 1, ((42 + 29) << 16) | (rc.right - 50))
time.sleep(0.8)
mdlg = uip.find_dialog("手动确认删除", 5)
print("manual", hex(mdlg) if mdlg else None, flush=True)
if mdlg:
    uip.capture(mdlg, r"build\ui_manual.png")
    uip.click_button(mdlg, 2)
    time.sleep(0.5)
uip.user32.PostMessageW(main, 0x0010, 0, 0)
time.sleep(0.5)
if p.poll() is None:
    p.terminate()
print("exit", p.wait(timeout=10), flush=True)
