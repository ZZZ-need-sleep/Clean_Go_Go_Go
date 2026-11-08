import importlib.util
import os
import shutil
import subprocess
import time

spec = importlib.util.spec_from_file_location("uip", r"tools\ui_probe.py")
uip = importlib.util.module_from_spec(spec)
spec.loader.exec_module(uip)

fixture = r"tests\fixture_extdel3"
data = r"build\extdel_data3"
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
uip.user32.PostMessageW(main, 0x0201, 1, (347 << 16) | 110)
time.sleep(0.8)
dlg = uip.find_dialog("按后缀删除", 5)
if dlg:
    edit = uip.user32.GetDlgItem(dlg, 1302)
    lst = uip.user32.GetDlgItem(dlg, 1305)
    uip.user32.SendMessageW(edit, 0x000C, 0, "docx")
    uip.user32.SendMessageW(dlg, 0x0111, 1303, 0)
    time.sleep(0.5)
    print("matches", uip.user32.SendMessageW(lst, 0x018B, 0, 0), flush=True)
    uip.user32.SendMessageW(dlg, 0x0111, 1307, 0)
    time.sleep(0.3)
    uip.user32.PostMessageW(dlg, 0x0111, 1308, 0)
    conf = uip.find_dialog("确认按后缀删除", 5)
    print("confirm", hex(conf) if conf else None, flush=True)
    if conf:
        uip.click_button(conf, 6)
        t0 = time.time()
        for i in range(20):
            time.sleep(1)
            d = uip.find_dialog("按后缀删除", 0.1)
            print("t", round(time.time() - t0, 1), "dialog", hex(d) if d else None,
                  "proc", p.poll(), flush=True)
            if d is None:
                break
if p.poll() is None:
    p.terminate()
print("exit", p.wait(timeout=10), flush=True)
