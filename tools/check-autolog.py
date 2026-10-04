"""Run with python tools/check-autolog.py. No external messages are sent."""
import tempfile
from pathlib import Path
import unittest
import json
from unittest.mock import patch
import autolog as a

NAME = 'Game-Sonic-12345678-20261004-180000-UTC-0.log'
CONTENT = b'PS5X360 0.5.2-preview\nGame: Sonic\nTitle ID: 1234\nSource: /private/roms/Sonic\nCrash: sample\n'


class FakeFTP:
    def __enter__(self): return self
    def __exit__(self, *args): pass
    def connect(self, *args, **kw): pass
    def login(self): pass
    def cwd(self, path): assert path == a.LOG_FOLDER
    def nlst(self): return [NAME, 'boot.log', 'save.dat', 'Game-Launcher-12345678-20261004-180000-UTC-0.log']
    def retrlines(self, command, callback):
        assert command == 'LIST'
        for name in self.nlst():
            callback(f'-rw-r--r-- 1 ftp ftp {len(CONTENT)} Oct 04 18:00 {name}')
    def voidcmd(self, command): pass
    def size(self, name): return len(CONTENT)
    def sendcmd(self, command): return '213 20261004180000'
    def retrbinary(self, command, callback, **kw):
        assert command == 'RETR ' + NAME
        callback(CONTENT)


class Checks(unittest.TestCase):
    def setUp(self):
        self.folder = tempfile.TemporaryDirectory()
        self.addCleanup(self.folder.cleanup)
        self.queue = a.Queue(Path(self.folder.name) / 'queue.db')
        self.addCleanup(self.queue.db.close)

    def count(self):
        return self.queue.db.execute('SELECT count(*) FROM reports').fetchone()[0]

    def test_redaction_preserves_diagnostics(self):
        text = 'GPU error eboot+0x842dc\nIP 192.168.0.19 ::1 2001:db8::1\nMAC aa:bb:cc:dd:ee:ff\nAuthorization: token-secret\nSource: /private/roms/Sonic\nhttps://discord.com/api/webhooks/private\nC:\\Users\\Bruno\\Desktop\n'
        output = a.redact(text)
        for private in ('192.168.0.19', '::1', 'aa:bb:cc', 'token-secret', '/private', 'discord.com', 'Bruno'):
            self.assertNotIn(private, output)
        self.assertIn('eboot+0x842dc', output)

    def test_queue_retry_survives_reopen_and_disabled_prevents_send(self):
        self.queue.add(NAME, 'signature', CONTENT)
        def offline(*args): raise OSError('offline')
        self.assertFalse(self.queue.upload_one({'enabled':True}, offline, now=10))
        self.assertEqual(self.count(), 1)
        self.assertFalse(self.queue.upload_one({'enabled':False}, lambda *args:self.fail(), now=100))
        self.queue.db.close()
        self.queue = a.Queue(Path(self.folder.name) / 'queue.db')
        self.addCleanup(self.queue.db.close)
        self.assertTrue(self.queue.seen(NAME, 'signature'))
        reply = lambda config, identifier, body: 'accepted ' + identifier + '\n'
        self.assertTrue(self.queue.upload_one({'enabled':True}, reply, now=100))
        self.assertEqual(self.count(),0)

    def test_wrong_ack_is_not_delivery(self):
        self.queue.add(NAME,'signature',CONTENT)
        self.assertFalse(self.queue.upload_one({'enabled':True},lambda *args:'accepted wrong',now=1))
        self.assertEqual(self.count(),1)

    def test_stability_excludes_launcher_and_does_not_resend_same_file(self):
        observations = {}
        with patch.object(a.ftplib,'FTP',FakeFTP):
            a.collect({'host':'192.168.0.19'},self.queue,observations,now=0)
            self.assertEqual(self.count(),0)
            a.collect({'host':'192.168.0.19'},self.queue,observations,now=121)
            self.assertEqual(self.count(),1)
            a.collect({'host':'192.168.0.19'},self.queue,observations,now=300)
            self.assertEqual(self.count(),1)
        body = self.queue.db.execute('SELECT body FROM reports').fetchone()[0].decode()
        self.assertIn('Game: Sonic',body)
        self.assertIn('session exit not confirmed',body)
        self.assertNotIn('/private',body)

    def test_limits_and_https(self):
        report=a.build_report(NAME,[(NAME,CONTENT+b'x'*3_000_000)])
        self.assertLessEqual(len(report),a.MAX_REPORT)
        self.assertTrue(report.startswith(a.MAGIC.encode()))
        with self.assertRaises(ValueError):
            a.validate({'host':'192.168.0.19','endpoint':'http://localhost/v1/reports','upload_token':'x'*32})
        for n in range(20): self.queue.add(str(n),'sig',str(n).encode())
        self.assertFalse(self.queue.add('full','sig',b'full'))
        self.assertFalse(self.queue.seen('full','sig'))

    def test_enable_requires_explicit_consent_in_temporary_config(self):
        home = Path(self.folder.name)
        path = home / 'config.json'
        a.atomic_json(path, {'host':'192.168.0.19','endpoint':'https://relay.example/v1/reports',
                             'upload_token':'test-only-'+'x'*32,'enabled':False})
        with patch('sys.argv',['autolog','--home',str(home),'--enable']), patch('builtins.input',return_value='NO'):
            a.main()
        self.assertFalse(json.loads(path.read_text())['enabled'])
        with patch('sys.argv',['autolog','--home',str(home),'--enable']), patch('builtins.input',return_value='YES'):
            a.main()
        self.assertTrue(json.loads(path.read_text())['enabled'])


if __name__=='__main__': unittest.main()
