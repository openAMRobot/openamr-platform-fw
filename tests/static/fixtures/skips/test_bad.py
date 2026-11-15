import unittest
class T(unittest.TestCase):
    @unittest.skip("no issue named")
    def test_x(self):
        pass
