import unittest

from tlg_coop import debuglog


def call(handle, text):
    """Log lines for one message, three characters per light bar call, as coop_debug.lua sends."""
    while (len(text) + 1) % 3:
        text += " "
    text += "\n"
    return [f"\x1b[0;37m[Lib.Pad] <Debug> (Game:Main) pad.cpp:573 scePadSetLightBar: called "
            f"handle = {handle} rgb = {ord(text[i])} {ord(text[i+1])} {ord(text[i+2])}\x1b[m"
            for i in range(0, len(text), 3)]


class DebugLogTest(unittest.TestCase):
    def test_decodes_interleaved_handles(self):
        a, b = call(2, "COOP hello pad1"), call(1, "P1 x")
        lines = [a[0], b[0], "[Render] unrelated", a[1], b[1]] + a[2:] + b[2:]
        self.assertEqual(debuglog.decode(lines), [(1, "P1 x"), (2, "COOP hello pad1")])

    def test_trailing_padding_removed_but_inner_spaces_kept(self):
        self.assertEqual(debuglog.decode(call(2, "a  b")), [(2, "a  b")])

    def test_incomplete_message_is_not_reported(self):
        self.assertEqual(debuglog.decode(call(2, "abcdef")[:-1]), [])

    def test_scripts_hello_is_not_text(self):
        def call(r, g, b):
            return f"scePadSetLightBar: called handle = 2 rgb = {r} {g} {b}"
        lines = [call(104, 105, 32), call(192, 15, 238), call(33, 32, 10)]
        self.assertEqual(debuglog.decode(lines), [(2, "hi !")])

    def test_pad_opens(self):
        line = ("[Lib.Pad] <Info> (Game:Main) pad.cpp:346 scePadOpen: called user_id = 1001, "
                "type = 0, index = 0, player index = 2, out handle = 2")
        self.assertEqual(debuglog.pad_opens([line]), [
            {"user_id": 1001, "type": 0, "index": 0, "player_index": 2, "handle": 2}])


if __name__ == "__main__":
    unittest.main()
