#!/usr/bin/env python3

import tkinter as tk
from tkinter import scrolledtext
import subprocess
import threading
import webbrowser
import os
import sys
import signal
import argparse

# SUDO ENFORCEMENT
if os.geteuid() != 0:
    print("\033[91m[ERROR] This script must be run as root!\033[0m")
    print("Please use: sudo ./entrypoint.py [args]")
    sys.exit(1)

# CLI ARGUMENTS
parser = argparse.ArgumentParser(description="System Control Panel")
parser.add_argument("--static", action="store_true", help="Run networks in static mode")
parser.add_argument("--dynamic", action="store_true", help="Run networks in dynamic mode (default)")
parser.add_argument("--build", action="store_true", help="Force build networks on startup")
args, unknown = parser.parse_known_args()

NET_MODE = "--static" if args.static else "--dynamic"
NET_BUILD = "--build" if args.build else ""
startup_process = None

# Visual Constants
BG_MAIN = "#1e1e1e"      
BG_PANEL = "#2d2d2d"
BG_SIDEBAR = "#151515"
FG_TEXT = "#eeeeee"      
ACCENT_GREEN = "#28a745" 
ACCENT_RED = "#dc3545"
ACCENT_BLUE = "#007acc"
ACCENT_GRAY = "#4a4a4a"
ACCENT_PURPLE = "#6f42c1"
FONT_BOLD = ("Segoe UI", 9, "bold")
FONT_NORM = ("Segoe UI", 9)
FONT_TITLE = ("Segoe UI", 16, "bold")

# Visual Feedback Helpers
def shake_widget(widget, original_color, count=0):
    if count == 0:
        widget.config(bg="#721c24") 
    offsets = [5, -10, 10, -10, 10, -5, 0]
    if count < len(offsets):
        widget.place(x=offsets[count], y=0, relwidth=1, relheight=1)
        root.after(50, lambda: shake_widget(widget, original_color, count + 1))
    else:
        widget.place(x=0, y=0, relwidth=1, relheight=1)
        root.after(1000, lambda: widget.config(bg=original_color))

class LogConsole(tk.Frame):
    def __init__(self, parent):
        super().__init__(parent, bg=BG_MAIN)
        self.console = scrolledtext.ScrolledText(self, bg="#000000", fg="#d4d4d4", 
                                                font=("Consolas", 10), state="disabled", 
                                                borderwidth=0)
        self.console.pack(fill="both", expand=True)
            
    def log(self, message):
        def append():
            self.console.config(state="normal")
            self.console.insert(tk.END, message + "\n")
            self.console.see(tk.END)
            self.console.config(state="disabled")
        self.after(0, append)

# Process Functions
def stream_logs(stream, logger):
    for line in stream:
        logger.log(line.strip())

def startup_networks():
    """Automatically triggered on load."""
    global startup_process
    sys_logs.log(f"[SYSTEM] Auto-starting networks...")
    
    cmd = ["make", "networks", f"NET_MODE={NET_MODE}", "CMD=up", f"OPTS={NET_BUILD}"]
    sys_logs.log(f"[RUNNING] {' '.join(cmd)}")
    
    startup_process = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, bufsize=1, start_new_session=True)
    threading.Thread(target=stream_logs, args=(startup_process.stdout, sys_logs), daemon=True).start()

def run_make_command(make_target, action, logger):
    """Generic function to handle UP, DOWN, and BUILD for targets."""
    if action == "up":
        cmd = ["make", make_target, "CMD=up", "OPTS="]
    elif action == "build":
        cmd = ["make", make_target, "CMD=up", "OPTS=--build"]
    elif action == "down":
        cmd = ["make", make_target, "CMD=down"]
    
    logger.log(f"[{action.upper()}] {' '.join(cmd)}")
    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, bufsize=1)
    threading.Thread(target=stream_logs, args=(proc.stdout, logger), daemon=True).start()

def open_web_browser(url, logger, prefix="[SYSTEM]"):
    logger.log(f"{prefix} Opening {url} in browser...")
    sudo_user = os.environ.get("SUDO_USER")
    if sudo_user:
        display = os.environ.get("DISPLAY", ":0")
        cmd = [
            "sudo", "-u", sudo_user, "sh", "-c", 
            f"DISPLAY={display} DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/$(id -u {sudo_user})/bus XDG_RUNTIME_DIR=/run/user/$(id -u {sudo_user}) xdg-open {url}"
        ]
        subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    else:
        webbrowser.open(url)

def run_vm():
    path = qcow_path_entry.get().strip()
    if not path or not os.path.exists(path):
        iso_logs.log(f"[ERROR] Path does not exist: {path}")
        shake_widget(qcow_path_entry, BG_MAIN)
        return
    def execute():
        cmd = ["make", "vm", f"VM_PATH={path}"]
        iso_logs.log(f"[RUNNING VM] {' '.join(cmd)}")
        proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, bufsize=1)
        threading.Thread(target=stream_logs, args=(proc.stdout, iso_logs), daemon=True).start()
        exit_code = proc.wait()
        if exit_code != 0:
            root.after(0, lambda: shake_widget(qcow_path_entry, BG_MAIN))
    threading.Thread(target=execute, daemon=True).start()

# Cleanup Action upon Window Exit
def on_closing():
    root.protocol("WM_DELETE_WINDOW", lambda: None)
    show_page("System") 
    sys_logs.log("[SYSTEM] Shutting down... Running Docker Compose down on everything.")
    
    def cleanup():
        for target in ["networks", "outside-net", "isolated-net"]:
            sys_logs.log(f"[SYSTEM] Stopping {target}...")
            subprocess.run(["make", target, "CMD=down"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            
        sys_logs.log("[SYSTEM] Cleanup complete. Goodbye!")
        root.after(1500, root.destroy) 
        
    threading.Thread(target=cleanup, daemon=True).start()


# UI SETUP & LAYOUT
root = tk.Tk()
root.title("System Control Panel (ROOT)")
root.geometry("1100x700")
root.configure(bg=BG_MAIN)
root.protocol("WM_DELETE_WINDOW", on_closing)

# Layout: Sidebar (Left) and Content (Right)
sidebar = tk.Frame(root, bg=BG_SIDEBAR, width=200)
sidebar.pack(side="left", fill="y")
sidebar.pack_propagate(False)

content_area = tk.Frame(root, bg=BG_MAIN)
content_area.pack(side="left", fill="both", expand=True)

# Dictionary to hold pages
pages = {}
sidebar_buttons = {}

def show_page(page_name):
    for page in pages.values():
        page.pack_forget()
    pages[page_name].pack(fill="both", expand=True)
    for name, btn in sidebar_buttons.items():
        if name == page_name:
            btn.config(bg=BG_PANEL, fg="white", font=("Segoe UI", 10, "bold"))
        else:
            btn.config(bg=BG_SIDEBAR, fg="#aaaaaa", font=("Segoe UI", 10))

def create_page(name, title_text):
    page = tk.Frame(content_area, bg=BG_MAIN)
    pages[name] = page
    
    btn = tk.Button(sidebar, text=name, anchor="w", padx=20, pady=10, 
                    bg=BG_SIDEBAR, fg="#aaaaaa", relief="flat", borderwidth=0,
                    activebackground=BG_PANEL, activeforeground="white",
                    command=lambda n=name: show_page(n))
    btn.pack(fill="x")
    sidebar_buttons[name] = btn
    
    title_label = tk.Label(page, text=title_text, bg=BG_MAIN, fg=FG_TEXT, font=FONT_TITLE)
    title_label.pack(anchor="w", padx=20, pady=(20, 10))
    return page


# SYSTEM PAGE (Startup & Exit Logs)
page_system = create_page("System", "System Startup & Core Activity")
sys_logs = LogConsole(page_system)
sys_logs.pack(fill="both", expand=True, padx=20, pady=(0, 20))

# OUTSIDE-NET PAGE
page_outside = create_page("Outside Net", "Outside Network Controls")

out_controls = tk.Frame(page_outside, bg=BG_PANEL, padx=15, pady=15)
out_controls.pack(fill="x", padx=20, pady=(0, 15))

out_btn_frame = tk.Frame(out_controls, bg=BG_PANEL)
out_btn_frame.pack(fill="x", pady=5)

tk.Button(out_btn_frame, text="UP", command=lambda: run_make_command("outside-net", "up", out_logs), bg=ACCENT_GREEN, fg="white", font=FONT_BOLD, relief="flat", width=10).pack(side="left", padx=(0,5))
tk.Button(out_btn_frame, text="DOWN", command=lambda: run_make_command("outside-net", "down", out_logs), bg=ACCENT_GRAY, fg="white", font=FONT_BOLD, relief="flat", width=10).pack(side="left", padx=5)
tk.Button(out_btn_frame, text="BUILD", command=lambda: run_make_command("outside-net", "build", out_logs), bg=ACCENT_BLUE, fg="white", font=FONT_BOLD, relief="flat", width=10).pack(side="left", padx=5)
tk.Button(out_btn_frame, text="APP", command=lambda: open_web_browser("http://localhost:8080", out_logs, "[OUTSIDE]"), bg=ACCENT_PURPLE, fg="white", font=FONT_BOLD, relief="flat", width=10).pack(side="left", padx=5)

tk.Label(page_outside, text="Logs:", bg=BG_MAIN, fg=FG_TEXT, font=FONT_BOLD).pack(anchor="w", padx=20)
out_logs = LogConsole(page_outside)
out_logs.pack(fill="both", expand=True, padx=20, pady=(5, 20))

# ISOLATED-NET PAGE
page_isolated = create_page("Isolated Net", "Isolated Network & VM Controls")

iso_controls = tk.Frame(page_isolated, bg=BG_PANEL, padx=15, pady=15)
iso_controls.pack(fill="x", padx=20, pady=(0, 15))

# Main Isolated Net Buttons
iso_btn_frame = tk.Frame(iso_controls, bg=BG_PANEL)
iso_btn_frame.pack(fill="x", pady=5)

tk.Button(iso_btn_frame, text="UP", command=lambda: run_make_command("isolated-net", "up", iso_logs), bg=ACCENT_GREEN, fg="white", font=FONT_BOLD, relief="flat", width=10).pack(side="left", padx=(0,5))
tk.Button(iso_btn_frame, text="DOWN", command=lambda: run_make_command("isolated-net", "down", iso_logs), bg=ACCENT_GRAY, fg="white", font=FONT_BOLD, relief="flat", width=10).pack(side="left", padx=5)
tk.Button(iso_btn_frame, text="BUILD", command=lambda: run_make_command("isolated-net", "build", iso_logs), bg=ACCENT_BLUE, fg="white", font=FONT_BOLD, relief="flat", width=10).pack(side="left", padx=5)
tk.Button(iso_btn_frame, text="APP", command=lambda: open_web_browser("http://localhost:9090", iso_logs, "[ISOLATED]"), bg=ACCENT_PURPLE, fg="white", font=FONT_BOLD, relief="flat", width=10).pack(side="left", padx=5)

# VM Configuration Toggle logic
vm_container = tk.Frame(iso_controls, bg=BG_PANEL)

tk.Label(vm_container, text="QCOW2 Image Path:", font=FONT_NORM, bg=BG_PANEL, fg="#aaa").pack(anchor="w", pady=(5,0))
entry_shake_holder = tk.Frame(vm_container, bg=BG_MAIN, height=25)
entry_shake_holder.pack(fill="x", pady=(2, 5))
entry_shake_holder.pack_propagate(False)
qcow_path_entry = tk.Entry(entry_shake_holder, bg=BG_MAIN, fg="white", insertbackground="white", borderwidth=0)
qcow_path_entry.place(x=0, y=0, relwidth=1, relheight=1)
tk.Button(vm_container, text="RUN VM", command=run_vm, bg=ACCENT_RED, fg="white", font=FONT_BOLD, relief="flat", width=15).pack(anchor="w", pady=(0, 5))

def toggle_vm_config():
    if vm_container.winfo_ismapped():
        vm_container.pack_forget()
    else:
        vm_container.pack(fill="x", pady=(15, 0))

tk.Button(iso_btn_frame, text="TOGGLE VM CONFIG", command=toggle_vm_config, bg="#333333", fg="white", font=FONT_BOLD, relief="flat", width=20).pack(side="right", padx=5)

tk.Label(page_isolated, text="Logs:", bg=BG_MAIN, fg=FG_TEXT, font=FONT_BOLD).pack(anchor="w", padx=20)
iso_logs = LogConsole(page_isolated)
iso_logs.pack(fill="both", expand=True, padx=20, pady=(5, 20))

show_page("System")

root.after(500, startup_networks)
root.mainloop()