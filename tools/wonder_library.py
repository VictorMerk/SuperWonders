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
    'original': ('Super Mario Bros. Wonder', 'Original edition'),
    'switch2': ('Super Mario Bros. Wonder', 'Switch 2 package · verification build'),
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
                     'NX_LAN_', 'NX_PLAY_', 'NX_FRAMEGEN_PROBE')
        return not (name.startswith(transient) or
                    name in ('NX_GAMELIB', 'NX_DATA_ROOT', 'NX_HEADLESS', 'NX_HEADLESS_PRESENT',
                             'NX_NO_LAUNCHER', 'NX_LOG', 'NX_FRAMETRACE', 'NX_AUDIO_DUMP') or
                    (name.startswith('NX_') and name.endswith('_TOUR')))
    env = {k: v for k, v in os.environ.items() if inherited(k)}
    env['NX_DATA_ROOT'] = str(root)
    if configure:
        launcher = executable.with_name('wonder-launcher.exe')
        if os.name == 'nt' and launcher.is_file():
            executable = launcher
    return [str(executable)] + (['--config'] if configure else []), str(executable.parent), env


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
    from tkinter import filedialog, messagebox

    window = tk.Tk()
    window.title('SuperWonders • Library')
    window.geometry('960x740')
    window.minsize(760, 560)
    bg, panel, text, quiet, accent = '#132822', '#203c32', '#f6f0dc', '#b4c5b5', '#f3bd54'
    window.configure(bg=bg)
    try:
        config = load_config(args.config)
    except (OSError, ValueError) as e:
        messagebox.showerror('Cannot read Wonder library', str(e), parent=window)
        window.destroy()
        return 2
    running, status, buttons, manage_buttons = {}, {}, {}, {}
    choose_buttons, folder_buttons, results, health = {}, {}, {}, {}
    canvas = tk.Canvas(window, bg=bg, highlightthickness=0)
    scrollbar = tk.Scrollbar(window, orient='vertical', command=canvas.yview)
    scrollbar.pack(side='right', fill='y')
    canvas.configure(yscrollcommand=scrollbar.set)
    canvas.pack(fill='both', expand=True)
    body = tk.Frame(canvas, bg=bg, padx=24, pady=24)
    content = canvas.create_window((0,0), window=body, anchor='nw')
    body.bind('<Configure>', lambda event: canvas.configure(scrollregion=canvas.bbox('all')))
    canvas.bind('<Configure>', lambda event: canvas.itemconfigure(content, width=event.width))
    window.bind('<MouseWheel>', lambda event: canvas.yview_scroll(-1 if event.delta>0 else 1, 'units'))
    tk.Label(body, text='SUPERWONDERS', font=('Segoe UI', 11, 'bold'), fg=accent, bg=bg).pack(anchor='w')
    tk.Label(body, text='Your Flower Kingdom', font=('Georgia', 29), fg=text, bg=bg).pack(anchor='w', pady=(7, 5))
    tk.Label(body, text='Two editions. A place for each adventure.', font=('Segoe UI', 12),
             fg=quiet, bg=bg).pack(anchor='w', pady=(0, 22))

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
        card = tk.Frame(body, bg=panel, padx=20, pady=17)
        card.pack(fill='x', pady=(0, 12))
        actions = tk.Frame(card, bg=panel)
        actions.pack(side='right', padx=(18, 0))
        tk.Label(card, text=edition.upper(), fg=accent, bg=panel, font=('Segoe UI', 9, 'bold')).pack(anchor='w')
        tk.Label(card, text=title, fg=text, bg=panel, font=('Segoe UI', 15, 'bold')).pack(anchor='w', pady=(5, 4))
        status[key] = tk.StringVar()
        tk.Label(card, textvariable=status[key], fg=quiet, bg=panel, font=('Segoe UI', 10),
                 wraplength=430, justify='left').pack(anchor='w')
        buttons[key] = tk.Button(actions, text='Play', command=lambda k=key: play(k), bg=accent, fg=bg,
            activebackground='#ffd582', relief='flat', width=12, pady=5, font=('Segoe UI', 11, 'bold'))
        buttons[key].pack()
        manage_buttons[key] = tk.Button(actions, text='LAN, mods & settings',
            command=lambda k=key: play(k, configure=True), relief='flat', bg=panel, fg=text,
            activebackground=panel, activeforeground=accent, font=('Segoe UI', 9), pady=5)
        manage_buttons[key].pack(pady=(4, 0))
        choose_buttons[key] = tk.Button(actions, text='Choose installation', command=lambda k=key: configure(k), relief='flat',
            bg=panel, fg=quiet, activebackground=panel, activeforeground=text, font=('Segoe UI', 9))
        choose_buttons[key].pack(pady=(6, 0))
        folders = tk.Frame(card, bg=panel)
        folders.pack(anchor='w', pady=(8, 0))
        folder_buttons[key] = []
        for label, name in (('Open mods', 'mods'), ('Open saves', 'user')):
            button = tk.Button(folders, text=label, command=lambda k=key, n=name: folder(k,n),
                               bg=panel, fg=quiet, relief='flat', font=('Segoe UI', 9))
            button.pack(side='left', padx=(0,8))
            folder_buttons[key].append(button)
        health[key] = installation_health(config['editions'][key])
        refresh(key)
    utilities = tk.Frame(body, bg=bg)
    utilities.pack(anchor='w', pady=(8,4))
    for label, action in (('Reload library', reload_config), ('Export preparation report', export_diagnostics)):
        tk.Button(utilities, text=label, command=action, bg=panel, fg=text, relief='flat',
                  font=('Segoe UI',10), padx=12, pady=6).pack(side='left', padx=(0,12))
    tk.Label(body, text='Each edition uses its own game files, saves and settings.\n'
             'Switch 2 play requires a compatible prepared runtime; importing a NUP alone is not enough.',
             fg=quiet, bg=bg, justify='left', font=('Segoe UI', 10)).pack(anchor='w', pady=(8, 0))

    def poll():
        for key in SLOTS:
            refresh(key)
        window.after(1000, poll)
    window.after(1000, poll)
    window.mainloop()
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
