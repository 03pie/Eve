"""Drive the registered preview PID through OS UI only; never launch or kill it.

The recorder owns process lifetime, saved-file/exit evidence and package verification.
This driver records each fresh UI observation/action and stops on unknown state.
"""
import argparse
import ctypes
from ctypes import wintypes
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import time


ROOT = Path(__file__).resolve().parents[1]
ACTION = ROOT / 'tools' / 'preview_ui_action.ps1'


class Unknown(RuntimeError):
    """Evidence does not establish the required precondition or UI outcome."""


def write_new(path, value):
    with Path(path).open('x', encoding='utf-8', newline='\n') as stream:
        json.dump(value, stream, ensure_ascii=False, indent=2)
        stream.write('\n')


def read_json(path):
    return json.loads(Path(path).read_text(encoding='utf-8-sig'))


def norm(path):
    return os.path.normcase(os.path.abspath(path))


def id_matches(actual, suffix):
    return actual == suffix or actual.endswith('.' + suffix)


def control(snapshot, suffix='', name=None, visible=True, within=None):
    if snapshot.get('errors') or snapshot.get('truncated'):
        raise Unknown('Incomplete UI snapshot')
    matches = [item for item in snapshot['elements']
               if (not suffix or id_matches(item['id'], suffix))
               and (name is None or item['name'] == name)
               and (not visible or not item['offscreen'])
               and (within is None or in_scope(snapshot, item, within))]
    if len(matches) != 1:
        raise Unknown(f'Expected one control {suffix!r}/{name!r}, found {len(matches)}')
    return matches[0]


def text(snapshot, suffix):
    item = control(snapshot, suffix)
    if item.get('textTruncated') and suffix != 'CordisWorkbenchOutput':
        raise Unknown('Required UI text is truncated: ' + suffix)
    return item['value'] if item.get('value') is not None else item['name']


def in_scope(snapshot, item, scope):
    by_index = {row['index']: row for row in snapshot['elements']}
    seen = set()
    while item['index'] != scope['index']:
        if item['index'] in seen or 'parentIndex' not in item:
            raise Unknown('UI snapshot cannot establish control ancestry')
        seen.add(item['index'])
        parent = item['parentIndex']
        if parent is None:
            return False
        if parent not in by_index:
            raise Unknown('UI snapshot has a missing ancestor')
        item = by_index[parent]
    return True


def config_control(snapshot, field, expected=None):
    """Qt's splitter can remove automation IDs; constrain fallback to the real page."""
    scope = control(snapshot, 'CordisConfigurationPage')
    named = [row for row in snapshot['elements'] if id_matches(row['id'], field) and not row['offscreen']]
    if len(named) > 1:
        raise Unknown('Configuration control ID is not unique')
    if named:
        selected = named[0]
    else:
        candidates = [row for row in snapshot['elements'] if not row['id'] and not row['offscreen']
                      and in_scope(snapshot, row, scope)]
        if field == 'CordisConfigFilePath':
            candidates = [row for row in candidates if row['type'] == 'ControlType.Text'
                          and not row.get('nameTruncated') and norm(row['name']) == norm(expected)]
        elif field == 'CordisConfigEditor':
            candidates = [row for row in candidates if row['type'] == 'ControlType.Edit'
                          and 'ValuePatternIdentifiers.Pattern' in row['patterns']
                          and (expected is None or row.get('value') == expected)]
        else:
            raise Unknown('No supported scoped configuration fallback')
        if len(candidates) != 1:
            raise Unknown(f'Expected one scoped {field}; found {len(candidates)}')
        selected = candidates[0]
    if not in_scope(snapshot, selected, scope) or selected.get('textTruncated'):
        raise Unknown('Configuration control scope/text is incomplete')
    if expected is not None and field == 'CordisConfigEditor' and selected.get('value') != expected:
        raise Unknown('Configuration editor differs from the last observed/written UI text')
    return selected


def module_rows(snapshot):
    rows = snapshot.get('moduleRows', [])
    result = {}
    for index, row in enumerate(rows):
        if len(row) != 4 or not row[0].startswith('example.') or row[0][8:] in result:
            raise Unknown('Unexpected/duplicate module grid row')
        result[row[0][8:]] = {'index': index, 'version': row[1], 'policy': row[2], 'path': row[3]}
    if set(result) != {'counter', 'reporter', 'notebook'}:
        raise Unknown('Module grid does not identify the three real business modules')
    return result


def module_focus_cell(snapshot):
    """A clipped table can expose only its first row; focus precedes row navigation."""
    rows = module_rows(snapshot)
    table = control(snapshot, 'CordisModuleTable')['rect']
    if not table:
        raise Unknown('Module table has no actual rectangle')
    cells = []
    for item in snapshot['elements']:
        rect = item.get('rect')
        if (item['name'] not in {'example.' + alias for alias in rows} or item['type'] != 'ControlType.DataItem'
                or item['offscreen'] or not item['enabled'] or not rect or rect['width'] <= 2 or rect['height'] <= 2):
            continue
        x, y = rect['x'] + rect['width'] / 2, rect['y'] + rect['height'] / 2
        if table['x'] <= x < table['x'] + table['width'] and table['y'] <= y < table['y'] + table['height']:
            cells.append(item)
    if not cells:
        raise Unknown('No actual visible business cell can establish table focus')
    return min(cells, key=lambda item: (item['rect']['y'], item['rect']['x'], item['index']))


def notebook_suggestion(snapshot, search_id):
    """Only this exact page query may use Ela's single unnamed suggestion row."""
    search = control(snapshot, search_id)
    if ('.CordisPageSearch.' not in search['id'] or search['type'] != 'ControlType.Edit'
            or search.get('value') != '插件笔记' or not search['enabled'] or not search['focused']):
        raise Unknown('Actual focused page-search query is not the notebook title')
    popup = control(snapshot, 'ElaSuggestBoxSearchViewBaseWidget.ElaBaseListView')
    if popup['type'] != 'ControlType.List' or popup['class'] != 'ElaBaseListView' or not popup['enabled']:
        raise Unknown('Actual notebook suggestion popup differs')
    items = [item for item in snapshot['elements'] if item['type'] == 'ControlType.ListItem'
             and not item['offscreen'] and in_scope(snapshot, item, popup)]
    if len(items) != 1:
        raise Unknown('Notebook page query does not expose exactly one visible suggestion')
    item = items[0]
    if (not item['enabled'] or item['name'] not in ('', '插件笔记') or item.get('nameTruncated')
            or not item['rect'] or item['rect']['width'] <= 0 or item['rect']['height'] <= 0):
        raise Unknown('Notebook suggestion is unavailable or has conflicting accessible text')
    return item


def applied_operation(report, alias):
    operation = re.search(r'^OperationId #(\d+)$', report, re.M)
    if (not report.startswith('模块 ' + alias + ' · ') or not operation
            or 'committed=1 · recovered=0 · resourcesRetained=0' not in report
            or not re.search(r'\bApplied\n', report)):
        raise Unknown('Current module report does not establish Applied')
    return int(operation[1])


def business_saved(status):
    state = re.search(r'^当前目标修订 (\d+) · 已保存 (\d+) · 配置已同步$', status, re.M)
    receipt = re.search(r'^保存回执：修订 (\d+) · Written$', status, re.M)
    if not state or not receipt or len({state[1], state[2], receipt[1]}) != 1:
        raise Unknown('Save does not establish Written for the current revision')
    return int(state[1])


def config_applied(snapshot):
    match = re.fullmatch(r'运行：已应用 · R(\d+)', text(snapshot, 'CordisConfigSummary'))
    if not match or text(snapshot, 'CordisConfigEditStatus') != '编辑：已应用':
        raise Unknown('Configuration has not been accepted and applied')
    return int(match[1])


def config_saved(snapshot, revision):
    return text(snapshot, 'CordisConfigSaveStatus') == f'磁盘：已保存 R{revision}'


def validate_registration(registration, status, package, run_root, pid, mode, phase, config_file=None):
    if (registration.get('schema') != 'cordis.preview.desktop.registration/1'
            or norm(registration['packageRoot']) != norm(package)
            or norm(registration['dataRoot']) != norm(run_root)
            or status.get('pid') != pid or status.get('entry') != mode
            or status.get('ordinal') != (1 if phase == 'initial' else 2)
            or status.get('phase') != 'user-operating' or status.get('recordingOnly') is not True):
        raise Unknown('Recorder registration/current PID does not match this UI run')
    if mode == 'configuration':
        registered = registration['configurationFile']
        if (not config_file or norm(config_file) != norm(registered)
                or norm(registered) != norm(Path(run_root) / 'configuration' / 'cordis.yaml')):
            raise Unknown('Configuration CLI path is not the registered private test file')
        if os.path.commonpath([norm(registered), norm(run_root)]) != norm(run_root):
            raise Unknown('Configuration file is outside the registered data root')


def config_value(title):
    return {'formatVersion': 1, 'plugins': [{'id': 'notes', 'plugin': 'demo.notes', 'config': {'title': title}}]}


def evidence_label(value):
    if re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9_-]{0,47}', value, flags=re.ASCII) is None:
        raise argparse.ArgumentTypeError('Evidence label must be 1-48 ASCII letters/digits, underscore or hyphen')
    return value


class NativeInput:
    """Actual foreground/point/focus guards surround every OS input operation."""
    def __init__(self, pid, record, observe=None):
        self.pid, self.record, self.observe = pid, record, observe
        self.user = ctypes.WinDLL('user32', use_last_error=True)
        self.kernel = ctypes.WinDLL('kernel32', use_last_error=True)
        self.kernel.GetCurrentThreadId.restype = wintypes.DWORD
        self.user.GetForegroundWindow.restype = wintypes.HWND
        self.user.WindowFromPoint.argtypes = [wintypes.POINT]
        self.user.WindowFromPoint.restype = wintypes.HWND
        self.user.SetForegroundWindow.argtypes = [wintypes.HWND]
        self.user.SetForegroundWindow.restype = wintypes.BOOL
        self.user.SetFocus.argtypes = [wintypes.HWND]
        self.user.SetFocus.restype = wintypes.HWND
        self.user.AttachThreadInput.argtypes = [wintypes.DWORD, wintypes.DWORD, wintypes.BOOL]
        self.user.AttachThreadInput.restype = wintypes.BOOL
        self.user.PeekMessageW.argtypes = [ctypes.POINTER(wintypes.MSG), wintypes.HWND,
                                          wintypes.UINT, wintypes.UINT, wintypes.UINT]
        self.user.PeekMessageW.restype = wintypes.BOOL
        self.user.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
        self.user.GetWindowThreadProcessId.restype = wintypes.DWORD
        if not self.user.IsProcessDPIAware():
            self.user.SetProcessDPIAware()
        if not self.user.IsProcessDPIAware():
            raise Unknown('Cannot establish DPI-aware UI input coordinates')

    def owner(self, hwnd):
        value = wintypes.DWORD()
        self.user.GetWindowThreadProcessId(hwnd, ctypes.byref(value))
        return value.value

    def focus_facts(self):
        class GuiInfo(ctypes.Structure):
            _fields_ = [('cbSize', wintypes.DWORD), ('flags', wintypes.DWORD)] + [
                (key, wintypes.HWND) for key in ('active', 'focus', 'capture', 'menu', 'move', 'caret')
            ] + [('rect', wintypes.RECT)]
        info = GuiInfo(); info.cbSize = ctypes.sizeof(info)
        if not self.user.GetGUIThreadInfo(0, ctypes.byref(info)):
            raise Unknown('Cannot inspect actual input focus')
        return {'foregroundPid': self.owner(self.user.GetForegroundWindow()), 'focusPid': self.owner(info.focus)}

    def guard(self, point=None):
        facts = self.focus_facts()
        if point is not None:
            facts['pointPid'] = self.owner(self.user.WindowFromPoint(wintypes.POINT(*point)))
        if any(value != self.pid for value in facts.values()):
            raise Unknown('Actual OS input would reach another PID: ' + str(facts))
        return facts

    def main_window(self, snapshot):
        if snapshot.get('errors') or snapshot.get('truncated') or snapshot.get('pid') != self.pid:
            raise Unknown('Cannot establish complete live window metadata for the registered PID')
        roots = [item for item in snapshot['roots'] if item['id'] == 'ElaWindow']
        if (len(roots) != 1 or roots[0].get('pid') != self.pid
                or not isinstance(roots[0].get('hwnd'), int) or roots[0]['hwnd'] <= 0
                or self.owner(roots[0]['hwnd']) != self.pid):
            raise Unknown('Cannot identify the live main window')
        return roots[0]['hwnd']

    def wait_guard(self):
        until = time.monotonic() + 1
        while True:
            try:
                return self.guard()
            except Unknown:
                if time.monotonic() >= until:
                    raise
                time.sleep(.03)

    def activate(self, snapshot):
        hwnd = self.main_window(snapshot)
        facts = {'hwnd': hwnd, 'before': self.focus_facts(), 'attached': [], 'detached': []}
        try:
            facts['setForegroundReturn'] = bool(self.user.SetForegroundWindow(hwnd))
            try:
                facts['after'] = self.wait_guard()
                return
            except Unknown as error:
                facts['normalGuardError'] = str(error)
            if self.observe is None or self.main_window(self.observe()) != hwnd:
                raise Unknown('Fresh registered main window changed before activation fallback')
            current = self.kernel.GetCurrentThreadId()
            foreground = self.user.GetForegroundWindow()
            foreground_thread = self.user.GetWindowThreadProcessId(foreground, None)
            target_thread = self.user.GetWindowThreadProcessId(hwnd, None)
            if not current or not foreground_thread or not target_thread or self.owner(hwnd) != self.pid:
                raise Unknown('Cannot establish actual activation thread identities')
            facts['threads'] = {'current': current, 'foreground': foreground_thread, 'target': target_thread}
            message = wintypes.MSG()
            facts['peekMessageReturn'] = bool(self.user.PeekMessageW(ctypes.byref(message), None, 0, 0, 0))
            pairs = list(dict.fromkeys((current, thread) for thread in (foreground_thread, target_thread)
                                       if current != thread))
            attached = []
            try:
                for pair in pairs:
                    ok = bool(self.user.AttachThreadInput(*pair, True))
                    facts['attached'].append({'threads': list(pair), 'ok': ok, 'error': ctypes.get_last_error() if not ok else 0})
                    if not ok:
                        raise Unknown('Cannot temporarily attach activation input queues')
                    attached.append(pair)
                facts['attachedSetForegroundReturn'] = bool(self.user.SetForegroundWindow(hwnd))
                ctypes.set_last_error(0)
                facts['setFocusPreviousHwnd'] = self.user.SetFocus(hwnd) or 0
                facts['setFocusError'] = ctypes.get_last_error()
                facts['whileAttached'] = self.wait_guard()
            finally:
                detach_failed = False
                for pair in reversed(attached):
                    try:
                        ok = bool(self.user.AttachThreadInput(*pair, False))
                        facts['detached'].append({'threads': list(pair), 'ok': ok, 'error': ctypes.get_last_error() if not ok else 0})
                        detach_failed |= not ok
                    except Exception as error:
                        facts['detached'].append({'threads': list(pair), 'ok': False, 'error': str(error)})
                        detach_failed = True
                if detach_failed:
                    raise Unknown('Cannot detach temporary activation input queues; stop without input')
            facts['after'] = self.wait_guard()
        except Exception as error:
            facts['error'] = str(error)
            raise
        finally:
            self.record('activate', facts)

    def click(self, element):
        rect = element['rect']
        if not rect or rect['width'] <= 2 or rect['height'] <= 2:
            raise Unknown('Control has no usable actual rectangle')
        point = (round(rect['x'] + rect['width'] / 2), round(rect['y'] + rect['height'] / 2))
        before = self.guard(point)
        original = wintypes.POINT()
        if not self.user.GetCursorPos(ctypes.byref(original)):
            raise Unknown('Cannot preserve cursor position')
        try:
            if not self.user.SetCursorPos(*point):
                raise Unknown('Cannot move cursor to the verified actual control')
            self.guard(point)
            self.user.mouse_event(0x0002, 0, 0, 0, 0)
            self.user.mouse_event(0x0004, 0, 0, 0, 0)
            after = self.guard(point)
            self.record('mouse', {'point': point, 'before': before, 'after': after, 'control': element})
        finally:
            self.user.SetCursorPos(original.x, original.y)

    def keys(self, groups):
        for group in groups:
            before = self.guard()
            pressed = []
            try:
                for key in group:
                    self.user.keybd_event(key, 0, 0, 0); pressed.append(key)
            finally:
                for key in reversed(pressed):
                    self.user.keybd_event(key, 0, 2, 0)
            self.record('keys', {'keys': group, 'before': before, 'after': self.guard()})


class Driver:
    def __init__(self, args):
        self.args = args
        self.package, self.data = args.package_root.resolve(), args.run_root.resolve()
        label = getattr(args, 'evidence_label', None)
        if label is not None:
            label = evidence_label(label)
        self.output = self.data / (f'ui-{args.mode}-{args.phase}-pid-{args.pid}' + (f'-{label}' if label else ''))
        self.output.mkdir(exist_ok=False)
        self.sequence = 0
        self.operations = []
        self.deadline = time.monotonic() + 240

    def initialize(self):
        self.validate()
        manifest = self.package / 'build-manifest.json'
        if hashlib.sha256(manifest.read_bytes()).hexdigest() != self.registration['manifestSha256']:
            raise Unknown('Registered package manifest differs')
        self.input = NativeInput(self.args.pid, self.record, self.snapshot)

    def validate(self):
        self.registration = read_json(self.data / 'registration.json')
        validate_registration(self.registration, read_json(self.data / 'status.json'), self.package, self.data,
                              self.args.pid, self.args.mode, self.args.phase, self.args.configuration_file)

    def next_path(self, label, extension='json'):
        self.sequence += 1
        return self.output / f'{self.sequence:04d}-{label}.{extension}'

    def record(self, label, facts):
        write_new(self.next_path(label), {'pid': self.args.pid, 'utc': time.time(), **facts})

    def action(self, action, suffix='', name=None, value=None, selector=None):
        self.validate()
        if time.monotonic() >= self.deadline:
            raise Unknown('UI sequence exceeded its time budget')
        request = self.next_path(action + '-request')
        output = request.with_name(request.stem.replace('-request', '-raw') + '.json')
        selected = {'id': suffix, 'name': name, 'scope': None, 'type': None,
                    'emptyId': False, 'matchValue': False, 'expectedValue': None}
        if selector:
            selected.update(selector)
        write_new(request, {'pid': self.args.pid, 'entry': self.args.mode, 'action': action,
                            'selector': selected, 'value': value})
        argv = ['powershell.exe', '-NoProfile', '-NonInteractive', '-STA', '-ExecutionPolicy', 'Bypass',
                '-File', str(ACTION), '-Request', str(request), '-Output', str(output)]
        with request.with_suffix('.stdout').open('xb') as stdout, request.with_suffix('.stderr').open('xb') as stderr:
            result = subprocess.run(argv, stdout=stdout, stderr=stderr, timeout=min(12, max(1, self.deadline-time.monotonic())))
        raw = read_json(output)
        if result.returncode or raw.get('ok') is not True or raw.get('pid') != self.args.pid:
            raise Unknown('UI action failed; see ' + str(output))
        return raw.get('snapshot')

    def snapshot(self):
        # Qt can replace its accessibility tree after accepting a target. Retry only
        # observations; every failed raw reply remains recorded. Never replay input.
        last = None
        for attempt in range(3):
            try:
                return self.action('Read')
            except (Unknown, subprocess.TimeoutExpired) as error:
                last = str(error)
                self.record('read-unknown', {'attempt': attempt + 1, 'error': last})
                if time.monotonic() >= self.deadline:
                    break
                time.sleep(.1)
        raise Unknown('Read-only UI observations remain unavailable: ' + str(last))

    def wait(self, check, description, seconds=25):
        until = min(self.deadline, time.monotonic() + seconds)
        last = None
        while time.monotonic() < until:
            try:
                snapshot = self.snapshot()
                value = check(snapshot)
                if value:
                    return snapshot
            except Unknown as error:
                last = str(error)
            time.sleep(.15)  # Observe asynchronous UI state; never repeat a mutation.
        raise Unknown(description + (': ' + last if last else ''))

    def invoke(self, suffix):
        self.action('Invoke', suffix)

    def menu(self, suffix, parent='文件(F)'):
        self.action('Expand', name=parent)
        self.invoke(suffix)

    def close_dialog(self, title):
        self.action('Close', 'CordisContributionDialog', title)

    def candidate(self, alias, letter):
        subdir = 'qt-upgrade' if alias == 'notebook' else 'native-upgrade'
        path = self.package / subdir / f'{alias}-{letter}' / 'module.json'
        value = read_json(path)  # Static declaration only; never open/hash its live DLL.
        if value['moduleId'] != 'org.cordis.example.' + alias:
            raise Unknown('Candidate identity mismatch')
        return path, value['moduleVersion']

    def grid(self, expected):
        snapshot = self.snapshot(); rows = module_rows(snapshot)
        for alias, letter in expected.items():
            if rows[alias]['version'] != self.candidate(alias, letter)[1]:
                raise Unknown('Current grid version differs: ' + alias)
        return snapshot, rows

    def select_module(self, alias):
        snapshot = self.snapshot(); rows = module_rows(snapshot)
        cell = module_focus_cell(snapshot)
        self.input.activate(snapshot); self.input.click(cell)
        self.input.keys([(0x11, 0x24)] + [(0x28,)] * rows[alias]['index'])

    def upgrade(self, alias, letter):
        path, version = self.candidate(alias, letter)
        self.select_module(alias)
        self.action('SetValue', 'CordisUpgradeSource', value=str(path))
        self.invoke('CordisUpgradePreview')
        self.wait(lambda s: text(s, 'CordisUpgradePreviewText').startswith('org.cordis.example.' + alias + '\n')
                  and text(s, 'CordisUpgradeSource') == str(path)
                  and control(s, 'CordisUpgradeStart')['enabled'], 'Candidate preview/selected row mismatch')
        self.invoke('CordisUpgradeStart')
        snapshot = self.wait(lambda s: applied_operation(text(s, 'CordisUpgradeReportText'), alias)
                             and module_rows(s)[alias]['version'] == version, 'Upgrade did not reach Applied')
        operation = applied_operation(text(snapshot, 'CordisUpgradeReportText'), alias)
        if operation in self.operations:
            raise Unknown('Stale OperationId reused')
        self.operations.append(operation)
        if '配置未保存' not in text(snapshot, 'CordisUpgradeSaveStatus'):
            raise Unknown('Applied target did not become dirty before explicit Save')
        self.invoke('CordisUpgradeSave')
        saved = self.wait(lambda s: business_saved(text(s, 'CordisUpgradeSaveStatus')), 'Written receipt missing')
        self.record('upgrade-result', {'alias': alias, 'letter': letter, 'operation': operation,
                                      'version': version, 'savedRevision': business_saved(text(saved, 'CordisUpgradeSaveStatus'))})

    def open_notebook(self):
        snapshot = self.snapshot()
        edits = [item for item in snapshot['elements'] if '.CordisPageSearch.' in item['id']
                 and item['type'] == 'ControlType.Edit' and not item['offscreen']]
        if len(edits) != 1:
            raise Unknown('Cannot identify actual page-search editor')
        edit = edits[0]
        self.action('SetValue', edit['id'], value='插件笔记')
        self.input.activate(self.snapshot()); self.input.click(edit)
        # Real editing triggers the suggestion filter (UIA SetValue alone is programmatic textChanged).
        self.input.keys([(0x23,), (0x20,), (0x08,)])
        self.wait(lambda s: notebook_suggestion(s, edit['id']), 'Notebook suggestion missing')
        self.input.keys([(0x0D,)])
        self.wait(lambda s: control(s, 'CordisNotebookEditor')['enabled'], 'Notebook page did not open')

    def show_output(self, identity):
        snapshot = self.snapshot()
        panel = control(snapshot, 'CordisWorkbenchPanel', visible=False)
        if panel['offscreen']:
            self.invoke('CordisTogglePanel')
            snapshot = self.snapshot()
        tabs = [item for item in snapshot['elements'] if item['type'] == 'ControlType.TabItem'
                and item['name'] == '输出' and not item['offscreen']]
        if len(tabs) != 1:
            raise Unknown('Cannot identify the actual output tab')
        self.input.activate(snapshot); self.input.click(tabs[0])
        snapshot = self.snapshot()
        self.input.click(control(snapshot, 'CordisOutputTarget'))
        channel = f'notebook · 实例 #{identity[0]} / 代 {identity[1]}'
        snapshot = self.wait(lambda s: control(s, name=channel, within=control(s, 'CordisOutputTarget'))['rect'],
                             'Current notebook output channel is not visible')
        self.input.click(control(snapshot, name=channel, within=control(snapshot, 'CordisOutputTarget')))

    def publish(self, marker, command=False, owner=None):
        self.action('SetValue', 'CordisNotebookEditor', value=marker)
        self.wait(lambda s: text(s, 'CordisNotebookEditor') == marker, 'Notebook edit was not accepted')
        if command:
            self.menu('CordisOpenCommands', parent='视图(V)')
            self.action('SetValue', 'CordisCommandFilter', value='notebook.publish')
            snapshot = self.wait(lambda s: control(s, 'CordisRunCommand')['enabled'], 'Notebook command unavailable')
            entries = [item for item in snapshot['elements'] if item['type'] == 'ControlType.ListItem'
                       and '输出笔记' in item['name'] and not item['offscreen']]
            if len(entries) != 1:
                raise Unknown('Filtered notebook command is not unique')
            identity = re.search(r'插件 #(\d+) / 代 (\d+)$', entries[0]['name'])
            if not identity:
                raise Unknown('Notebook command has no actual contribution generation')
            self.input.activate(snapshot); self.input.click(entries[0]); self.invoke('CordisRunCommand')
            result = self.wait(lambda s: text(s, 'CordisCommandResult') == '执行完成：插件笔记：输出笔记',
                               'Notebook command result did not complete successfully')
            self.record('notebook-command', {'contribution': entries[0]['name'], 'identity': list(identity.groups()),
                                            'result': text(result, 'CordisCommandResult'), 'marker': marker})
            owner = identity.groups()
        else:
            if owner is None:
                raise Unknown('The button output requires an observed current command owner')
            self.invoke('CordisNotebookPublish')
            self.wait(lambda s: text(s, 'CordisNotebookFeedback') == '已写入当前实例的输出通道。', 'Notebook publish failed')
        self.show_output(owner)
        self.wait(lambda s: marker in text(s, 'CordisWorkbenchOutput'), 'Actual notebook output marker missing')
        return identity.groups() if command else None

    def business(self):
        self.menu('CordisOpenModules')
        expected = {'counter': 'a', 'reporter': 'a', 'notebook': 'a' if self.args.phase == 'initial' else 'b'}
        self.grid(expected); self.close_dialog('模块管理')
        marker = 'P6 UI 笔记 ' + self.output.name
        self.open_notebook()
        original_identity = self.publish(marker + ' initial command', command=True)
        draft_marker = marker + ' initial button'
        self.publish(draft_marker, owner=original_identity)
        if self.args.phase == 'initial':
            self.menu('CordisOpenModules')
            for alias, letter in [('counter', 'b'), ('reporter', 'b'), ('counter', 'a'), ('reporter', 'a')]:
                self.upgrade(alias, letter)
            self.close_dialog('模块管理')
            if text(self.snapshot(), 'CordisNotebookEditor') != draft_marker:
                raise Unknown('Native roundtrip lost the active notebook draft')
            self.menu('CordisOpenModules'); self.upgrade('notebook', 'b'); self.close_dialog('模块管理')
            self.open_notebook()
            if text(self.snapshot(), 'CordisNotebookEditor') != draft_marker:
                raise Unknown('Notebook A-to-B migration did not retain the live draft')
        current_identity = self.publish(marker + ' B command', command=True)
        if self.args.phase == 'initial' and current_identity == original_identity:
            raise Unknown('Notebook B did not publish a new command contribution generation')
        self.publish(marker + ' B button', owner=current_identity)
        self.menu('CordisOpenModules'); self.grid({'counter': 'a', 'reporter': 'a', 'notebook': 'b'})
        self.close_dialog('模块管理')

    def config_path(self, snapshot):
        actual = config_control(snapshot, 'CordisConfigFilePath', str(self.args.configuration_file))
        if actual.get('nameTruncated') or norm(actual['name']) != norm(self.args.configuration_file):
            raise Unknown('Actual configuration UI path differs from the registered Unicode CLI path')

    def accept_config(self, value):
        snapshot = self.snapshot(); self.config_path(snapshot)
        actual = config_control(snapshot, 'CordisConfigEditor', self.editor_text)
        selector = {'id': actual['id'], 'scope': 'CordisConfigurationPage', 'type': 'ControlType.Edit',
                    'emptyId': not actual['id'], 'matchValue': True, 'expectedValue': self.editor_text}
        wanted = json.dumps(value, ensure_ascii=False, indent=2)
        self.action('SetValue', value=wanted, selector=selector)
        self.wait(lambda s: config_control(s, 'CordisConfigEditor', wanted), 'Configuration editor write not observed')
        self.editor_text = wanted
        self.invoke('CordisConfigPreview')
        self.wait(lambda s: control(s, 'CordisConfigApply')['enabled'], 'Configuration preview is not applicable')
        self.invoke('CordisConfigApply')
        return self.wait(config_applied, 'Configuration acceptance did not apply')

    def configuration(self):
        self.menu('CordisConfigurationFile')
        snapshot = self.wait(lambda s: config_control(s, 'CordisConfigEditor')['enabled'], 'Configuration page missing')
        self.config_path(snapshot)
        self.editor_text = config_control(snapshot, 'CordisConfigEditor')['value']
        expected = config_value('PreviewUI-V2')
        file = self.args.configuration_file
        if file.exists() and file.read_text(encoding='utf-8-sig') != self.editor_text:
            raise Unknown('Registered initial file differs from the actual UI editor source')
        self.record('configuration-initial-ui', {'path': str(file), 'source': self.editor_text, 'fileExists': file.exists()})
        if self.args.phase == 'reopen':
            if json.loads(self.editor_text) != expected:
                raise Unknown('Reopened editor does not contain saved V2')
            self.accept_config(expected)
        else:
            accepted = self.accept_config(config_value('PreviewUI-V1')); revision1 = config_applied(accepted)
            self.invoke('CordisConfigSave')
            self.wait(lambda s: config_saved(s, revision1), 'V1 not saved at its accepted revision')
            v1 = file.read_bytes(); self.record('v1-saved', {'sha256': hashlib.sha256(v1).hexdigest(), 'text': v1.decode('utf-8-sig')})
            if json.loads(v1) != config_value('PreviewUI-V1'):
                raise Unknown('V1 Written UI state does not match actual private file')
            accepted = self.accept_config(expected); revision2 = config_applied(accepted)
            if revision2 <= revision1:
                raise Unknown('V2 did not create a new accepted revision')
            self.wait(lambda s: text(s, 'CordisConfigSaveStatus') == '磁盘：未保存', 'V2 did not become dirty')
            self.validate(); self.config_path(self.snapshot())
            for ancestor in [file, *file.parents]:
                if ancestor.exists() and (ancestor.is_symlink() or getattr(ancestor.lstat(), 'st_file_attributes', 0) & 0x400):
                    raise Unknown('Private configuration path crosses a reparse point')
            if file.read_bytes() != v1:
                raise Unknown('Private file changed before the intentional external write')
            external = (json.dumps(config_value('PreviewUI-VExternal'), ensure_ascii=False, indent=2) + '\n').encode('utf-8')
            with self.next_path('external-original', 'bin').open('xb') as saved:
                saved.write(v1)
            file.write_bytes(external)  # Only the explicitly registered new test YAML; never a business session.
            self.record('external-write', {'path': str(file), 'beforeSha256': hashlib.sha256(v1).hexdigest(),
                                          'externalSha256': hashlib.sha256(external).hexdigest(), 'externalText': external.decode()})
            self.invoke('CordisConfigSave')
            conflict = self.wait(lambda s: text(s, 'CordisConfigSaveStatus') == '磁盘：保存冲突', 'Save conflict missing')
            if file.read_bytes() != external or json.loads(config_control(conflict, 'CordisConfigEditor', self.editor_text)['value']) != expected:
                raise Unknown('Conflict overwrote external bytes or lost accepted V2 editor')
            if config_applied(conflict) != revision2:
                raise Unknown('Conflict did not retain the accepted V2 revision')
            self.invoke('CordisConfigRebase')
            self.wait(lambda s: config_applied(s) == revision2
                      and text(s, 'CordisConfigSaveStatus') == '磁盘：未保存'
                      and control(s, 'CordisConfigSave')['enabled']
                      and config_control(s, 'CordisConfigEditor', self.editor_text), 'Rebase did not retain dirty V2')
            self.invoke('CordisConfigSave')
            self.wait(lambda s: config_saved(s, revision2), 'Rebased V2 not saved at its accepted revision')
            saved = file.read_bytes()
            if json.loads(saved) != expected:
                raise Unknown('Explicit post-rebase save does not contain accepted V2')
            self.record('v2-saved', {'sha256': hashlib.sha256(saved).hexdigest(), 'text': saved.decode('utf-8-sig')})
        self.close_dialog('配置管理')

    def run(self):
        self.initialize()
        self.input.activate(self.snapshot())
        if self.args.mode == 'business':
            self.business()
        else:
            self.configuration()
        self.action('Close', 'ElaWindow')


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pid', type=int, required=True)
    parser.add_argument('--package-root', type=Path, required=True)
    parser.add_argument('--run-root', type=Path, required=True)
    parser.add_argument('--mode', choices=['business', 'configuration'], required=True)
    parser.add_argument('--phase', choices=['initial', 'reopen'], default='initial')
    parser.add_argument('--configuration-file', type=Path)
    parser.add_argument('--evidence-label', type=evidence_label,
                        help='Explicit new evidence suffix; does not authorize replay or change the registered run')
    args = parser.parse_args(argv)
    driver = None
    try:
        if os.name != 'nt' or args.pid <= 0:
            raise Unknown('A positive registered Windows PID is required')
        if args.configuration_file:
            if not args.configuration_file.is_absolute():
                raise Unknown('Configuration file must be an explicit absolute path')
            args.configuration_file = args.configuration_file.absolute()
        driver = Driver(args); driver.run()
        write_new(driver.output / 'summary.json', {'schema': 'cordis.preview.ui.sequence/1', 'pid': args.pid,
                  'mode': args.mode, 'phase': args.phase, 'outcome': 'UISequenceCompletedNormalCloseRequested',
                  'operationIds': driver.operations, 'exitEvidenceOwner': 'record-desktop.ps1', 'targetKilled': False})
        return 0
    except Exception as error:
        if driver:
            write_new(driver.output / 'summary.json', {'schema': 'cordis.preview.ui.sequence/1', 'pid': args.pid,
                      'mode': args.mode, 'phase': args.phase, 'outcome': 'Unknown', 'error': str(error), 'targetKilled': False})
        print('Preview UI unknown: ' + str(error))
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
