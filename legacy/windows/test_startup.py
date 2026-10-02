import unittest
from unittest.mock import patch
from run import startup_plan


class StartupTests(unittest.TestCase):
    def plan(self, busy, compatible=True, authenticated=True):
        with patch('run.port_busy', side_effect=busy), patch('run.compatible_dashboard', return_value=compatible), patch('run.broker_available', return_value=authenticated):
            return startup_plan({'http_port': 8088, 'mqtt_port': 1884})

    def test_fresh_start(self):
        self.assertEqual(self.plan([False, False]), (True, True))

    def test_existing_project(self):
        with self.assertRaisesRegex(RuntimeError, '8088'):
            self.plan([True, True])

    def test_existing_broker(self):
        with self.assertRaisesRegex(RuntimeError, '1884'):
            self.plan([False, True])

    def test_existing_dashboard(self):
        with self.assertRaisesRegex(RuntimeError, '8088'):
            self.plan([True, False])

    def test_unrelated_web_server(self):
        with self.assertRaisesRegex(RuntimeError, '8088'):
            self.plan([True, True], compatible=False)

    def test_wrong_broker_credentials(self):
        with self.assertRaisesRegex(RuntimeError, '1884'):
            self.plan([False, True], authenticated=False)

    def test_identical_ports(self):
        with self.assertRaisesRegex(RuntimeError, 'must be different'):
            startup_plan({'http_port': 1884, 'mqtt_port': 1884})


if __name__ == '__main__':
    unittest.main(verbosity=2)
