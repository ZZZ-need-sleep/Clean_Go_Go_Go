import ctypes
import importlib.util
import os
import shutil
import subprocess
import time
from ctypes import wintypes

spec = importlib.util.spec_from_file_location("uip", r"tools\ui_probe.py")
uip = importlib.util.module_from_spec(spec)
spec.loader.exec_module(uip)

fixture = r"tests\fixture_manual"
data = r"build\manual_data"
for path in (fixture, data):
    if os.path.exists(path):
        shutil.rmtree(path)
shutil.copytree(r"tests\fixture", fixture)
os.makedirs(data)

p = subprocess.Popen([r"dist\Genshin.exe", "--data-dir", data, "--test-scan", fixture],
                     stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
print("testscan", p.wait(timeout=60), flush=True)

p = subprocess.Popen([r"dist\Genshin.exe", "--data-dir", data],
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
uip.user32.SendMessageW(edit, 0x000C, 0, "年度报告.docx")  # WM_SETTEXT
uip.user32.PostMessageW(main, 0x0111, (0x0300 << 16) | 2006, edit)
time.sleep(1.0)

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
print("manual dialog", hex(mdlg) if mdlg else None, flush=True)
if mdlg:
    e1 = uip.user32.GetDlgItem(mdlg, 1502)
    e2 = uip.user32.GetDlgItem(mdlg, 1503)
    ok = uip.user32.GetDlgItem(mdlg, 1)
    print("initial ok enabled", uip.user32.IsWindowEnabled(ok), flush=True)

    # Put text on the clipboard, then try WM_PASTE.
    user32 = uip.user32
    kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel32.GlobalAlloc.argtypes = [wintypes.UINT, ctypes.c_size_t]
    kernel32.GlobalAlloc.restype = ctypes.c_void_p
    kernel32.GlobalLock.argtypes = [ctypes.c_void_p]
    kernel32.GlobalLock.restype = ctypes.c_void_p
    kernel32.GlobalUnlock.argtypes = [ctypes.c_void_p]
    user32.SetClipboardData.argtypes = [wintypes.UINT, ctypes.c_void_p]
    user32.SetClipboardData.restype = ctypes.c_void_p
    user32.OpenClipboard(0)
    user32.EmptyClipboard()
    text = "确认删除"
    size = (len(text) + 1) * ctypes.sizeof(ctypes.c_wchar)
    h = kernel32.GlobalAlloc(0x0042, size)  # GMEM_MOVEABLE | GMEM_ZEROINIT
    ptr = kernel32.GlobalLock(h)
    ctypes.memmove(ptr, text, size)
    kernel32.GlobalUnlock(h)
    user32.SetClipboardData(13, h)  # CF_UNICODETEXT
    user32.CloseClipboard()

    user32.SendMessageW(e1, 0x0302, 0, 0)  # WM_PASTE
    time.sleep(0.2)
    print("after paste len", user32.GetWindowTextLengthW(e1), flush=True)

    user32.SendMessageW(e1, 0x000C, 0, text)
    user32.SendMessageW(e2, 0x000C, 0, text)
    user32.SendMessageW(mdlg, 0x0111, (0x0300 << 16) | 1502, e1)
    user32.SendMessageW(mdlg, 0x0111, (0x0300 << 16) | 1503, e2)
    time.sleep(0.3)
    print("ok enabled after input", user32.IsWindowEnabled(ok), flush=True)
    user32.PostMessageW(mdlg, 0x0111, 1, 0)
    time.sleep(4)

target = os.path.join(fixture, r"办公\年度报告.docx")
print("target exists", os.path.exists(target), flush=True)
print("dialogs", [(hex(h), uip.window_text(h)) for h in uip.enum_windows()
                 if uip.class_name(h) == "#32770" and uip.user32.IsWindowVisible(h)], flush=True)
uip.user32.PostMessageW(main, 0x0010, 0, 0)
time.sleep(1)
if p.poll() is None:
    p.terminate()
print("exit", p.wait(timeout=10), flush=True)
