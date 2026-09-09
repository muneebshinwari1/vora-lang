"""Small declarative grammar. Never evaluates Python or executes DSL expressions."""
import json
import re
from pathlib import Path

from .model import Agent, Step, Workflow

IDENT = r"[A-Za-z_][A-Za-z0-9_]*"
PLACEHOLDER = re.compile(r"\{(" + IDENT + r")\}")


class ParseError(ValueError):
    pass


def parse(source: str) -> Workflow:
    lines = [(n, line.strip()) for n, line in enumerate(source.splitlines(), 1)
             if line.strip() and not line.lstrip().startswith("#")]
    if not lines:
        raise ParseError("Empty workflow")
    n, header = lines.pop(0)
    match = re.fullmatch(rf"workflow ({IDENT})\(({IDENT})\):", header)
    if not match:
        raise ParseError(f"Line {n}: expected workflow Name(input):")
    name, input_name = match.groups()
    agents, raw_steps, symbols = {}, [], {input_name}
    output = None

    def string(value: str, line: int) -> str:
        try:
            result = json.loads(value)
        except (ValueError, TypeError) as exc:
            raise ParseError(f"Line {line}: use a double-quoted JSON string") from exc
        if not isinstance(result, str) or not result.strip():
            raise ParseError(f"Line {line}: expected a non-empty string")
        return result

    for n, line in lines:
        if output is not None:
            raise ParseError(f"Line {n}: return must be the last statement")
        declaration = re.fullmatch(rf"agent ({IDENT}) = (.+)", line)
        invocation = re.fullmatch(rf"({IDENT}) = ({IDENT})\((.+)\)", line)
        returned = re.fullmatch(rf"return ({IDENT})", line)
        if declaration:
            symbol, role = declaration.groups()
            if symbol in symbols:
                raise ParseError(f"Line {n}: duplicate name '{symbol}'")
            symbols.add(symbol)
            agents[symbol] = Agent(symbol, string(role, n))
        elif invocation:
            symbol, agent, prompt = invocation.groups()
            if symbol in symbols:
                raise ParseError(f"Line {n}: duplicate name '{symbol}'")
            symbols.add(symbol)
            raw_steps.append((n, symbol, agent, string(prompt, n)))
        elif returned:
            output = returned.group(1)
        else:
            raise ParseError(f"Line {n}: unsupported statement: {line}")
    step_names = {s[1] for s in raw_steps}
    if output is None or output not in step_names:
        raise ParseError("Workflow must return a defined step")
    steps = []
    for n, symbol, agent, prompt in raw_steps:
        if agent not in agents:
            raise ParseError(f"Line {n}: undefined agent '{agent}'")
        refs = list(dict.fromkeys(PLACEHOLDER.findall(prompt)))
        unknown = set(refs) - step_names - {input_name}
        if unknown:
            raise ParseError(f"Line {n}: undefined reference(s): {', '.join(sorted(unknown))}")
        steps.append(Step(symbol, agent, prompt, tuple(r for r in refs if r != input_name)))
    pending = {step.name: set(step.dependencies) for step in steps}
    visited = set()
    while pending:
        ready = [key for key, deps in pending.items() if deps <= visited]
        if not ready:
            raise ParseError(f"Dependency cycle among: {', '.join(pending)}")
        for key in ready:
            visited.add(key)
            del pending[key]
    return Workflow(name, input_name, agents, tuple(steps), output)


def load(path: str | Path) -> Workflow:
    return parse(Path(path).read_text(encoding="utf-8"))


def plan(workflow: Workflow) -> dict:
    pending = {s.name: set(s.dependencies) for s in workflow.steps}
    visited, layers = set(), []
    while pending:
        ready = [key for key, deps in pending.items() if deps <= visited]
        if not ready:
            raise ParseError("Dependency cycle")
        layers.append(ready)
        for key in ready:
            visited.add(key)
            del pending[key]
    return {
        "workflow": workflow.name,
        "input": workflow.input_name,
        "output": workflow.output,
        "minimum_calls": len(workflow.steps),
        "parallel_layers": layers,
        "steps": [{"name": s.name, "agent": s.agent,
                   "dependencies": list(s.dependencies)} for s in workflow.steps],
    }
