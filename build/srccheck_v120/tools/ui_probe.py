"""Small Win32 UI probe/screenshot helper used for local smoke testing.

Usage:
  python tools/ui_probe.py probe --data-dir build/scantest_data
  python tools/ui_probe.py shot  --data-dir build/scantest_data --out build/shot.png
"""
import argparse
import ctypes
import subprocess
import time
from ctypes import wintypes

user32 = ctypes.WinDLL("user32", use_last_error=True)
gdi32 = ctypes.WinDLL("gdi32", use_last_error=True)

EnumWindowsProc = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
EnumChildProc = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)


def window_text(hwnd):
    n = user32.GetWindowTextLengthW(hwnd)
    buf = ctypes.create_unicode_buffer(n + 1)
    user32.GetWindowTextW(hwnd, buf, n + 1)
    return buf.value


def class_name(hwnd):
    buf = ctypes.create_unicode_buffer(256)
    user32.GetClassNameW(hwnd, buf, 256)
    return buf.value


def rect(hwnd):
    r = wintypes.RECT()
    user32.GetWindowRect(hwnd, ctypes.byref(r))
    return (r.left, r.top, r.right, r.bottom)


def enum_windows(parent=None):
    out = []

    def cb(hwnd, lp):
        out.append(hwnd)
        return True

    if parent is None:
        user32.EnumWindows(EnumWindowsProc(cb), 0)
    else:
        user32.EnumChildWindows(parent, EnumChildProc(cb), 0)
    return out


def find_windows(cls_prefix=None, title_contains=None):
    found = []
    for hwnd in enum_windows():
        cls = class_name(hwnd)
        title = window_text(hwnd)
        if cls_prefix and not cls.startswith(cls_prefix):
            continue
        if title_contains and title_contains not in title:
            continue
        if user32.IsWindowVisible(hwnd):
            found.append((hwnd, cls, title, rect(hwnd)))
    return found


def dump_tree(hwnd, depth=0, limit=100):
    lines = []
    title = window_text(hwnd)
    cls = class_name(hwnd)
    lines.append("  " * depth + f"{hwnd:#x} {cls!r} title={title!r} rect={rect(hwnd)}")
    if depth >= 3:
        return lines
    for child in enum_windows(hwnd):
        lines.extend(dump_tree(child, depth + 1, limit))
        if len(lines) >= limit:
            break
    return lines


def find_main(timeout=15.0):
    deadline = time.time() + timeout
    while time.time() < deadline:
        for hwnd in enum_windows():
            if class_name(hwnd) == "XzqFileCleanerMain":
                return hwnd
        time.sleep(0.2)
    return None


def find_dialog(title_contains=None, timeout=10.0):
    deadline = time.time() + timeout
    while time.time() < deadline:
        for hwnd in enum_windows():
            if class_name(hwnd) != "#32770" or not user32.IsWindowVisible(hwnd):
                continue
            title = window_text(hwnd)
            if title_contains is None or title_contains in title:
                return hwnd
        time.sleep(0.2)
    return None


def send_yes_to_messagebox(timeout=10.0):
    hwnd = find_dialog(None, timeout)
    if hwnd:
        user32.PostMessageW(hwnd, 0x0111, 6, 0)
    return hwnd


def click_button(hwnd, button_id):
    user32.PostMessageW(hwnd, 0x0111, button_id, 0)


def capture(hwnd, out):
    from PIL import Image

    left, top, right, bottom = rect(hwnd)
    width, height = right - left, bottom - top
    hdc = user32.GetWindowDC(hwnd)
    mem = gdi32.CreateCompatibleDC(hdc)
    bmp = gdi32.CreateCompatibleBitmap(hdc, width, height)
    old = gdi32.SelectObject(mem, bmp)
    # PW_RENDERFULLCONTENT = 2
    ok = user32.PrintWindow(hwnd, mem, 2)
    gdi32.SelectObject(mem, old)

    class BITMAPINFOHEADER(ctypes.Structure):
        _fields_ = [
            ("biSize", wintypes.DWORD),
            ("biWidth", wintypes.LONG),
            ("biHeight", wintypes.LONG),
            ("biPlanes", wintypes.WORD),
            ("biBitCount", wintypes.WORD),
            ("biCompression", wintypes.DWORD),
            ("biSizeImage", wintypes.DWORD),
            ("biXPelsPerMeter", wintypes.LONG),
            ("biYPelsPerMeter", wintypes.LONG),
            ("biClrUsed", wintypes.DWORD),
            ("biClrImportant", wintypes.DWORD),
        ]

    class BITMAPINFO(ctypes.Structure):
        _fields_ = [("bmiHeader", BITMAPINFOHEADER), ("bmiColors", wintypes.DWORD * 3)]

    bi = BITMAPINFO()
    bi.bmiHeader.biSize = ctypes.sizeof(BITMAPINFOHEADER)
    bi.bmiHeader.biWidth = width
    bi.bmiHeader.biHeight = -height
    bi.bmiHeader.biPlanes = 1
    bi.bmiHeader.biBitCount = 32
    bi.bmiHeader.biCompression = 0
    buf = ctypes.create_string_buffer(width * height * 4)
    gdi32.GetDIBits(mem, bmp, 0, height, buf, ctypes.byref(bi), 0)
    img = Image.frombuffer("RGBA", (width, height), buf, "raw", "BGRA", 0, 1)
    img.convert("RGB").save(out)
    print("PrintWindow", ok, "saved", out, img.size)
    gdi32.DeleteObject(bmp)
    gdi32.DeleteDC(mem)
    user32.ReleaseDC(hwnd, hdc)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("mode", choices=["probe", "shot", "batch", "quick", "protected", "nodata_stop", "single", "protectdlg", "ext", "extdlg"])
    ap.add_argument("--exe", default=r"dist\Genshin.exe")
    ap.add_argument("--data-dir", required=True)
    ap.add_argument("--out", default=r"build\ui_shot.png")
    args = ap.parse_args()

    proc = subprocess.Popen(
        [args.exe, "--data-dir", args.data_dir],
        stdin=subprocess.DEVNULL,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    print("PID", proc.pid)
    time.sleep(1.5)
    for hwnd, cls, title, r in find_windows():
        print(f"TOP {hwnd:#x} {cls!r} {title!r} {r}")
    print("--- dialogs ---")
    for hwnd, cls, title, r in find_windows(cls_prefix="#32770"):
        for line in dump_tree(hwnd):
            print(line)
    if args.mode == "nodata_stop":
        dlg = find_dialog(None, 15.0)
        print("no-data dialog", hex(dlg) if dlg else None, repr(window_text(dlg)) if dlg else "")
        if dlg:
            print("dialog text:", " | ".join(window_text(c) for c in enum_windows(dlg)))
            click_button(dlg, 7)  # No: don't scan yet
        main_hwnd = find_main()
        print("main", hex(main_hwnd) if main_hwnd else None)
        if main_hwnd:
            time.sleep(0.5)
            user32.PostMessageW(main_hwnd, 0x0111, 2002, 0)  # full scan
            time.sleep(2.0)
            user32.PostMessageW(main_hwnd, 0x0111, 2004, 0)  # stop
            time.sleep(4.0)
            capture(main_hwnd, args.out)
    elif args.mode != "probe":
        dlg = send_yes_to_messagebox()
        print("clicked prompt yes", hex(dlg) if dlg else None)
        main_hwnd = find_main()
        print("main", hex(main_hwnd) if main_hwnd else None)
        if main_hwnd:
            time.sleep(1.0)
            if args.mode == "batch":
                user32.PostMessageW(main_hwnd, 0x0111, 2005, 0)
                dlg = find_dialog("一键清理", timeout=5.0)
                print("batch confirm", hex(dlg) if dlg else None)
                if dlg:
                    click_button(dlg, 6)
                time.sleep(2.0)
                done = find_dialog("清理完成", timeout=3.0)
                print("batch done", hex(done) if done else None)
                if done:
                    click_button(done, 1)
                time.sleep(0.5)
            elif args.mode == "quick":
                user32.PostMessageW(main_hwnd, 0x0111, 2001, 0)
                dlg = find_dialog("快速扫描", timeout=5.0)
                print("quick confirm", hex(dlg) if dlg else None)
                if dlg:
                    click_button(dlg, 6)
                    time.sleep(2.0)
            elif args.mode == "protected":
                # 保护区 nav: client x=30, y=244 at 96 DPI.
                lp = (244 << 16) | 30
                user32.PostMessageW(main_hwnd, 0x0201, 1, lp)
                time.sleep(0.8)
            elif args.mode == "ext":
                # 后缀保护 nav: client x=30, y=292 at 96 DPI.
                lp = (292 << 16) | 30
                user32.PostMessageW(main_hwnd, 0x0201, 1, lp)
                time.sleep(0.8)
            elif args.mode == "extdlg":
                lp = (292 << 16) | 30
                user32.PostMessageW(main_hwnd, 0x0201, 1, lp)
                time.sleep(0.5)
                user32.PostMessageW(main_hwnd, 0x0201, 1, (660 << 16) | 110)
                dlg = find_dialog("后缀保护管理", timeout=5.0)
                print("ext dialog", hex(dlg) if dlg else None)
                if dlg:
                    time.sleep(0.5)
                    capture(dlg, args.out)
                    click_button(dlg, 2)
                time.sleep(0.5)
                if proc.poll() is None:
                    proc.terminate()
                print("exit", proc.wait(timeout=10))
                return
            elif args.mode == "protectdlg":
                user32.PostMessageW(main_hwnd, 0x0201, 1, (660 << 16) | 110)
                dlg = find_dialog("保护区管理", timeout=5.0)
                print("protect dialog", hex(dlg) if dlg else None)
                if dlg:
                    time.sleep(0.5)
                    capture(dlg, args.out)
                    click_button(dlg, 2)
                time.sleep(0.5)
                capture(main_hwnd, args.out.replace(".png", "_main.png"))
                main_hwnd = find_main(timeout=1.0)
                if main_hwnd:
                    user32.PostMessageW(main_hwnd, 0x0010, 0, 0)
                    time.sleep(0.5)
                if proc.poll() is None:
                    proc.terminate()
                print("exit", proc.wait(timeout=10))
                return
            elif args.mode == "single":
                lst = None
                for child in enum_windows(main_hwnd):
                    if class_name(child) == "XZQFileList":
                        lst = child
                        break
                if lst:
                    rc = wintypes.RECT()
                    user32.GetClientRect(lst, ctypes.byref(rc))
                    x = rc.right - 50
                    y = 42 + 29
                    user32.PostMessageW(lst, 0x0201, 1, (y << 16) | x)
                    dlg = find_dialog("确认删除", timeout=5.0)
                    print("delete confirm", hex(dlg) if dlg else None)
                    if dlg:
                        click_button(dlg, 6)
                    time.sleep(1.5)
            capture(main_hwnd, args.out)
    # Ask the app to close through WM_CLOSE if we have the main window.
    main_hwnd = find_main(timeout=1.0)
    if main_hwnd:
        user32.PostMessageW(main_hwnd, 0x0010, 0, 0)
        time.sleep(0.5)
        dlg = find_dialog(None, timeout=1.0)
        if dlg:
            click_button(dlg, 6)
    time.sleep(0.5)
    if proc.poll() is None:
        proc.terminate()
    print("exit", proc.wait(timeout=10))


if __name__ == "__main__":
    main()
