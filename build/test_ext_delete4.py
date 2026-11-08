import importlib.util
import os
import shutil
import subprocess
import time

spec = importlib.util.spec_from_file_location("uip", r"tools\ui_probe.py")
uip = importlib.util.module_from_spec(spec)
spec.loader.exec_module(uip)

fixture = r"tests\fixture_extdel2"
data = r"build\extdel_data2"
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
    uip.user32.SendMessageW(edit, 0x000C, 0, "docx;xlsx;pdf")
    uip.user32.SendMessageW(dlg, 0x0111, 1303, 0)
    time.sleep(1)
    print("matches", uip.user32.SendMessageW(lst, 0x018B, 0, 0), flush=True)
    uip.user32.SendMessageW(dlg, 0x0111, 1307, 0)
    time.sleep(0.5)
    print("selected", uip.user32.SendMessageW(lst, 0x0190, 0, 0), flush=True)
    uip.user32.PostMessageW(dlg, 0x0111, 1308, 0)
    conf = uip.find_dialog("确认按后缀删除", 5)
    print("confirm", hex(conf) if conf else None, flush=True)
    if conf:
        uip.click_button(conf, 6)
        print("clicked yes", flush=True)
        for i in range(20):
            time.sleep(0.5)
            exists = [os.path.exists(os.path.join(fixture, r)) for r in
                      [r"办公\年度报告.docx", r"办公\预算表.xlsx", r"嵌套\深层\旧合同.pdf"]]
            print("t", i, "files", exists, "proc", p.poll(), flush=True)
            if not any(exists):
                break
        print("data mtime", os.path.getmtime(os.path.join(data, "data.xzq")), flush=True)
        time.sleep(5)
        modals = [(h, uip.window_text(h)) for h in uip.enum_windows()
                  if uip.class_name(h) == "#32770" and uip.user32.IsWindowVisible(h)]
        print("modal dialogs", [(hex(h), t) for h, t in modals], flush=True)
        for h, t in modals:
            print("children", hex(h), [repr(uip.window_text(c)) for c in uip.enum_windows(h)], flush=True)
        uip.capture(main, r"build\ui_extdel_after.png")
if p.poll() is None:
    p.terminate()
print("exit", p.wait(timeout=10), flush=True)
