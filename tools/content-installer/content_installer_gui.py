"""PS5X360 Content Installer: a window that installs Xbox 360 DLC and title updates into PS5X360
on the PS5 over FTP. Drop files or folders on the list, check the console address, press Install.
The work is done by content_installer.py; built into one .exe by build.py.
"""
import ftplib
import queue
import sys
import threading
import tkinter as tk
from pathlib import Path
from tkinter import filedialog, messagebox, ttk

import content_installer as ci

try:
    from tkinterdnd2 import DND_FILES, TkinterDnD
except ImportError:  # still usable through the Add buttons
    TkinterDnD = None

APP = 'PS5X360 Content Installer'


class App:
    def __init__(self):
        self.root = TkinterDnD.Tk() if TkinterDnD else tk.Tk()
        self.root.title(APP)
        try:
            self.root.iconbitmap(default=str(ci.resource('icon.ico')))
        except tk.TclError:
            pass  # no icon file beside the script
        self.root.geometry('980x620')
        self.root.minsize(760, 460)
        self.titles = ci.load_titles()
        self.packages = []
        self.events = queue.Queue()
        self.busy = False

        top = ttk.Frame(self.root, padding=(10, 10, 10, 4))
        top.pack(fill='x')
        ttk.Label(top, text='PS5 IP address').pack(side='left')
        # Left empty: every console has its own address (PS5 Settings > Network > Connection status).
        self.host = tk.StringVar()
        host_entry = ttk.Entry(top, textvariable=self.host, width=18)
        host_entry.pack(side='left', padx=(6, 12))
        host_entry.focus_set()
        ttk.Label(top, text='FTP port').pack(side='left')
        self.port = tk.StringVar(value=str(ci.FTP_PORT))
        ttk.Entry(top, textvariable=self.port, width=7).pack(side='left', padx=(6, 12))
        self.check_button = ttk.Button(top, text='Check console', command=self.check)
        self.check_button.pack(side='left')
        self.console_state = ttk.Label(top, text='Type your PS5 IP address, then Check console')
        self.console_state.pack(side='left', padx=12)

        hint = 'Drop DLC and title update files or folders here' if TkinterDnD else \
            'Add DLC and title update files or folders'
        ttk.Label(self.root, text=hint, padding=(10, 2)).pack(anchor='w')
        middle = ttk.Frame(self.root, padding=(10, 0))
        middle.pack(fill='both', expand=True)
        columns = ('game', 'kind', 'content', 'file', 'status')
        self.tree = ttk.Treeview(middle, columns=columns, show='headings', selectmode='extended')
        for name, text, width in (('game', 'Game', 230), ('kind', 'Kind', 90), ('content', 'Content', 250),
                                  ('file', 'File', 190), ('status', 'Status', 200)):
            self.tree.heading(name, text=text)
            self.tree.column(name, width=width, anchor='w')
        bar = ttk.Scrollbar(middle, orient='vertical', command=self.tree.yview)
        self.tree.configure(yscrollcommand=bar.set)
        self.tree.pack(side='left', fill='both', expand=True)
        bar.pack(side='right', fill='y')
        self.tree.tag_configure('refused', foreground='#a33')
        if TkinterDnD:
            for widget in (self.root, self.tree):
                widget.drop_target_register(DND_FILES)
                widget.dnd_bind('<<Drop>>', self.dropped)

        buttons = ttk.Frame(self.root, padding=(10, 6))
        buttons.pack(fill='x')
        ttk.Button(buttons, text='Add files...', command=self.add_files).pack(side='left')
        ttk.Button(buttons, text='Add folder...', command=self.add_folder).pack(side='left', padx=6)
        ttk.Button(buttons, text='Remove selected', command=self.remove).pack(side='left')
        ttk.Button(buttons, text='Clear', command=self.clear).pack(side='left', padx=6)
        self.install_button = ttk.Button(buttons, text='Install', command=self.install)
        self.install_button.pack(side='right')
        self.summary = ttk.Label(buttons, text='')
        self.summary.pack(side='right', padx=12)

        bottom = ttk.Frame(self.root, padding=(10, 0, 10, 10))
        bottom.pack(fill='x')
        self.progress = ttk.Progressbar(bottom, maximum=1000)
        self.progress.pack(fill='x')
        self.status = ttk.Label(bottom, text='')
        self.status.pack(anchor='w', pady=(2, 4))
        self.log = tk.Text(bottom, height=8, wrap='none')
        self.log.pack(fill='x')
        self.root.after(100, self.pump)

    # The list.

    def dropped(self, event):
        self.add(self.root.tk.splitlist(event.data))

    def add_files(self):
        self.add(filedialog.askopenfilenames(title='Xbox 360 packages'))

    def add_folder(self):
        folder = filedialog.askdirectory(title='Folder with Xbox 360 packages')
        if folder:
            self.add([folder])

    def add(self, paths):
        if self.busy or not paths:
            return
        known = {str(p.path.resolve()).lower() for p in self.packages}
        for p in ci.collect(paths):
            if str(p.path.resolve()).lower() in known:
                continue
            self.packages.append(p)
            game = p.title or ''
            name = self.titles.get(p.title or '') or p.title_name
            if name:
                game += f'  {name}'
            kind = {'dlc': 'DLC', 'update': 'Title update'}.get(p.kind, 'Refused')
            status = f'Refused: {p.reason}' if p.reason else 'Ready'
            self.tree.insert('', 'end', iid=str(id(p)), values=(game, kind, p.summary if not p.reason else '',
                                                               p.file, status),
                             tags=('refused',) if p.reason else ())
        self.update_summary()

    def remove(self):
        if self.busy:
            return
        chosen = set(self.tree.selection())
        self.packages = [p for p in self.packages if str(id(p)) not in chosen]
        self.tree.delete(*chosen)
        self.update_summary()

    def clear(self):
        if not self.busy:
            self.packages = []
            self.tree.delete(*self.tree.get_children())
            self.update_summary()

    def update_summary(self):
        dlc = sum(p.kind == 'dlc' for p in self.packages)
        updates = sum(p.kind == 'update' for p in self.packages)
        refused = sum(bool(p.reason) for p in self.packages)
        self.summary.config(text=f'{dlc} DLC, {updates} title update(s), {refused} refused')

    def set_status(self, iid, text):
        values = list(self.tree.item(iid, 'values'))
        values[4] = text
        self.tree.item(iid, values=values)

    # The console, on a worker thread; the window is updated from self.events.

    def console(self):
        try:
            port = int(self.port.get())
        except ValueError:
            port = ci.FTP_PORT
        host = self.host.get().strip()
        if not host:
            messagebox.showinfo(APP, "Type your PS5's IP address first (PS5 Settings > Network > "
                                     "Connection status, for example 192.168.0.20).")
            return None
        return ci.Console(host, port)

    def start(self, work):
        self.busy = True
        self.check_button.state(['disabled'])
        self.install_button.state(['disabled'])
        threading.Thread(target=work, daemon=True).start()

    def check(self):
        console = self.console()
        if not console:
            return

        def work():
            try:
                problem = console.check()
                self.events.put(('console', problem or 'Ready: PS5X360 found and closed'))
            except (OSError, ftplib.all_errors) as error:
                self.events.put(('console', f'Cannot connect to {console.host}:{console.port} ({error})'))
            finally:
                console.close()
                self.events.put(('done', None))
        self.start(work)

    def install(self):
        accepted = [p for p in self.packages if not p.reason]
        if not accepted:
            messagebox.showinfo(APP, 'There is nothing to install: add DLC or title update files.')
            return
        console = self.console()
        if not console:
            return

        def work():
            try:
                problem = console.check()
                self.events.put(('console', problem or 'Ready: PS5X360 found and closed'))
                if problem:
                    self.events.put(('message', problem))
                    return
                results = ci.install(console, self.packages,
                                     log=lambda line: self.events.put(('log', line)),
                                     progress=lambda done, total: self.events.put(('progress', done / total)),
                                     status=lambda text: self.events.put(('status', text)))
                for r in results:
                    self.events.put(('row', (str(id(r.package)), r.state, r.detail)))
                text = ci.report(results, console.host)
                self.events.put(('report', text))
            except (OSError, ftplib.all_errors) as error:
                self.events.put(('message', f'The installation stopped: {error}'))
            finally:
                console.close()
                self.events.put(('done', None))
        self.log.delete('1.0', 'end')
        self.start(work)

    def pump(self):
        while True:
            try:
                kind, value = self.events.get_nowait()
            except queue.Empty:
                break
            if kind == 'console':
                self.console_state.config(text=value)
            elif kind == 'log':
                self.log.insert('end', value + '\n')
                self.log.see('end')
            elif kind == 'progress':
                self.progress['value'] = value * 1000
            elif kind == 'status':
                self.status.config(text=value)
            elif kind == 'row':
                iid, state, detail = value
                self.set_status(iid, f'{state.capitalize()}: {detail}')
            elif kind == 'message':
                messagebox.showwarning(APP, value)
            elif kind == 'report':
                self.finish(value)
            elif kind == 'done':
                self.busy = False
                self.check_button.state(['!disabled'])
                self.install_button.state(['!disabled'])
        self.root.after(100, self.pump)

    def finish(self, text):
        self.status.config(text='Finished')
        self.log.insert('end', '\n' + text + '\n')
        self.log.see('end')
        where = Path(sys.executable if getattr(sys, 'frozen', False) else __file__).resolve().parent
        saved = ''
        try:
            path = where / f'content-installer-report-{ci.datetime.datetime.now():%Y%m%d-%H%M%S}.txt'
            path.write_text(text, 'utf-8')
            saved = f'\n\nReport saved to {path}'
        except OSError:
            pass
        messagebox.showinfo(APP, text.split('\n', 2)[1] + saved)


if __name__ == '__main__':
    App().root.mainloop()
