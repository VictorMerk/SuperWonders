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

SLOTS = {
    'original': ('Super Mario Bros. Wonder', 'Original edition'),
    'switch2': ('Super Mario Bros. Wonder', 'Switch 2 package · verification build'),
}


def default_config():
    return {'schema': 1, 'editions': {key: {'executable': '', 'data_root': ''} for key in SLOTS}}


def load_config(path):
    result = default_config()
    if not path.exists():
        return result
    data = json.loads(path.read_text(encoding='utf-8'))
    if data.get('schema') != 1:
        raise ValueError('Unknown library configuration version')
    for key in SLOTS:
        entry = data.get('editions', {}).get(key, {})
        for field in ('executable', 'data_root'):
            value = entry.get(field, '')
            if not isinstance(value, str):
                raise ValueError('Invalid edition path in the library configuration')
            result['editions'][key][field] = value
    return result


def save_config(path, config):
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + '.tmp')
    temporary.write_text(json.dumps(config, indent=2) + '\n', encoding='utf-8')
    temporary.replace(path)


def launch_spec(config, key):
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
        if other_key != key and other['data_root'] and Path(other['data_root']).resolve() == root:
            raise ValueError('The two editions need separate game-data folders to preserve their saves and caches.')
    env = {k: v for k, v in os.environ.items()
           if not k.upper().startswith('NX_TEST_') and k.upper() not in ('NX_GAMELIB', 'NX_DATA_ROOT')}
    env['NX_DATA_ROOT'] = str(root)
    return [str(executable)], str(executable.parent), env


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    base = Path(os.environ.get('LOCALAPPDATA', Path.home() / '.local/share')) / 'SuperWonders'
    parser.add_argument('--config', type=Path, default=base / 'library.json')
    args = parser.parse_args()
    import tkinter as tk
    from tkinter import filedialog, messagebox

    window = tk.Tk()
    window.title('SuperWonders • Library')
    window.geometry('860x570')
    window.minsize(760, 540)
    bg, panel, text, quiet, accent = '#132822', '#203c32', '#f6f0dc', '#b4c5b5', '#f3bd54'
    window.configure(bg=bg)
    try:
        config = load_config(args.config)
    except (OSError, ValueError) as e:
        messagebox.showerror('Cannot read Wonder library', str(e), parent=window)
        window.destroy()
        return 2
    running, status, buttons = {}, {}, {}
    body = tk.Frame(window, bg=bg, padx=30, pady=24)
    body.pack(fill='both', expand=True)
    tk.Label(body, text='SUPERWONDERS', font=('Segoe UI', 11, 'bold'), fg=accent, bg=bg).pack(anchor='w')
    tk.Label(body, text='Your Flower Kingdom', font=('Georgia', 29), fg=text, bg=bg).pack(anchor='w', pady=(7, 5))
    tk.Label(body, text='Two editions. A place for each adventure.', font=('Segoe UI', 12),
             fg=quiet, bg=bg).pack(anchor='w', pady=(0, 22))

    def refresh(key):
        item = config['editions'][key]
        ready = bool(item['executable'] and item['data_root'])
        live = key in running and running[key].poll() is None
        status[key].set('Running' if live else 'Runtime configured' if ready else 'Choose a prepared game installation')
        buttons[key].configure(state='normal' if ready and not live else 'disabled',
                               text='Running…' if live else 'Play')

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
            launch_spec(config, key)
            save_config(args.config, config)
        except (OSError, ValueError) as e:
            config['editions'][key] = previous
            messagebox.showerror('Cannot configure this edition', str(e), parent=window)
        refresh(key)

    def play(key):
        # One game at a time from this library, without stopping an existing process.
        if any(process.poll() is None for process in running.values()):
            messagebox.showinfo('Wonder is running', 'Close the current game before starting another edition.',
                                parent=window)
            return
        try:
            argv, cwd, env = launch_spec(config, key)
            running[key] = subprocess.Popen(argv, cwd=cwd, env=env)
        except (OSError, ValueError) as e:
            messagebox.showerror('Could not start Wonder', str(e), parent=window)
        refresh(key)

    for key, (title, edition) in SLOTS.items():
        card = tk.Frame(body, bg=panel, padx=20, pady=17)
        card.pack(fill='x', pady=(0, 12))
        actions = tk.Frame(card, bg=panel)
        actions.pack(side='right', padx=(18, 0))
        tk.Label(card, text=edition.upper(), fg=accent, bg=panel, font=('Segoe UI', 9, 'bold')).pack(anchor='w')
        tk.Label(card, text=title, fg=text, bg=panel, font=('Segoe UI', 15, 'bold')).pack(anchor='w', pady=(5, 4))
        status[key] = tk.StringVar()
        tk.Label(card, textvariable=status[key], fg=quiet, bg=panel, font=('Segoe UI', 10)).pack(anchor='w')
        buttons[key] = tk.Button(actions, text='Play', command=lambda k=key: play(k), bg=accent, fg=bg,
            activebackground='#ffd582', relief='flat', width=12, pady=5, font=('Segoe UI', 11, 'bold'))
        buttons[key].pack()
        tk.Button(actions, text='Choose installation', command=lambda k=key: configure(k), relief='flat',
            bg=panel, fg=quiet, activebackground=panel, activeforeground=text, font=('Segoe UI', 9)).pack(pady=(6, 0))
        refresh(key)
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
