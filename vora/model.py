from dataclasses import dataclass


@dataclass(frozen=True)
class Agent:
    name: str
    role: str


@dataclass(frozen=True)
class Step:
    name: str
    agent: str
    prompt: str
    dependencies: tuple[str, ...]


@dataclass(frozen=True)
class Workflow:
    name: str
    input_name: str
    agents: dict[str, Agent]
    steps: tuple[Step, ...]
    output: str
