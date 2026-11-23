"""Pure evidence/precondition checks: no Windows UI, process or product execution."""
import argparse
import copy
from pathlib import Path
import sys
import tempfile
import time
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools'))
import preview_ui as ui


def snapshot(*elements):
    return {'errors': [], 'truncated': False, 'elements': [
        {'id': item[0], 'name': item[1], 'value': None, 'offscreen': False} for item in elements]}


class PreviewUiValues(unittest.TestCase):
    def test_evidence_label_is_only_an_exclusive_ascii_directory_suffix(self):
        with tempfile.TemporaryDirectory() as directory:
            args = argparse.Namespace(package_root=Path(directory), run_root=Path(directory),
                                      mode='business', phase='reopen', pid=42, evidence_label=None)
            original = ui.Driver(args)
            self.assertEqual(original.output.name, 'ui-business-reopen-pid-42')
            ui.write_new(original.output / 'summary.json', {'outcome': 'Unknown'})
            before = (original.output / 'summary.json').read_bytes()
            args.evidence_label = 'continuation-1'
            continued = ui.Driver(args)
            self.assertEqual(continued.output.name, 'ui-business-reopen-pid-42-continuation-1')
            self.assertEqual((args.mode, args.phase, args.pid, continued.data),
                             ('business', 'reopen', 42, Path(directory).resolve()))
            with self.assertRaises(FileExistsError): ui.Driver(args)
            self.assertEqual((original.output / 'summary.json').read_bytes(), before)
            for label in ('../escape', '..', 'a/b', r'a\b', 'a:b', 'a b', '中文', '', '-start', 'a' * 49):
                args.evidence_label = label
                with self.subTest(label=label), self.assertRaises(argparse.ArgumentTypeError): ui.Driver(args)
            self.assertEqual(ui.evidence_label('A_1-z'), 'A_1-z')
            self.assertEqual(ui.evidence_label('a' * 48), 'a' * 48)

    def test_selector_has_component_boundary_and_unique_visible_match(self):
        value = snapshot(('root.CordisConfigEditor', 'one'), ('OtherCordisConfigEditor', 'two'))
        self.assertEqual(ui.text(value, 'CordisConfigEditor'), 'one')
        value['elements'].append(dict(value['elements'][0]))
        with self.assertRaises(ui.Unknown): ui.control(value, 'CordisConfigEditor')
        value['elements'][-1]['offscreen'] = True
        self.assertEqual(ui.text(value, 'CordisConfigEditor'), 'one')

    def test_output_target_scope_keeps_one_of_two_popup_root_representations(self):
        channel = 'notebook · 实例 #3 / 代 1'
        def row(index, parent, identifier='', name=''):
            return {'index': index, 'parentIndex': parent, 'id': identifier, 'name': name, 'offscreen': False}
        value = {'errors': [], 'truncated': False, 'elements': [
            row(0, None, 'CordisOutputTarget.ElaComboBoxContainer'), row(1, 0, 'ElaComboBoxView'), row(2, 1, name=channel),
            row(3, None, 'ElaWindow'), row(4, 3, 'ElaWindow.CordisOutputTarget', '输出目标'),
            row(5, 4, 'ElaComboBoxView'), row(6, 5, name=channel)]}
        with self.assertRaises(ui.Unknown): ui.control(value, name=channel)
        target = ui.control(value, 'CordisOutputTarget')
        self.assertEqual(ui.control(value, name=channel, within=target)['index'], 6)
        value['elements'].append(row(7, 5, name=channel))
        with self.assertRaises(ui.Unknown): ui.control(value, name=channel, within=target)

    def test_incomplete_or_truncated_required_text_is_unknown(self):
        value = snapshot(('CordisNotebookEditor', 'draft'))
        value['errors'] = ['provider timeout']
        with self.assertRaises(ui.Unknown): ui.text(value, 'CordisNotebookEditor')
        value['errors'] = []; value['elements'][0]['textTruncated'] = True
        with self.assertRaises(ui.Unknown): ui.text(value, 'CordisNotebookEditor')
        value['elements'][0]['id'] = 'CordisWorkbenchOutput'
        self.assertEqual(ui.text(value, 'CordisWorkbenchOutput'), 'draft')

    def test_report_rejects_stale_other_alias_and_non_applied_states(self):
        report = '模块 counter · previous #1 · candidate #2\nOperationId #5\ncommitted=1 · recovered=0 · resourcesRetained=0\n业务已共同提交。 Applied\n'
        self.assertEqual(ui.applied_operation(report, 'counter'), 5)
        for invalid in (report.replace('counter', 'reporter'), report.replace('Applied', 'NotApplied'),
                        report.replace('recovered=0', 'recovered=1'), report.replace('resourcesRetained=0', 'resourcesRetained=1')):
            with self.assertRaises(ui.Unknown): ui.applied_operation(invalid, 'counter')

    def test_written_must_match_both_current_and_persisted_revision(self):
        saved = '当前目标修订 3 · 已保存 3 · 配置已同步\n保存回执：修订 3 · Written'
        self.assertEqual(ui.business_saved(saved), 3)
        for bad in (saved.replace('修订 3 · Written', '修订 2 · Written'), saved.replace('Written', 'Unchanged'),
                    saved.replace('已保存 3', '已保存 2'), saved.replace('配置已同步', '配置未保存')):
            with self.assertRaises(ui.Unknown): ui.business_saved(bad)

    def test_grid_identity_is_unique_and_complete(self):
        value = {'moduleRows': [['example.' + alias, '1.1.0', 'policy', alias] for alias in ('counter', 'reporter', 'notebook')]}
        self.assertEqual(ui.module_rows(value)['notebook']['index'], 2)
        for rows in (value['moduleRows'][:2], value['moduleRows'] + [value['moduleRows'][0]], [['not-a-plugin', '', '', '']]):
            with self.assertRaises(ui.Unknown): ui.module_rows({'moduleRows': rows})

    def test_offscreen_target_uses_visible_table_cell_then_actual_grid_index(self):
        value = snapshot(('CordisModuleTable', ''))
        value['elements'][0].update(index=0, type='ControlType.Table', enabled=True,
                                    rect={'x': 100, 'y': 100, 'width': 400, 'height': 54})
        value['moduleRows'] = [['example.' + alias, '1.1.0', 'policy', alias]
                               for alias in ('counter', 'reporter', 'notebook')]
        for index, alias in enumerate(('counter', 'reporter', 'notebook'), 1):
            value['elements'].append({'index': index, 'id': '', 'name': 'example.' + alias,
                                      'type': 'ControlType.DataItem', 'enabled': True, 'offscreen': index != 1,
                                      'rect': {'x': 100, 'y': 125 + (index-1)*30, 'width': 120, 'height': 24}})
        driver = ui.Driver.__new__(ui.Driver)
        driver.snapshot = lambda: value
        driver.input = mock.Mock()
        driver.select_module('notebook')
        driver.input.click.assert_called_once_with(value['elements'][1])
        driver.input.keys.assert_called_once_with([(0x11, 0x24), (0x28,), (0x28,)])
        # A provider may not mark clipped rows offscreen; their actual centers still fail visibility.
        for row in value['elements'][1:]: row['offscreen'] = False
        self.assertEqual(ui.module_focus_cell(value)['name'], 'example.counter')
        value['elements'][1]['rect']['x'] = 600
        with self.assertRaises(ui.Unknown): ui.module_focus_cell(value)

    def test_anonymous_notebook_suggestion_is_limited_to_exact_focused_query_and_popup(self):
        search_id = 'ElaWindow.CordisCommandCenter.CordisPageSearch.ElaLineEdit'
        value = {'errors': [], 'truncated': False, 'elements': [
            {'index': 0, 'parentIndex': None, 'id': 'ElaWindow', 'name': '', 'type': 'ControlType.Window', 'offscreen': False},
            {'index': 1, 'parentIndex': 0, 'id': search_id, 'name': '', 'type': 'ControlType.Edit',
             'value': '插件笔记', 'focused': True, 'enabled': True, 'offscreen': False},
            {'index': 2, 'parentIndex': 0, 'id': 'ElaWindow.ElaSuggestBoxSearchViewBaseWidget.ElaBaseListView',
             'name': '', 'type': 'ControlType.List', 'class': 'ElaBaseListView', 'enabled': True, 'offscreen': False},
            {'index': 3, 'parentIndex': 2, 'id': '', 'name': '', 'type': 'ControlType.ListItem',
             'enabled': True, 'offscreen': False, 'rect': {'x': 941, 'y': 117, 'width': 356, 'height': 40}}]}
        self.assertEqual(ui.notebook_suggestion(value, search_id)['index'], 3)
        for field, replacement in [('value', 'other page'), ('focused', False)]:
            changed = copy.deepcopy(value); changed['elements'][1][field] = replacement
            with self.subTest(search=field), self.assertRaises(ui.Unknown): ui.notebook_suggestion(changed, search_id)
        for field, replacement in [('parentIndex', 0), ('name', 'other page'), ('offscreen', True)]:
            changed = copy.deepcopy(value); changed['elements'][3][field] = replacement
            with self.subTest(item=field), self.assertRaises(ui.Unknown): ui.notebook_suggestion(changed, search_id)
        changed = copy.deepcopy(value)
        extra = dict(changed['elements'][3], index=4); changed['elements'].append(extra)
        with self.assertRaises(ui.Unknown): ui.notebook_suggestion(changed, search_id)

    def test_configuration_applied_checks_editor_and_runtime(self):
        value = snapshot(('CordisConfigSummary', '运行：已应用 · R2'), ('CordisConfigEditStatus', '编辑：已应用'))
        self.assertEqual(ui.config_applied(value), 2)
        value['elements'][1]['name'] = '编辑：尚未应用'
        with self.assertRaises(ui.Unknown): ui.config_applied(value)

    def test_saved_configuration_revision_cannot_reuse_previous_receipt(self):
        value = snapshot(('CordisConfigSaveStatus', '磁盘：已保存 R1'))
        self.assertTrue(ui.config_saved(value, 1))
        self.assertFalse(ui.config_saved(value, 2))

    def test_menu_parent_is_explicit_while_file_menu_stays_default(self):
        driver = ui.Driver.__new__(ui.Driver)
        driver.action = mock.Mock()
        driver.menu('CordisOpenModules')
        self.assertEqual(driver.action.call_args_list,
                         [mock.call('Expand', name='文件(F)'), mock.call('Invoke', 'CordisOpenModules')])
        driver.action.reset_mock()
        driver.menu('CordisOpenCommands', parent='视图(V)')
        self.assertEqual(driver.action.call_args_list,
                         [mock.call('Expand', name='视图(V)'), mock.call('Invoke', 'CordisOpenCommands')])

    def test_splitter_empty_ids_require_real_page_ancestry_and_exact_editor_value(self):
        def row(index, parent, kind, value=None, name='', identifier=''):
            return {'index': index, 'parentIndex': parent, 'id': identifier, 'name': name, 'value': value,
                    'offscreen': False, 'enabled': True, 'type': kind, 'patterns': ['ValuePatternIdentifiers.Pattern']}
        value = {'errors': [], 'truncated': False, 'elements': [
            row(0, None, 'ControlType.Window', identifier='ElaWindow'),
            row(1, 0, 'ControlType.Group', identifier='ElaWindow.CordisConfigurationPage'),
            row(2, 1, 'ControlType.Group'), row(3, 2, 'ControlType.Edit', 'registered text'),
            row(4, 1, 'ControlType.Edit', 'details', identifier='CordisConfigDetails'),
            row(5, 0, 'ControlType.Edit', 'registered text')]}
        self.assertEqual(ui.config_control(value, 'CordisConfigEditor', 'registered text')['index'], 3)
        with self.assertRaises(ui.Unknown): ui.config_control(value, 'CordisConfigEditor', 'other text')
        value['elements'].append(row(6, 2, 'ControlType.Edit', 'registered text'))
        with self.assertRaises(ui.Unknown): ui.config_control(value, 'CordisConfigEditor', 'registered text')
        value['elements'].pop(); value['elements'][2]['parentIndex'] = 2
        with self.assertRaises(ui.Unknown): ui.config_control(value, 'CordisConfigEditor', 'registered text')

    def test_registration_never_authorizes_another_pid_phase_or_file(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / '扩展中文 space'; config = root / 'configuration' / 'cordis.yaml'
            reg = {'schema': 'cordis.preview.desktop.registration/1', 'packageRoot': str(root / 'package'),
                   'dataRoot': str(root), 'configurationFile': str(config)}
            status = {'pid': 42, 'entry': 'configuration', 'ordinal': 1, 'phase': 'user-operating', 'recordingOnly': True}
            ui.validate_registration(reg, status, root / 'package', root, 42, 'configuration', 'initial', config)
            for key, replacement in [('pid', 43), ('entry', 'business'), ('ordinal', 2), ('phase', 'recording-complete'), ('recordingOnly', False)]:
                changed = dict(status); changed[key] = replacement
                with self.assertRaises(ui.Unknown):
                    ui.validate_registration(reg, changed, root / 'package', root, 42, 'configuration', 'initial', config)
            with self.assertRaises(ui.Unknown):
                ui.validate_registration(reg, status, root / 'package', root, 42, 'configuration', 'initial', root / 'other.yaml')

    def test_exclusive_evidence_preserves_original_bytes(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'raw.json'
            ui.write_new(path, {'raw': '原件'})
            before = path.read_bytes()
            with self.assertRaises(FileExistsError): ui.write_new(path, {'raw': 'replacement'})
            self.assertEqual(path.read_bytes(), before)

    def test_transient_provider_failure_retries_only_reads_and_keeps_unknown_evidence(self):
        driver = ui.Driver.__new__(ui.Driver)
        driver.deadline = time.monotonic() + 5
        calls, records = [], []
        replies = iter([ui.Unknown('provider replaced tree'), {'elements': []}])
        def observe(action):
            calls.append(action)
            value = next(replies)
            if isinstance(value, Exception): raise value
            return value
        driver.action = observe; driver.record = lambda label, value: records.append((label, value))
        with mock.patch.object(ui.time, 'sleep'):
            self.assertEqual(driver.snapshot(), {'elements': []})
        self.assertEqual(calls, ['Read', 'Read'])
        self.assertEqual(records[0][0], 'read-unknown')
        self.assertIn('provider replaced tree', records[0][1]['error'])
        driver.action = mock.Mock(side_effect=ui.Unknown('unavailable'))
        with mock.patch.object(ui.time, 'sleep'), self.assertRaises(ui.Unknown): driver.snapshot()
        self.assertEqual(driver.action.call_args_list, [mock.call('Read')] * 3)


class PreviewUiActivation(unittest.TestCase):
    def setUp(self):
        self.native = ui.NativeInput.__new__(ui.NativeInput)
        self.native.pid = 42
        self.native.record = mock.Mock()
        self.snapshot = {'pid': 42, 'errors': [], 'truncated': False,
                         'roots': [{'id': 'ElaWindow', 'pid': 42, 'hwnd': 200}]}
        self.native.observe = mock.Mock(return_value=self.snapshot)
        self.native.user = mock.Mock()
        self.native.user.SetForegroundWindow.return_value = False
        self.native.user.SetFocus.return_value = 0
        self.native.user.AttachThreadInput.return_value = True
        self.native.user.GetForegroundWindow.return_value = 100
        self.native.user.GetWindowThreadProcessId.side_effect = lambda hwnd, _owner: {100: 9, 200: 11}[hwnd]
        self.native.kernel = mock.Mock()
        self.native.kernel.GetCurrentThreadId.return_value = 7
        self.native.owner = mock.Mock(return_value=42)
        self.native.focus_facts = mock.Mock(return_value={'foregroundPid': 99, 'focusPid': 99})
        self.good = {'foregroundPid': 42, 'focusPid': 42}
        self.native.wait_guard = mock.Mock(side_effect=[ui.Unknown('other PID'), self.good, self.good])
        self.errors = mock.patch.object(ui.ctypes, 'get_last_error', return_value=0, create=True)
        self.clear_error = mock.patch.object(ui.ctypes, 'set_last_error', create=True)
        self.errors.start(); self.clear_error.start()
        self.addCleanup(self.errors.stop); self.addCleanup(self.clear_error.stop)

    def test_normal_guard_success_does_not_attach_even_when_api_returns_zero(self):
        self.native.wait_guard.side_effect = [self.good]
        self.native.activate(self.snapshot)
        self.native.user.AttachThreadInput.assert_not_called()
        self.native.observe.assert_not_called()
        self.native.user.SetFocus.assert_not_called()
        self.assertFalse(self.native.record.call_args.args[1]['setForegroundReturn'])

    def test_fallback_fresh_identity_queue_and_final_guard_surround_temporary_attachment(self):
        self.native.user.PeekMessageW.return_value = False  # No pending messages is normal.
        self.native.activate(self.snapshot)
        self.native.observe.assert_called_once_with()
        self.native.user.PeekMessageW.assert_called_once()
        self.assertEqual(self.native.user.AttachThreadInput.call_args_list,
                         [mock.call(7, 9, True), mock.call(7, 11, True),
                          mock.call(7, 11, False), mock.call(7, 9, False)])
        self.native.user.SetFocus.assert_called_once_with(200)
        self.assertEqual(self.native.wait_guard.call_count, 3)
        facts = self.native.record.call_args.args[1]
        self.assertEqual(facts['after'], self.good)
        self.assertEqual(facts['threads'], {'current': 7, 'foreground': 9, 'target': 11})

    def test_incomplete_duplicate_or_wrong_pid_window_metadata_never_activates(self):
        for change in ({'errors': ['timeout']}, {'truncated': True}, {'pid': 99},
                       {'roots': self.snapshot['roots'] * 2},
                       {'roots': [{'id': 'ElaWindow', 'pid': 99, 'hwnd': 200}]},
                       {'roots': [{'id': 'ElaWindow', 'pid': 42, 'hwnd': 0}]}):
            with self.subTest(change=change), self.assertRaises(ui.Unknown):
                self.native.activate({**self.snapshot, **change})
        self.native.owner.return_value = 99
        with self.assertRaises(ui.Unknown): self.native.activate(self.snapshot)
        self.native.user.SetForegroundWindow.assert_not_called()
        self.native.user.AttachThreadInput.assert_not_called()

    def test_fresh_window_changed_before_fallback_never_attaches(self):
        self.native.observe.return_value = {**self.snapshot, 'roots': [{'id': 'ElaWindow', 'pid': 42, 'hwnd': 300}]}
        with self.assertRaises(ui.Unknown): self.native.activate(self.snapshot)
        self.native.user.AttachThreadInput.assert_not_called()
        self.native.user.SetFocus.assert_not_called()

    def test_same_thread_is_attached_only_once_and_self_is_skipped(self):
        for foreground, target, expected in ((9, 9, 9), (7, 11, 11)):
            self.native.user.GetWindowThreadProcessId.side_effect = lambda hwnd, _owner: foreground if hwnd == 100 else target
            self.native.wait_guard.side_effect = [ui.Unknown('other PID'), self.good, self.good]
            self.native.user.AttachThreadInput.reset_mock()
            self.native.activate(self.snapshot)
            self.assertEqual(self.native.user.AttachThreadInput.call_args_list,
                             [mock.call(7, expected, True), mock.call(7, expected, False)])

    def test_partial_attach_failure_releases_the_first_successful_pair(self):
        self.native.user.AttachThreadInput.side_effect = [True, False, True]
        with self.assertRaises(ui.Unknown): self.native.activate(self.snapshot)
        self.assertEqual(self.native.user.AttachThreadInput.call_args_list,
                         [mock.call(7, 9, True), mock.call(7, 11, True), mock.call(7, 9, False)])
        self.native.user.SetFocus.assert_not_called()

    def test_activation_and_attached_guard_exceptions_always_detach(self):
        for where in ('foreground', 'focus', 'guard'):
            self.setUp()
            if where == 'foreground': self.native.user.SetForegroundWindow.side_effect = [False, RuntimeError('activation failed')]
            if where == 'focus': self.native.user.SetFocus.side_effect = RuntimeError('focus failed')
            if where == 'guard': self.native.wait_guard.side_effect = [ui.Unknown('other PID'), ui.Unknown('still other PID')]
            with self.subTest(where=where), self.assertRaises((ui.Unknown, RuntimeError)):
                self.native.activate(self.snapshot)
            self.assertEqual(self.native.user.AttachThreadInput.call_args_list[-2:],
                             [mock.call(7, 11, False), mock.call(7, 9, False)])

    def test_detach_failure_still_releases_other_pair_and_reports_unknown(self):
        self.native.user.AttachThreadInput.side_effect = [True, True, False, True]
        with self.assertRaisesRegex(ui.Unknown, 'detach'): self.native.activate(self.snapshot)
        self.assertEqual(self.native.user.AttachThreadInput.call_args_list[-1], mock.call(7, 9, False))
        self.assertIn('detach', self.native.record.call_args.args[1]['error'])

    def test_success_while_attached_does_not_authorize_input_after_focus_is_lost(self):
        self.native.wait_guard.side_effect = [ui.Unknown('other PID'), self.good, ui.Unknown('other PID after detach')]
        with self.assertRaisesRegex(ui.Unknown, 'after detach'): self.native.activate(self.snapshot)
        self.assertTrue(all(entry['ok'] for entry in self.native.record.call_args.args[1]['detached']))

    def test_evidence_write_failure_occurs_after_all_detaches(self):
        self.native.record.side_effect = OSError('disk full')
        with self.assertRaises(OSError): self.native.activate(self.snapshot)
        self.assertEqual(self.native.user.AttachThreadInput.call_args_list[-2:],
                         [mock.call(7, 11, False), mock.call(7, 9, False)])


if __name__ == '__main__':
    unittest.main()
