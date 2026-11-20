"""One Windows/Linux launcher for independently prepared Wonder editions.

Run: python tools/wonder_library.py [--config PATH]
This selects compatible external runtimes; it does not make an unsupported NUP playable.
Each edition keeps its own data root, settings, caches and saves. No automatic save conversion.
"""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile
import shutil
import sys

SLOTS = {
    'original': ('Super Mario Bros. Wonder', 'Nintendo Switch · Original edition'),
    'switch2': ('Super Mario Bros. Wonder', 'Nintendo Switch 2 Edition'),
}


def default_config():
    return {'schema': 1, 'editions': {key: {'executable': '', 'data_root': ''} for key in SLOTS}}


def load_config(path):
    path = Path(path)
    result = default_config()
    if not path.exists():
        return result
    data = json.loads(path.read_text(encoding='utf-8-sig'))
    if not isinstance(data, dict) or data.get('schema') != 1:
        raise ValueError('Unknown library configuration version')
    editions = data.get('editions', {})
    if not isinstance(editions, dict):
        raise ValueError('Invalid edition configuration')
    for key in SLOTS:
        entry = editions.get(key, {})
        if not isinstance(entry, dict):
            raise ValueError('Invalid edition configuration')
        for field in ('executable', 'data_root'):
            value = entry.get(field, '')
            if not isinstance(value, str) or '\0' in value:
                raise ValueError('Invalid edition path in the library configuration')
            if value and not Path(value).is_absolute():
                value = str((path.resolve().parent / value).resolve())
            result['editions'][key][field] = value
    return result


def save_config(path, config):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(mode='w', encoding='utf-8', dir=path.parent,
                                         prefix=path.name+'.', suffix='.tmp', delete=False) as stream:
            temporary = Path(stream.name)
            stream.write(json.dumps(config, indent=2) + '\n')
            stream.flush()
            os.fsync(stream.fileno())
        # Keep the last readable configuration for recovery. A malformed old file
        # must not replace a previously usable backup.
        if path.exists():
            load_config(path)
            shutil.copy2(path, path.with_suffix(path.suffix+'.bak'))
        temporary.replace(path)
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)


GAME_LIBRARY = 'wonder_game.dll' if os.name == 'nt' else 'libwonder_game.so'
REQUIRED_FILES = ('game/'+GAME_LIBRARY, 'game/build-id.txt', 'analysis/main.bin',
                  'analysis/sdk.bin', 'shader_programs.bin', 'spirv/results.tsv')


def installation_health(entry):
    """Small read-only checks: no game loading, hashes or shader/cache mutation."""
    if not entry.get('executable') or not entry.get('data_root'):
        return ['Choose a prepared runtime and game-data folder.']
    problems = []
    if not Path(entry['executable']).is_file():
        problems.append('Runtime executable is missing.')
    root = Path(entry['data_root'])
    if not root.is_dir():
        return problems + ['Game-data folder is missing.']
    for name in REQUIRED_FILES:
        target = root / name
        if name == 'game/'+GAME_LIBRARY:
            adjacent = Path(entry['executable']).parent/GAME_LIBRARY
            if adjacent.is_file():
                target = adjacent
        try:
            usable = target.is_file() and target.stat().st_size > 0
        except OSError:
            usable = False
        if not usable:
            problems.append('Missing or empty prepared file: '+name)
    if not (root/'extract/romfs').is_dir():
        problems.append('Prepared RomFS folder is missing.')
    return problems


def diagnostic_report(config):
    """Export only fixed labels and boolean health, never paths/saves/LAN codes."""
    return {'schema': 1, 'scope': 'Preparation checks only; no gameplay certification',
            'editions': {key: {'configured': bool(entry['executable'] and entry['data_root']),
                              'problems': installation_health(entry)}
                         for key, entry in config['editions'].items()}}


def exit_message(code):
    if code == 0:
        return 'Closed normally'
    return 'Exited with error '+str(code)+' (0x'+format(code & 0xffffffff, '08X')+'). Check the game log.'


def active_user_root(root):
    root = Path(root)
    try:
        with (root/'active-profile.txt').open(encoding='utf-8') as stream:
            identifier = stream.readline(128).rstrip('\r\n')
        if not (2 < len(identifier) < 64 and identifier.startswith('p-') and
                not set(identifier)-set('p-0123456789')):
            return root/'user'
        profile = root/'profiles'/identifier
        if not profile.is_dir() or profile.is_symlink():
            return root/'user'
        with (profile/'profile-name.txt').open(encoding='utf-8') as stream:
            name = stream.readline(128).rstrip('\r\n')
        if not (1 <= len(name.encode('utf-8')) <= 31 and name == name.strip(' ') and
                all(ord(c) >= 32 and ord(c) != 127 for c in name)):
            return root/'user'
        return profile/'user'
    except (OSError, UnicodeError):
        return root/'user'


def open_folder(path):
    path = Path(path).resolve()
    if not path.is_dir():
        raise ValueError('This folder does not exist yet. Start this edition once or check its installation.')
    if os.name == 'nt':
        os.startfile(str(path))
    else:
        subprocess.Popen(['open' if sys.platform == 'darwin' else 'xdg-open', str(path)])


def launch_spec(config, key, *, configure=False):
    entry = config['editions'][key]
    if not entry['executable'] or not entry['data_root']:
        raise ValueError('Choose this edition’s prepared runtime and game-data folder first.')
    executable, root = Path(entry['executable']).resolve(), Path(entry['data_root']).resolve()
    if not executable.is_file():
        raise ValueError('The configured runtime executable cannot be found.')
    if not root.is_dir():
        raise ValueError('The configured game-data folder cannot be found.')
    if executable.suffix.lower() in ('.nup', '.nx', '.nsp', '.xci'):
        raise ValueError('Choose a prepared runtime executable; a game package cannot be launched directly.')
    if os.name == 'nt' and executable.suffix.lower() != '.exe':
        raise ValueError('Choose a Windows .exe runtime.')
    for other_key, other in config['editions'].items():
        if other_key != key and other['data_root'] and (Path(other['data_root']).resolve() == root or
                Path(other['data_root']).resolve() in root.parents or root in Path(other['data_root']).resolve().parents):
            raise ValueError('The two editions need separate game-data folders to preserve their saves and caches.')
    if not configure:
        problems = installation_health(entry)
        if problems:
            raise ValueError('Installation needs preparation: '+problems[0]+' Open LAN, mods & settings to repair it.')
    def inherited(name):
        name = name.upper()
        transient = ('NX_TEST_', 'NX_INPUT', 'NX_PROBE', 'NX_DUMP', 'NX_TRACE',
                     'NX_LAN_', 'NX_NATIVE_', 'NX_STEAM_', 'NX_PLAY_', 'NX_FRAMEGEN_PROBE')
        return not (name.startswith(transient) or
                    name in ('NX_GAMELIB', 'NX_DATA_ROOT', 'NX_HEADLESS', 'NX_HEADLESS_PRESENT',
                             'NX_NO_LAUNCHER', 'NX_LOG', 'NX_FRAMETRACE', 'NX_AUDIO_DUMP',
                             'NX_CONTROLS_TOUR_SECTION', 'NX_PIPELINE_MISS_TRACE', 'STEAMAPPID', 'STEAMGAMEID') or
                    (name.startswith('NX_') and name.endswith('_TOUR')))
    env = {k: v for k, v in os.environ.items() if inherited(k)}
    env['NX_DATA_ROOT'] = str(root)
    if configure:
        launcher = executable.with_name('wonder-launcher.exe')
        if os.name == 'nt' and launcher.is_file():
            executable = launcher
    return [str(executable)] + (['--config'] if configure else []), str(executable.parent), env


def library_window_geometry(work_area):
    """Client geometry and bounded minimum for this library's own Tk window.

    Leave room for window decorations and desktop panels. Coordinates may be
    negative on desktops with monitors left of or above the primary monitor.
    """
    left, top, right, bottom = work_area
    available_w = max(1, right - left - 32)
    available_h = max(1, bottom - top - 64)
    width, height = min(960, available_w), min(740, available_h)
    x = left + max(0, (right - left - width) // 2)
    y = top + max(0, (bottom - top - height - 40) // 2)
    return width, height, x, y, min(520, available_w), min(420, available_h)


def library_card_stacked(available_width, actions_width, font_size):
    """Reserve a readable text column; measure actions in the actual Tk font."""
    return available_width < max(300, font_size * 26) + actions_width + 18


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    base = Path(os.environ.get('LOCALAPPDATA', Path.home() / '.local/share')) / 'SuperWonders'
    parser.add_argument('--config', type=Path, default=base / 'library.json')
    parser.add_argument('--check', action='store_true', help='Print read-only preparation checks as JSON, without opening a window')
    parser.add_argument('--diagnostics', type=Path, help='Write a report without private paths or save data')
    args = parser.parse_args()
    if args.check or args.diagnostics:
        try:
            config = load_config(args.config)
            report = diagnostic_report(config)
            encoded = json.dumps(report, indent=2)+'\n'
            if args.diagnostics:
                args.diagnostics.write_text(encoded, encoding='utf-8')
            if args.check:
                print(encoded, end='')
            return int(any(item['problems'] for item in report['editions'].values()))
        except (OSError, ValueError) as error:
            print('Cannot check Wonder library: '+str(error), file=sys.stderr)
            return 2
    import tkinter as tk
    from tkinter import filedialog, messagebox, font as tkfont

    window = tk.Tk()
    window.withdraw()
    window.title('SuperWonders • Library')
    window.update_idletasks()
    work_area = (window.winfo_vrootx(), window.winfo_vrooty(),
                 window.winfo_vrootx() + window.winfo_vrootwidth(),
                 window.winfo_vrooty() + window.winfo_vrootheight())
    if os.name == 'nt':
        # Query only this app's monitor. No other windows are moved or inspected.
        try:
            import ctypes
            from ctypes import wintypes
            class MonitorInfo(ctypes.Structure):
                _fields_ = [('size', wintypes.DWORD), ('monitor', wintypes.RECT),
                            ('work', wintypes.RECT), ('flags', wintypes.DWORD)]
            user32 = ctypes.windll.user32
            user32.MonitorFromWindow.argtypes = [wintypes.HWND, wintypes.DWORD]
            user32.MonitorFromWindow.restype = wintypes.HANDLE
            user32.GetMonitorInfoW.argtypes = [wintypes.HANDLE, ctypes.POINTER(MonitorInfo)]
            user32.GetMonitorInfoW.restype = wintypes.BOOL
            monitor = user32.MonitorFromWindow(window.winfo_id(), 2)
            info = MonitorInfo()
            info.size = ctypes.sizeof(info)
            if monitor and user32.GetMonitorInfoW(monitor, ctypes.byref(info)):
                work_area = (info.work.left, info.work.top, info.work.right, info.work.bottom)
        except (AttributeError, OSError):
            pass  # Tk's own desktop geometry remains a usable fallback.
    width, height, x, y, minimum_w, minimum_h = library_window_geometry(work_area)
    window.geometry(f'{width}x{height}+{x}+{y}')
    window.minsize(minimum_w, minimum_h)
    bg, panel, text, quiet, accent = '#0d0f13', '#171a20', '#f1f3f7', '#adb6c6', '#81bbff'
    window.configure(bg=bg)
    try:
        config = load_config(args.config)
    except (OSError, ValueError) as e:
        messagebox.showerror('Cannot read Wonder library', str(e), parent=window)
        window.destroy()
        return 2
    running, status, buttons, manage_buttons = {}, {}, {}, {}
    choose_buttons, folder_buttons, results, health = {}, {}, {}, {}
    canvas = tk.Canvas(window, bg=bg, highlightthickness=0, yscrollincrement=24)
    scrollbar = tk.Scrollbar(window, orient='vertical', command=canvas.yview)
    scrollbar.pack(side='right', fill='y')
    canvas.configure(yscrollcommand=scrollbar.set)
    canvas.pack(fill='both', expand=True)
    body = tk.Frame(canvas, bg=bg, padx=20, pady=20)
    content = canvas.create_window((0,0), window=body, anchor='nw')
    body.bind('<Configure>', lambda event: canvas.configure(scrollregion=canvas.bbox('all')))
    responsive_labels, card_layouts, utility_buttons = [], [], []
    pending_layout = None
    text_em = tkfont.Font(root=window, font=('Segoe UI', 10)).measure('M')

    def wrap_label(parent, **options):
        options.setdefault('wraplength', 480)
        label = tk.Label(parent, justify='left', anchor='w', **options)
        responsive_labels.append((label, parent))
        return label

    def action_button(parent, label, command, primary=False):
        return tk.Button(parent, text=label, command=command, bg=accent if primary else parent.cget('bg'),
                         fg=bg if primary else quiet, activebackground='#acd3ff' if primary else '#20252e',
                         activeforeground=bg if primary else text, relief='flat', borderwidth=0,
                         highlightthickness=2, highlightbackground=parent.cget('bg'), highlightcolor=accent,
                         font=('Segoe UI', 13, 'bold') if primary else ('Segoe UI', 10),
                         padx=16, pady=11 if primary else 8, takefocus=True)

    def reflow():
        nonlocal pending_layout
        pending_layout = None
        for card, info, actions in card_layouts:
            available = max(1, card.winfo_width())
            stacked = library_card_stacked(available, actions.winfo_reqwidth(), text_em)
            previous = getattr(card, '_stacked', None)
            if previous != stacked:
                card._stacked = stacked
                actions.grid_forget()
                if stacked:
                    actions.grid(row=1, column=0, sticky='ew', pady=(14, 0))
                else:
                    actions.grid(row=0, column=1, sticky='ne', padx=(18, 0))
        for label, parent in responsive_labels:
            # Width follows the actual text column, not the whole card/window.
            wrap = max(1, parent.winfo_width() - 2 * int(parent.cget('padx')) - 4)
            if int(float(label.cget('wraplength'))) != wrap:
                label.configure(wraplength=wrap)
        if utility_buttons:
            available = max(1, utilities.winfo_width())
            minimum = sum(button.winfo_reqwidth() for button in utility_buttons) + 12
            columns = 2 if available >= minimum else 1
            if getattr(utilities, '_columns', None) != columns:
                utilities._columns = columns
                for column in range(2):
                    utilities.grid_columnconfigure(column, weight=1 if column < columns else 0)
                for index, button in enumerate(utility_buttons):
                    button.grid(row=index // columns, column=index % columns, sticky='ew',
                                padx=(0, 12 if index % columns < columns - 1 else 0), pady=(0, 8))
        canvas.configure(scrollregion=canvas.bbox('all'))

    def schedule_layout(event=None):
        nonlocal pending_layout
        if pending_layout is None:
            # A Configure event can result from wrapping itself. Debounce to a
            # bounded later turn instead of recursively draining idle callbacks.
            pending_layout = window.after(16, reflow)

    def resize_canvas(event):
        canvas.itemconfigure(content, width=max(1, event.width))
        schedule_layout()

    canvas.bind('<Configure>', resize_canvas)
    body.bind('<Configure>', schedule_layout, add='+')
    window.bind('<MouseWheel>', lambda event: canvas.yview_scroll(
        -max(1, abs(event.delta) // 120) if event.delta > 0 else max(1, abs(event.delta) // 120), 'units') if event.delta else None)
    window.bind('<Button-4>', lambda event: canvas.yview_scroll(-1, 'units'))
    window.bind('<Button-5>', lambda event: canvas.yview_scroll(1, 'units'))
    window.bind('<Prior>', lambda event: canvas.yview_scroll(-1, 'pages'))
    window.bind('<Next>', lambda event: canvas.yview_scroll(1, 'pages'))

    def reveal_focus(event):
        widget = event.widget
        if widget is window or widget.winfo_toplevel() is not window:
            return
        region = canvas.bbox('all')
        if not region or region[3] <= 0:
            return
        top = widget.winfo_rooty() - body.winfo_rooty()
        bottom = top + widget.winfo_height()
        view_top = canvas.canvasy(0)
        view_bottom = view_top + canvas.winfo_height()
        if top < view_top:
            canvas.yview_moveto(max(0, top - 12) / region[3])
        elif bottom > view_bottom:
            canvas.yview_moveto(max(0, bottom + 12 - canvas.winfo_height()) / region[3])

    window.bind('<FocusIn>', reveal_focus, add='+')
    tk.Label(body, text='SUPERWONDERS', font=('Segoe UI', 11, 'bold'), fg=accent, bg=bg).pack(anchor='w')
    wrap_label(body, text='Your library', font=('Bahnschrift', 30), fg=text, bg=bg).pack(fill='x', pady=(7, 5))
    wrap_label(body, text='Choose an edition. Continue your adventure.', font=('Segoe UI', 12),
               fg=quiet, bg=bg).pack(fill='x', pady=(0, 22))

    def refresh(key):
        item = config['editions'][key]
        ready = bool(item['executable'] and item['data_root'])
        busy = any(process.poll() is None for process in running.values())
        if key in running and running[key].poll() is not None:
            results[key] = exit_message(running.pop(key).returncode)
            health[key] = installation_health(item)
        live = key in running and running[key].poll() is None
        status[key].set('Running' if live else results.get(key) or
                        (health[key][0] if health[key] else 'Ready to play'))
        buttons[key].configure(state='normal' if ready and not busy and not health[key] else 'disabled',
                               text='Running…' if live else 'Play')
        manage_buttons[key].configure(state='normal' if ready and not busy else 'disabled')
        choose_buttons[key].configure(state='disabled' if busy else 'normal')
        for button in folder_buttons[key]:
            button.configure(state='normal' if ready else 'disabled')

    def configure(key):
        executable = filedialog.askopenfilename(parent=window, title='Choose the runtime for ' + SLOTS[key][1],
            filetypes=[('Game runtime', '*.exe')] if os.name == 'nt' else [('Game runtime', '*')])
        if not executable:
            return
        root = filedialog.askdirectory(parent=window, title='Choose this edition’s game-data folder',
                                      initialdir=str(Path(executable).parent / 'data'), mustexist=True)
        if not root:
            return
        previous = config['editions'][key]
        config['editions'][key] = {'executable': executable, 'data_root': root}
        try:
            launch_spec(config, key, configure=True)
            save_config(args.config, config)
        except (OSError, ValueError) as e:
            config['editions'][key] = previous
            messagebox.showerror('Cannot configure this edition', str(e), parent=window)
        health[key] = installation_health(config['editions'][key])
        results.pop(key, None)
        refresh(key)

    def play(key, configure=False):
        # One game at a time from this library, without stopping an existing process.
        if any(process.poll() is None for process in running.values()):
            messagebox.showinfo('Wonder is running', 'Close the current game before starting another edition.',
                                parent=window)
            return
        try:
            argv, cwd, env = launch_spec(config, key, configure=configure)
            results.pop(key, None)
            running[key] = subprocess.Popen(argv, cwd=cwd, env=env)
        except (OSError, ValueError) as e:
            messagebox.showerror('Could not start Wonder', str(e), parent=window)
        for slot in SLOTS:
            refresh(slot)

    def folder(key, name):
        try:
            root = Path(config['editions'][key]['data_root'])
            open_folder(active_user_root(root) if name == 'user' else root/name)
        except (OSError, ValueError) as error:
            messagebox.showerror('Cannot open folder', str(error), parent=window)

    def reload_config():
        nonlocal config
        if any(process.poll() is None for process in running.values()):
            return
        try:
            updated = load_config(args.config)
            config = updated
            results.clear()
            for slot in SLOTS:
                health[slot] = installation_health(config['editions'][slot])
                refresh(slot)
        except (OSError, ValueError) as error:
            messagebox.showerror('Cannot reload Wonder library', str(error), parent=window)

    def export_diagnostics():
        target = filedialog.asksaveasfilename(parent=window, title='Export preparation report',
                                             defaultextension='.json', initialfile='wonder-preparation.json')
        if target:
            try:
                Path(target).write_text(json.dumps(diagnostic_report(config), indent=2)+'\n', encoding='utf-8')
            except OSError as error:
                messagebox.showerror('Cannot export report', str(error), parent=window)

    for key, (title, edition) in SLOTS.items():
        tk.Frame(body, bg='#2a303a', height=1).pack(fill='x', pady=(0, 12))
        card = tk.Frame(body, bg=bg, padx=0, pady=14)
        card.pack(fill='x', pady=(0, 12))
        card.grid_columnconfigure(0, weight=1)
        info = tk.Frame(card, bg=bg)
        info.grid(row=0, column=0, sticky='nsew')
        actions = tk.Frame(card, bg=bg)
        actions.grid(row=0, column=1, sticky='ne', padx=(18, 0))
        actions.grid_columnconfigure(0, weight=1)
        card_layouts.append((card, info, actions))
        card.bind('<Configure>', schedule_layout)
        info.bind('<Configure>', schedule_layout)
        wrap_label(info, text=edition, fg=quiet, bg=bg, font=('Segoe UI', 11, 'bold')).pack(fill='x')
        wrap_label(info, text=title, fg=text, bg=bg, font=('Bahnschrift', 25)).pack(fill='x', pady=(5, 4))
        status[key] = tk.StringVar()
        wrap_label(info, textvariable=status[key], fg=quiet, bg=bg, font=('Segoe UI', 11)).pack(fill='x')
        buttons[key] = action_button(actions, 'Play', lambda k=key: play(k), primary=True)
        buttons[key].grid(row=0, column=0, sticky='ew')
        manage_buttons[key] = action_button(actions, 'LAN, mods & settings', lambda k=key: play(k, configure=True))
        manage_buttons[key].grid(row=1, column=0, sticky='ew', pady=(5, 0))
        choose_buttons[key] = action_button(actions, 'Choose installation', lambda k=key: configure(k))
        choose_buttons[key].grid(row=2, column=0, sticky='ew', pady=(5, 0))
        folders = tk.Frame(info, bg=bg)
        folders.pack(anchor='w', pady=(8, 0))
        folder_buttons[key] = []
        for label, name in (('Open mods', 'mods'), ('Open saves', 'user')):
            button = action_button(folders, label, lambda k=key, n=name: folder(k,n))
            button.pack(side='left', padx=(0,8))
            folder_buttons[key].append(button)
        health[key] = installation_health(config['editions'][key])
        refresh(key)
    utilities = tk.Frame(body, bg=bg)
    utilities.pack(fill='x', pady=(8,4))
    for label, action in (('Reload library', reload_config), ('Export preparation report', export_diagnostics)):
        utility_buttons.append(action_button(utilities, label, action))
    wrap_label(body, text='Each edition uses its own game files, saves and settings.\n'
             'Switch 2 play requires a compatible prepared runtime; importing a NUP alone is not enough.',
               fg=quiet, bg=bg, font=('Segoe UI', 10)).pack(fill='x', pady=(8, 0))

    def poll():
        for key in SLOTS:
            refresh(key)
        window.after(1000, poll)
    window.after(1000, poll)
    schedule_layout()
    window.deiconify()
    window.mainloop()
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
