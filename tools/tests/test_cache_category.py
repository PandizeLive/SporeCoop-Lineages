"""The sidebar patch must preserve vanilla categories and reject unknown shapes."""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from build_lineages_cache_category import CATEGORY, array, extend_root, key, props
from part_substitutes import parse


class CacheCategoryTest(unittest.TestCase):
    def test_preserves_categories_and_does_not_duplicate(self):
        original = props(array(0x744717C0, 0x20, [key(17), key(29)], 12))
        patched = extend_root(original)
        fields = parse(patched)
        self.assertEqual(fields[0x744717C0][2], key(17) + key(29) + key(CATEGORY))
        self.assertEqual(extend_root(patched), patched)

    def test_unknown_root_fields_are_not_silently_discarded(self):
        unknown = props(array(0x744717C0, 0x20, [], 12), array(1234, 0x20, [], 12))
        with self.assertRaises(ValueError):
            extend_root(unknown)

    def test_rejects_wrong_key_stride(self):
        malformed = props(array(0x744717C0, 0x20, [b'1234'], 4))
        with self.assertRaises(ValueError):
            extend_root(malformed)


if __name__ == '__main__':
    unittest.main()
