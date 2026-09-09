import unittest

from vora.parser import ParseError, parse, plan


class ParserTests(unittest.TestCase):
    def test_parallel_graph_and_forward_reference(self):
        workflow = parse('''workflow Example(input):
agent a = "role"
last = a("{first} {second}")
first = a("{input}")
second = a("independent")
return last''')
        self.assertEqual(plan(workflow)["parallel_layers"], [["first", "second"], ["last"]])
        self.assertEqual(workflow.steps[0].dependencies, ("first", "second"))

    def test_reject_invalid_programs(self):
        bodies = [
            'x = a("{missing}")\nreturn x',
            'x = absent("hi")\nreturn x',
            'x = a("{y}")\ny = a("{x}")\nreturn x',
            'x = a("{x}")\nreturn x',
            'x = a("hi")\nx = a("hi")\nreturn x',
            'input = a("hi")\nreturn input',
            'x = a("hi")\nreturn missing',
            'x = a("hi")\nreturn x\ny = a("later")',
            'import os\nreturn x',
            'x = a(__import__("os"))\nreturn x',
            'x = a(42)\nreturn x',
        ]
        for body in bodies:
            with self.subTest(body=body), self.assertRaises(ParseError):
                parse('workflow Example(input):\nagent a = "role"\n' + body)

    def test_literal_json_and_duplicate_placeholders(self):
        workflow = parse('''workflow Example(input):
agent a = "role"
x = a("JSON: {\\"a\\": 1}; {input}")
y = a("{x} and {x}")
return y''')
        self.assertEqual(workflow.steps[1].dependencies, ("x",))


if __name__ == "__main__":
    unittest.main()
