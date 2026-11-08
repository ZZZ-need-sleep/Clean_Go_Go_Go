import importlib.util
import shutil
import subprocess
import time
import os

spec = importlib.util.spec_from_file_location("uip", r"tools\ui_probe.py")
uip = importlib.util.module_from_spec(spec)
spec.loader.exec_module(uip)

src = r"build\scantest_ext"
dst = r"build\ext_mgmt_data"
if os.path.exists(dst):
    shutil.rmtree(dst)
shutil.copytree(src, dst)

WM_COMMAND = 0x0111
LB_GETCOUNT = 0x018B
LB_GETTEXT = 0x0189


def open_app():
    p = subprocess.Popen(
        [r"dist\Genshin.exe", "--data-dir", dst],
        stdin=subprocess.DEVNULL,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    dlg = uip.find_dialog(None, 15)
    if dlg:
        uip.click_button(dlg, 6)
    main = uip.find_main(15)
    time.sleep(1)
    uip.user32.PostMessageW(main, 0x0201, 1, (292 << 16) | 30)
    time.sleep(0.5)
    uip.user32.PostMessageW(main, 0x0201, 1, (660 << 16) | 110)
    dlg = uip.find_dialog("后缀保护管理", 5)
    return p, main, dlg


p, main, dlg = open_app()
print("dialog", hex(dlg) if dlg else None, flush=True)
if dlg:
    lst = uip.user32.GetDlgItem(dlg, 1202)
    edit = uip.user32.GetDlgItem(dlg, 1203)
    print("initial count", uip.user32.SendMessageW(lst, LB_GETCOUNT, 0, 0), flush=True)
    uip.user32.SendMessageW(edit, 0x000C, 0, "xyz")
    uip.user32.SendMessageW(dlg, WM_COMMAND, 1204, 0)
    time.sleep(0.8)
    print("after add count", uip.user32.SendMessageW(lst, LB_GETCOUNT, 0, 0), flush=True)
    uip.click_button(dlg, 2)
    time.sleep(1)
    uip.user32.PostMessageW(main, 0x0010, 0, 0)
    time.sleep(0.8)
if p.poll() is None:
    p.terminate()
print("exit1", p.wait(timeout=10), flush=True)

p, main, dlg = open_app()
print("reopen dialog", hex(dlg) if dlg else None, flush=True)
if dlg:
    lst = uip.user32.GetDlgItem(dlg, 1202)
    count = uip.user32.SendMessageW(lst, LB_GETCOUNT, 0, 0)
    print("persisted count", count, flush=True)
    found = False
    for i in range(count):
        buf = uip.ctypes.create_unicode_buffer(64)
        uip.user32.SendMessageW(lst, LB_GETTEXT, i, uip.ctypes.byref(buf))
        if buf.value == ".xyz":
            found = True
    print("contains .xyz", found, flush=True)
    uip.click_button(dlg, 2)
    time.sleep(0.5)
    uip.user32.PostMessageW(main, 0x0010, 0, 0)
    time.sleep(0.5)
if p.poll() is None:
    p.terminate()
print("exit2", p.wait(timeout=10), flush=True)
