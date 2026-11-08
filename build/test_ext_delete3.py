import importlib.util
import os
import subprocess
import time

spec = importlib.util.spec_from_file_location("uip", r"tools\ui_probe.py")
uip = importlib.util.module_from_spec(spec)
spec.loader.exec_module(uip)

data = r"build\extdel_data"
p = subprocess.Popen([r"dist\Genshin.exe", "--data-dir", data],
                     stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
dlg = uip.find_dialog(None, 15)
if dlg:
    uip.click_button(dlg, 6)
main = uip.find_main(15)
time.sleep(1)
uip.user32.PostMessageW(main, 0x0201, 1, (347 << 16) | 110)
time.sleep(0.8)
dlg = uip.find_dialog("按后缀删除", 5)
print("dialog", hex(dlg) if dlg else None, flush=True)
if dlg:
    edit = uip.user32.GetDlgItem(dlg, 1302)
    lst = uip.user32.GetDlgItem(dlg, 1305)
    btn = uip.user32.GetDlgItem(dlg, 1308)
    print("btn", hex(btn) if btn else None, "enabled", uip.user32.IsWindowEnabled(btn), flush=True)
    uip.user32.SendMessageW(edit, 0x000C, 0, "docx;xlsx;pdf")
    uip.user32.SendMessageW(dlg, 0x0111, 1303, 0)
    time.sleep(1)
    print("count", uip.user32.SendMessageW(lst, 0x018B, 0, 0), flush=True)
    print("btn enabled after search", uip.user32.IsWindowEnabled(btn), flush=True)
    uip.user32.SendMessageW(dlg, 0x0111, 1307, 0)
    time.sleep(0.5)
    print("selected", uip.user32.SendMessageW(lst, 0x0190, 0, 0), flush=True)
    print("btn enabled after select", uip.user32.IsWindowEnabled(btn), flush=True)
    uip.user32.SendMessageW(btn, 0x00F5, 0, 0)  # BM_CLICK
    time.sleep(1)
    for hwnd in uip.enum_windows():
        if uip.user32.IsWindowVisible(hwnd):
            cls = uip.class_name(hwnd)
            if cls.startswith("#32770") or cls == "XzqFileCleanerMain":
                print("window", hex(hwnd), repr(cls), repr(uip.window_text(hwnd)), flush=True)
    # dump child texts of the ext delete dialog if still open
    if uip.user32.IsWindow(dlg):
        print("dlg children", [repr(uip.window_text(c)) for c in uip.enum_windows(dlg)], flush=True)
    time.sleep(1)
    print("after click dialogs", [(hex(h), uip.window_text(h)) for h in uip.enum_windows() if uip.user32.IsWindowVisible(h) and uip.class_name(h) == "#32770"], flush=True)
    time.sleep(2)
if p.poll() is None:
    p.terminate()
print("exit", p.wait(timeout=10), flush=True)
