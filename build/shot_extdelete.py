import importlib.util
import subprocess
import time

spec = importlib.util.spec_from_file_location("uip", r"tools\ui_probe.py")
uip = importlib.util.module_from_spec(spec)
spec.loader.exec_module(uip)

p = subprocess.Popen([r"dist\Genshin.exe", "--data-dir", r"build\scantest_v130c"],
                     stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
dlg = uip.find_dialog(None, 15)
if dlg:
    uip.click_button(dlg, 6)
main = uip.find_main(15)
time.sleep(1)
uip.user32.PostMessageW(main, 0x0201, 1, (347 << 16) | 60)
time.sleep(0.8)
dlg = uip.find_dialog("按后缀删除", 5)
print("dialog", hex(dlg) if dlg else None, flush=True)
if dlg:
    edit = uip.user32.GetDlgItem(dlg, 1302)
    uip.user32.SendMessageW(edit, 0x000C, 0, "docx;xlsx;pdf")
    uip.user32.SendMessageW(dlg, 0x0111, 1303, 0)
    time.sleep(0.8)
    uip.capture(dlg, r"build\ui_extdelete_dialog.png")
    uip.click_button(dlg, 2)
    time.sleep(0.5)
uip.user32.PostMessageW(main, 0x0010, 0, 0)
time.sleep(0.5)
if p.poll() is None:
    p.terminate()
print("exit", p.wait(timeout=10), flush=True)
