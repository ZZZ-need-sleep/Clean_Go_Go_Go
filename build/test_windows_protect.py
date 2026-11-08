import importlib.util
import subprocess
import time

spec = importlib.util.spec_from_file_location("uip", r"tools\ui_probe.py")
uip = importlib.util.module_from_spec(spec)
spec.loader.exec_module(uip)

p = subprocess.Popen([r"dist\Genshin.exe", "--data-dir", r"build\selftest_v120"],
                     stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
dlg = uip.find_dialog(None, 15)
if dlg:
    uip.click_button(dlg, 6)
main = uip.find_main(15)
time.sleep(1)
uip.user32.PostMessageW(main, 0x0201, 1, (660 << 16) | 110)
dlg = uip.find_dialog("保护区管理", 5)
print("dialog", hex(dlg) if dlg else None, flush=True)
if dlg:
    lst = uip.user32.GetDlgItem(dlg, 1102)
    n = uip.user32.SendMessageW(lst, 0x018B, 0, 0)
    print("dirs", n, flush=True)
    for i in range(n):
        buf = uip.ctypes.create_unicode_buffer(512)
        uip.user32.SendMessageW(lst, 0x0189, i, uip.ctypes.byref(buf))
        print(repr(buf.value), flush=True)
    uip.click_button(dlg, 2)
    time.sleep(0.5)
uip.user32.PostMessageW(main, 0x0010, 0, 0)
time.sleep(0.5)
if p.poll() is None:
    p.terminate()
print("exit", p.wait(timeout=10), flush=True)
