"""Pure file fixtures; dummy PE bytes/exit receipts never qualify a real runtime.

Only Windows observation and the stock PowerShell verifier are replaced. These
tests exercise actual directory copies, leases, byte CAS and interrupted writes;
no product, UI, ACL, VM or subprocess is started.
"""
import copy
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock
import uuid

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools'))
import update_preview as updater


def put(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(updater.encoded(value) if isinstance(value, (dict, list)) else value)
    return updater.digest(path)


def fixture_package(root, generation):
    """An exact static package, deliberately containing non-executable bytes."""
    root.mkdir()
    put(root / 'cordis_config_workbench.exe', b'file-fixture-config')
    put(root / 'cordis_platform_workbench_demo.exe', b'file-fixture-business')
    declarations = {}
    for alias in updater.ALIASES:
        for variant in ('a', 'b'):
            group = 'qt-upgrade' if alias == 'notebook' else 'native-upgrade'
            directory = root / group / (alias + '-' + variant)
            library = 'cordis_example_' + alias + '_' + variant + '.dll'
            sha = put(directory / library, (generation + ':' + alias + ':' + variant).encode())
            value = {'manifestVersion': 1, 'moduleId': 'org.cordis.example.' + alias,
                     'moduleVersion': '1.1.0' if variant == 'a' else '1.2.0',
                     'loader': 'qt-ui' if alias == 'notebook' else 'c-abi',
                     'library': library, 'sha256': sha, 'platform': 'windows-x64',
                     'abi': {'major': 1, 'minor': 0}, 'plugins': ['example.' + alias],
                     'unloadPolicy': 'resident' if alias == 'notebook' else 'explicit'}
            put(directory / 'module.json', value)
            declarations[alias, variant] = (directory / 'module.json', value)
    inventory = updater.snapshot(root)
    manifest = {'schema': 'cordis.preview.package/1', 'scope': 'internal-technical-preview',
                'platform': 'windows-x64', 'deliveryVersion': '0.1.5-preview.' + ('2' if generation == 'old' else '3'),
                'profile': {'id': 'ela-msvc-release', 'buildType': 'Release', 'runtime': 'MD',
                            'pointerBits': 64, 'asan': False, 'sdkVersion': '0.1.5'},
                'qualification': {'sourceSha256': ('a' if generation == 'old' else 'b') * 64,
                                  'nativeApprovalCount': 4, 'qtPolicy': 'resident'},
                'versions': {'configurationFormat': 1, 'moduleManifest': 1, 'businessAbi': '1.0',
                             'moduleSchemas': {'example.counter': 1, 'example.reporter': None, 'example.notebook': 1}},
                'files': [{'path': name, **record} for name, record in sorted(inventory['files'].items())]}
    sha = put(root / 'build-manifest.json', manifest)
    files = updater.snapshot(root)['files']
    put(root / 'SHA256SUMS.txt', ''.join(record['sha256'] + '  ' + name + '\n' for name, record in sorted(files.items())).encode())
    return updater.package(root, sha), declarations


class DeploymentFiles(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='cordis-update-files-')
        self.addCleanup(self.temporary.cleanup)
        self.base = Path(self.temporary.name).resolve()
        self.old, self.old_modules = fixture_package(self.base / 'old', 'old')
        self.new, self.new_modules = fixture_package(self.base / 'new', 'new')
        self.source = self.base / 'accepted-business'
        self.source.mkdir()
        for declaration, value in self.old_modules.values():
            directory = self.source / 'store' / value['sha256']
            put(directory / 'module.json', declaration.read_bytes())
            put(directory / value['library'], (declaration.parent / value['library']).read_bytes())
        self.selected = {'counter': 'a', 'reporter': 'a', 'notebook': 'b'}
        self.document = {'formatVersion': 1,
                         'modules': {alias: {'manifest': self.old_modules[alias, variant][1]['sha256'] + '/module.json'}
                                     for alias, variant in self.selected.items()},
                         'plugins': [{'id': 'notes', 'module': 'notebook', 'plugin': 'example.notebook',
                                      'config': {'text': 'persistent draft', 'title': 'accepted title'}}],
                         'unknownExtension': {'retained': [1, 'state', None]}}
        put(self.source / 'session.yaml', self.document)
        put(self.source / 'store/unrecognized/PIN', b'unknown owner; never remove')
        put(self.source / 'store/unrecognized/extra.bin', b'foreign store bytes')
        (self.source / 'empty-directory').mkdir()
        inventory = updater.snapshot(self.source)
        self.index = self.base / 'accepted-index.json'
        self.index_sha = put(self.index, {'schema': 'cordis.p6.accepted-business-baseline.index/1',
                                        'files': [{'path': name, **row} for name, row in inventory['files'].items()]})
        self.config = self.base / 'accepted-configuration.yaml'
        self.config_value = {'formatVersion': 1, 'plugins': [{'id': 'notes', 'plugin': 'demo.notes', 'config': {'title': 'same title'}}]}
        self.config_sha = put(self.config, self.config_value)
        self.mapping = self.base / 'mapping.json'; self.review = self.base / 'mapping-review.json'
        anchors = {'oldManifestSha256': self.old['sha256'], 'newManifestSha256': self.new['sha256'],
                   'newQualificationSha256': self.new['manifest']['qualification']['sourceSha256'], 'profileId': 'ela-msvc-release'}
        entries = []
        for alias, variant in self.selected.items():
            values = {}
            for name, pack, table in (('old', self.old, self.old_modules), ('new', self.new, self.new_modules)):
                path, declaration = table[alias, variant]
                values[name] = {'moduleId': declaration['moduleId'], 'moduleVersion': declaration['moduleVersion'],
                                'loader': declaration['loader'], 'unloadPolicy': declaration['unloadPolicy'],
                                'abi': declaration['abi'], 'librarySha256': declaration['sha256'], 'manifestSha256': updater.digest(path)}
                values[name]['locator' if name == 'old' else 'packageManifest'] = (
                    declaration['sha256'] + '/module.json' if name == 'old' else path.relative_to(pack['path']).as_posix())
            schema = self.old['manifest']['versions']['moduleSchemas']['example.' + alias]
            entries.append({'alias': alias, **values, 'oldStateSchema': schema, 'newStateSchema': schema})
        self.map_sha = put(self.mapping, {'schema': 'cordis.preview.migration-map/1', **anchors, 'entries': entries})
        approvals = [{'moduleId': value['moduleId'], 'sha256': value['sha256']}
                     for (alias, _), (_, value) in self.new_modules.items() if alias != 'notebook']
        self.review_sha = put(self.review, {'schema': 'cordis.preview.migration-review/1', 'passed': True,
                                          'mappingSha256': self.map_sha, **anchors,
                                          'adoptionPath': updater.ADOPTION, 'nativeApprovals': approvals})
        self.quiet = mock.patch.object(updater, 'quiet', autospec=True).start()
        self.verify = mock.patch.object(updater, 'stock_verify', autospec=True).start()
        self.addCleanup(mock.patch.stopall)
        self.owner = updater.Updater(self.base / 'controller')
        self.original_sources = updater.snapshot(self.source)
        self.selection = self.owner.initialize(self.old['path'], self.old['sha256'], self.source, self.index, self.index_sha,
                                               self.config, self.config_sha)
        self.initial_sha = updater.digest(self.owner.path('active.json'))

    def proof(self, selection, mapping=None):
        """Synthetic reviewed-input fixtures; never run/accept actual binaries."""
        data = self.owner.path(selection['data']); rows = []
        after = updater.snapshot(data)
        for number, entry in enumerate(('configuration', 'business'), 101):
            identifier = uuid.uuid4().hex
            file = updater.CONFIG if entry == 'configuration' else updater.BUSINESS
            launch = {'schema': 'cordis.preview.update-launch/1', 'launchId': identifier, 'entry': entry,
                      'fixtureOnly': True, 'tupleSha256': updater.value_sha(selection), 'exitCode': 0,
                      'normalExitObserved': True, 'error': None, 'forcedCloseOrKillPerformed': False,
                      'pid': number, 'creationFileTime': str(133000000000000000 + number), 'processHandleObserved': True,
                      'toolSha256': updater.digest(updater.__file__), 'dataAfter': after}
            path = self.owner.path('launches/' + identifier + '/exit.json'); sha = put(path, launch)
            row = {'entry': entry, 'launchId': identifier, 'launchReceiptSha256': sha, 'pid': number,
                   'creationFileTime': launch['creationFileTime'], 'acceptedRevision': 1,
                   'explicitSaveObserved': True, 'saveOutcome': 'Saved' if entry == 'configuration' else 'Unchanged',
                   'savedFileSha256': after['files'][file]['sha256']}
            if entry == 'business':
                row['modules'] = []
                for alias, value in updater.read(data / updater.BUSINESS)['modules'].items():
                    path = data / 'business/store' / value['manifest']; declaration = updater.read(path)
                    row['modules'].append({'alias': alias, 'sha256': declaration['sha256'], 'path': str(path.parent / declaration['library'])})
            rows.append(row)
        receipt = self.base / ('fixture-validation-' + uuid.uuid4().hex + '.json')
        sha = put(receipt, {'schema': 'cordis.preview.update-validation/1', 'fixtureOnly': True,
                           'externallyReviewed': True, 'transactionId': selection['transactionId'],
                           'tupleSha256': updater.value_sha(selection), 'mappingSha256': mapping,
                           'runtimeSchemaValidated': True, 'launches': rows})
        return receipt, sha

    def prepare(self):
        proof, sha = self.proof(self.selection)
        return self.owner.prepare(self.new['path'], self.new['sha256'], self.mapping, self.map_sha,
                                  self.review, self.review_sha, self.initial_sha, proof, sha)

    def ready(self):
        result = self.prepare()
        proof, sha = self.proof(result['candidate'], self.map_sha)
        return result, proof, sha

    def test_offline_mapping_preserves_every_unselected_byte_and_original(self):
        result = self.prepare(); selection = result['candidate']; transaction = result['transactionId']
        old_data = self.owner.path(self.selection['data']); candidate = self.owner.path(selection['data'])
        before = updater.snapshot(old_data); after = updater.snapshot(candidate)
        self.assertEqual(updater.snapshot(self.source), self.original_sources)
        self.assertEqual(updater.digest(self.owner.path('active.json')), self.initial_sha)
        self.assertEqual(updater.snapshot(self.owner.path('backups/' + transaction)), before)
        for name, record in before['files'].items():
            if name != updater.BUSINESS: self.assertEqual(after['files'][name], record)
        changed = updater.read(candidate / updater.BUSINESS)
        expected = copy.deepcopy(self.document)
        for alias, variant in self.selected.items():
            expected['modules'][alias]['manifest'] = self.new_modules[alias, variant][1]['sha256'] + '/module.json'
        self.assertEqual(changed, expected)
        self.assertEqual(len(after['files']), len(before['files']) + 6)
        self.assertTrue((candidate / 'business/empty-directory').is_dir())

    def test_initial_output_nested_in_original_package_or_accepted_data_refuses_before_any_write(self):
        for original in (self.old['path'], self.source):
            with self.subTest(original=original):
                before = updater.snapshot(original)
                owner = updater.Updater(original / 'must-not-create')
                with self.assertRaisesRegex(updater.Refused, 'overlap'):
                    owner.initialize(self.old['path'], self.old['sha256'], self.source, self.index, self.index_sha,
                                     self.config, self.config_sha)
                self.assertEqual(updater.snapshot(original), before)
                self.assertFalse(owner.root.exists())

    def test_activation_requires_two_bound_reviewed_host_receipts_and_preserves_backup(self):
        result, proof, sha = self.ready()
        self.owner.activate(result['transactionId'], proof, sha)
        self.assertEqual(self.owner.selector(), result['candidate'])
        self.assertEqual(updater.snapshot(self.owner.path(self.selection['data']))['files'][updater.BUSINESS]['sha256'],
                         self.original_sources['files']['session.yaml']['sha256'])
        self.assertEqual(updater.snapshot(self.source), self.original_sources)

    def test_rollback_retains_new_and_unknown_store_and_repeated_recovery_is_read_only(self):
        result, proof, sha = self.ready(); transaction = result['transactionId']
        self.owner.activate(transaction, proof, sha)
        new_data = updater.snapshot(self.owner.path(result['candidate']['data']))
        self.assertEqual(self.owner.recover(transaction, rollback=True)['state'], 'rolled-back')
        tree = updater.snapshot(self.owner.root)
        self.assertEqual(self.owner.recover(transaction, rollback=True)['state'], 'rolled-back')
        self.assertEqual(updater.snapshot(self.owner.root), tree)
        self.assertEqual(self.owner.selector()['release'], self.selection['release'])
        self.assertEqual(self.owner.selector()['data'], self.selection['data'])
        self.assertIsNone(self.owner.selection_mapping(self.owner.selector()))
        self.assertEqual(updater.snapshot(self.owner.path(result['candidate']['data'])), new_data)

    def test_interruptions_before_commit_preserve_old_selector_and_all_partial_outputs(self):
        for boundary in ('after-plan', 'after-backup', 'after-prepared'):
            with self.subTest(boundary=boundary):
                def stop(name):
                    if name == boundary: raise RuntimeError('injected updater interruption')
                existing = set(self.owner.path('transactions').iterdir())
                with mock.patch.object(updater, '_checkpoint', side_effect=stop):
                    with self.assertRaises(RuntimeError): self.prepare()
                transaction = (set(self.owner.path('transactions').iterdir()) - existing).pop().name
                self.assertEqual(self.owner.recover(transaction)['state'], 'old-selection-not-committed')
                self.assertEqual(updater.digest(self.owner.path('active.json')), self.initial_sha)
                self.assertTrue(self.owner.path('transactions/' + transaction + '/00-plan.json').is_file())
                if boundary != 'after-plan': self.assertTrue(self.owner.path('backups/' + transaction).is_dir())
                self.assertEqual(updater.snapshot(self.source), self.original_sources)

    def test_interruption_after_validation_resumes_without_mapping_or_any_runtime_launch(self):
        result, proof, sha = self.ready(); transaction = result['transactionId']
        def stop(name):
            if name == 'after-validated': raise RuntimeError('injected updater interruption')
        with mock.patch.object(updater, '_checkpoint', side_effect=stop):
            with self.assertRaises(RuntimeError): self.owner.activate(transaction, proof, sha)
        self.assertEqual(self.owner.recover(transaction)['state'], 'old-selection-not-committed')
        with mock.patch.object(updater, 'migration', side_effect=AssertionError('mapping replay')), \
             mock.patch.object(updater.subprocess, 'Popen', side_effect=AssertionError('process replay')):
            self.assertEqual(self.owner.recover(transaction, resume=True)['state'], 'new-selection-committed')
        self.assertEqual(self.owner.selector(), result['candidate'])

    def test_selector_replace_interruption_before_or_after_has_unambiguous_recovery(self):
        result, proof, sha = self.ready(); transaction = result['transactionId']
        def stop(name):
            if name == 'before-selector-replace': raise RuntimeError('injected updater interruption')
        with mock.patch.object(updater, '_checkpoint', side_effect=stop):
            with self.assertRaises(RuntimeError): self.owner.activate(transaction, proof, sha)
        self.assertEqual(self.owner.recover(transaction)['state'], 'old-selection-not-committed')
        def after(name):
            if name == 'after-selector-replace': raise RuntimeError('injected updater interruption')
        with mock.patch.object(updater, '_checkpoint', side_effect=after):
            with self.assertRaises(RuntimeError): self.owner.recover(transaction, resume=True)
        self.assertEqual(self.owner.recover(transaction)['state'], 'new-selection-committed')
        self.assertEqual(self.owner.selector(), result['candidate'])

    def test_os_lease_prevents_second_controller_before_any_deployment(self):
        with self.owner.lease():
            with self.assertRaisesRegex(updater.Refused, 'lease'): self.prepare()
        # The test proof is outside deployment state; no app/data/journal change.
        self.assertEqual(set(self.owner.path('transactions').iterdir()), {self.owner.path('transactions/' + self.selection['transactionId'])})
        self.assertEqual(updater.digest(self.owner.path('active.json')), self.initial_sha)

    def test_stale_selector_or_data_cannot_activate_and_is_not_overwritten(self):
        result, proof, sha = self.ready()
        old_data = self.owner.path(self.selection['data'])
        put(old_data / 'business/new-foreign.bin', b'foreign edit')
        state = updater.snapshot(self.owner.root)
        with self.assertRaisesRegex(updater.Refused, 'CAS changed'): self.owner.activate(result['transactionId'], proof, sha)
        self.assertEqual(updater.snapshot(self.owner.root), state)

    def test_foreign_selector_is_never_guessed_or_replaced_by_recovery(self):
        result = self.prepare(); path = self.owner.path('active.json')
        foreign = copy.deepcopy(self.selection); foreign['selectionId'] = uuid.uuid4().hex
        put(path, foreign); state = updater.snapshot(self.owner.root)
        with self.assertRaisesRegex(updater.Refused, 'Foreign'): self.owner.recover(result['transactionId'], rollback=True)
        self.assertEqual(updater.snapshot(self.owner.root), state)

    def test_modified_old_data_prevents_rollback_without_removing_new_tree(self):
        result, proof, sha = self.ready(); transaction = result['transactionId']
        self.owner.activate(transaction, proof, sha)
        put(self.owner.path(self.selection['data']) / 'business/store/unrecognized/PIN', b'new foreign pin')
        state = updater.snapshot(self.owner.root)
        with self.assertRaisesRegex(updater.Refused, 'rollback data changed'): self.owner.recover(transaction, rollback=True)
        self.assertEqual(updater.snapshot(self.owner.root), state)

    def test_corrupted_rollback_intent_cannot_select_another_existing_slot(self):
        result, proof, sha = self.ready(); transaction = result['transactionId']
        self.owner.activate(transaction, proof, sha)
        def stop(name):
            if name == 'before-selector-replace': raise RuntimeError('injected rollback interruption')
        with mock.patch.object(updater, '_checkpoint', side_effect=stop):
            with self.assertRaises(RuntimeError): self.owner.recover(transaction, rollback=True)
        path = self.owner.path('transactions/' + transaction + '/rollback-intent.json')
        intent = updater.read(path)
        intent['selection']['data'] = result['candidate']['data']
        put(path, intent); state = updater.snapshot(self.owner.root)
        with self.assertRaisesRegex(updater.Refused, 'exact old app/data'): self.owner.recover(transaction, rollback=True)
        self.assertEqual(updater.snapshot(self.owner.root), state)

    def test_mapping_needs_exact_review_not_equal_version_or_four_profile_union(self):
        review = updater.read(self.review)
        for mutation in ('not-reviewed', 'profile-union', 'wrong-adoption', 'wrong-qualification'):
            with self.subTest(mutation=mutation):
                bad = copy.deepcopy(review)
                if mutation == 'not-reviewed': bad['passed'] = False
                elif mutation == 'profile-union': bad['nativeApprovals'] *= 3
                elif mutation == 'wrong-adoption': bad['adoptionPath'] = 'qualification/native-business.json'
                else: bad['newQualificationSha256'] = 'c' * 64
                sha = put(self.review, bad)
                with self.assertRaises(updater.Refused):
                    updater.migration(self.old, self.new, self.owner.path(self.selection['data']), self.mapping, self.map_sha, self.review, sha)

    def test_schema_change_and_old_locator_tampering_are_refused(self):
        original = updater.read(self.mapping)
        for mutation in ('schema', 'locator', 'dll', 'identity'):
            with self.subTest(mutation=mutation):
                mapping = copy.deepcopy(original); row = mapping['entries'][0]
                if mutation == 'schema': row['newStateSchema'] = 2
                elif mutation == 'locator': row['old']['locator'] = 'foreign/module.json'
                elif mutation == 'dll': row['old']['librarySha256'] = 'f' * 64
                else: row['new']['moduleVersion'] = '9.0.0'
                sha = put(self.mapping, mapping)
                review = updater.read(self.review); review['mappingSha256'] = sha
                review_sha = put(self.review, review)
                with self.assertRaises(updater.Refused):
                    updater.migration(self.old, self.new, self.owner.path(self.selection['data']), self.mapping, sha, self.review, review_sha)

    def test_bad_runtime_proof_never_publishes_activation_intent(self):
        result, proof, _ = self.ready(); original = updater.read(proof)
        for mutation in ('not-reviewed', 'missing-host', 'no-save', 'wrong-pid', 'wrong-creation', 'config-enum', 'wrong-path', 'missing-exit'):
            with self.subTest(mutation=mutation):
                bad = copy.deepcopy(original)
                if mutation == 'not-reviewed': bad['externallyReviewed'] = False
                elif mutation == 'missing-host': bad['launches'].pop()
                elif mutation == 'no-save': bad['launches'][0]['explicitSaveObserved'] = False
                elif mutation == 'wrong-pid': bad['launches'][0]['pid'] += 1
                elif mutation == 'wrong-creation': bad['launches'][0]['creationFileTime'] = '1'
                elif mutation == 'config-enum': bad['launches'][0]['saveOutcome'] = 'Written'
                elif mutation == 'wrong-path': bad['launches'][1]['modules'][0]['path'] = str(self.base / 'foreign.dll')
                else: bad['launches'][0]['launchId'] = 'f' * 32
                sha = put(proof, bad)
                with self.assertRaises((updater.Refused, OSError)):
                    self.owner.activate(result['transactionId'], proof, sha)
                self.assertEqual(updater.digest(self.owner.path('active.json')), self.initial_sha)
                self.assertFalse(self.owner.path('transactions/' + result['transactionId'] + '/30-activation-intent.json').exists())

    def test_exit_failure_and_forged_old_tool_hash_are_refused_even_with_reviewed_envelope(self):
        result, proof, _ = self.ready(); envelope = updater.read(proof)
        row = envelope['launches'][0]; path = self.owner.path('launches/' + row['launchId'] + '/exit.json'); original = updater.read(path)
        for key, value in (('exitCode', 1), ('exitCode', False), ('processHandleObserved', False), ('toolSha256', '0' * 64), ('error', 'Incomplete/truncated logs')):
            with self.subTest(key=key):
                bad = {**original, key: value}; row['launchReceiptSha256'] = put(path, bad)
                sha = put(proof, envelope)
                with self.assertRaises(updater.Refused): self.owner.activate(result['transactionId'], proof, sha)

    def test_candidate_nonconfiguration_change_cannot_be_hidden_in_external_review(self):
        result = self.prepare(); data = self.owner.path(result['candidate']['data'])
        put(data / 'business/store/unrecognized/extra.bin', b'accidental candidate damage')
        proof, sha = self.proof(result['candidate'], self.map_sha)
        with self.assertRaisesRegex(updater.Refused, 'nonconfiguration data changed'):
            self.owner.activate(result['transactionId'], proof, sha)
        self.assertEqual(updater.digest(self.owner.path('active.json')), self.initial_sha)

    def test_corrupted_prepared_chain_is_refused(self):
        result, proof, sha = self.ready(); path = self.owner.path('transactions/' + result['transactionId'] + '/10-prepared.json')
        value = updater.read(path); value['selection']['data'] = self.selection['data']; put(path, value)
        with self.assertRaisesRegex(updater.Refused, 'hash chain'): self.owner.activate(result['transactionId'], proof, sha)

    def test_pending_lock_and_payload_tamper_fail_before_new_transaction(self):
        data = self.owner.path(self.selection['data']); put(data / 'configuration/cordis.yaml.cordis.lock', b'unknown lock')
        with self.assertRaisesRegex(updater.Refused, 'lock retained'): self.prepare()
        put(self.new['path'] / 'cordis_config_workbench.exe', b'changed after qualification')
        with self.assertRaisesRegex(updater.Refused, 'inventory/bytes'): updater.package(self.new['path'], self.new['sha256'])


class UnsafeInputs(unittest.TestCase):
    def test_noncanonical_or_device_paths_are_refused(self):
        for name in ('../foreign', 'a/../b', '/absolute', 'a\\b', 'a:b', 'x//y', 'x/.', 'CON.txt', 'a/trailing.', 'a/trailing ',
                     'x/\x01', 'x/\x7f', 'COM\u00b9.txt', 'COM\u00b2', 'com\u00b3.log', 'LPT\u00b9', 'lpt\u00b2.txt', 'LPT\u00b3'):
            with self.subTest(name=name), self.assertRaises(updater.Refused): updater.relative(name)

    def test_duplicate_json_nonfinite_and_excess_depth_are_refused(self):
        for raw in (b'{"x":1,"x":2}', b'{"x":NaN}', b'[' * 34 + b'0' + b']' * 34):
            with self.subTest(raw=raw), self.assertRaises(updater.Refused): updater.parse(raw)

    def test_hardlinked_input_is_refused_without_touching_either_name(self):
        with tempfile.TemporaryDirectory(prefix='cordis-update-link-') as name:
            root = Path(name).resolve(); original = root / 'one'; alias = root / 'two'
            original.write_bytes(b'original'); os.link(original, alias)
            with self.assertRaisesRegex(updater.Refused, 'Hard-linked'): updater.snapshot(root)
            self.assertEqual(original.read_bytes(), alias.read_bytes())

    def test_copy_new_only_keeps_partial_destination_instead_of_replacing_it(self):
        with tempfile.TemporaryDirectory(prefix='cordis-update-copy-') as name:
            root = Path(name).resolve(); source = root / 'source'; source.mkdir(); put(source / 'a', b'a')
            target = root / 'partial'; target.mkdir(); put(target / 'foreign', b'keep')
            with self.assertRaises(updater.Refused): updater.copy_tree(source, target, updater.snapshot(source))
            self.assertEqual((target / 'foreign').read_bytes(), b'keep')

    def test_copy_nested_under_source_cannot_pollute_the_original_tree(self):
        with tempfile.TemporaryDirectory(prefix='cordis-update-overlap-') as name:
            source = Path(name).resolve(); put(source / 'original', b'accepted source')
            before = updater.snapshot(source)
            with self.assertRaisesRegex(updater.Refused, 'overlap'):
                updater.copy_tree(source, source / 'unwanted-child', before)
            self.assertEqual(updater.snapshot(source), before)


if __name__ == '__main__':
    unittest.main()
