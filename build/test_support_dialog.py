import importlib.util
import subprocess
import time

spec = importlib.util.spec_from_file_location("uip", r"tools\ui_probe.py")
uip = importlib.util.module_from_spec(spec)
spec.loader.exec_module(uip)

p = subprocess.Popen([r"dist\Genshin.exe", "--data-dir", r"build\selftest_v130b"],
                     stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
dlg = uip.find_dialog(None, 15)
if dlg:
    uip.click_button(dlg, 6)
main = uip.find_main(15)
time.sleep(1)
uip.user32.PostMessageW(main, 0x0201, 1, (342 << 16) | 157)
dlg = uip.find_dialog("支持的清理格式", 5)
print("dialog", hex(dlg) if dlg else None, flush=True)
if dlg:
    edit = uip.user32.GetDlgItem(dlg, 1401)
    n = uip.user32.GetWindowTextLengthW(edit)
    buf = uip.ctypes.create_unicode_buffer(n + 1)
    uip.user32.GetWindowTextW(edit, buf, n + 1)
    print("text length", n, flush=True)
    print(buf.value[:400], flush=True)
    uip.capture(dlg, r"build\ui_support.png")
    uip.click_button(dlg, 2)
    time.sleep(0.5)
uip.user32.PostMessageW(main, 0x0010, 0, 0)
time.sleep(0.5)
if p.poll() is None:
    p.terminate()
print("exit", p.wait(timeout=10), flush=True)
